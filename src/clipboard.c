#include "clipboard.h"
#include "text.h"

#include <SDL2/SDL.h>
#include <string.h>
#include <unistd.h>

static void clipboard_buf_reset(App* app);
static void clipboard_buf_ensure(App* app, size_t add);
static void clipboard_buf_append_byte(App* app, char b);
static void clipboard_buf_append_utf8(App* app, uint32_t c);
static void clipboard_paste_text_to_pty(App* app, const char *s);
static int sb_get_cell_virtual(App* app, int vline, int col, uint32_t *out_ch);
static int sb_virtual_line_is_continuation(App* app, int vline);

void clipboard_copy_selection(App* app) {
  if (!SESSION(app)->region_mode || !SESSION(app)->selecting) return;

  int l1 = SESSION(app)->sel_line;
  int c1 = SESSION(app)->sel_col;
  int l2 = SESSION(app)->reg_line;
  int c2 = SESSION(app)->reg_col;

  // 正規化
  if (l1 > l2 || (l1 == l2 && c1 > c2)) {
    int t;
    t = l1; l1 = l2; l2 = t;
    t = c1; c1 = c2; c2 = t;
  }

  int total = SESSION(app)->sb_count + TERM_ROWS;
  if (total <= 0) return;

  if (l1 < 0) l1 = 0;
  if (l1 >= total) l1 = total - 1;
  if (l2 < 0) l2 = 0;
  if (l2 >= total) l2 = total - 1;
  if (c1 < 0) c1 = 0;
  if (c1 >= TERM_COLS) c1 = TERM_COLS - 1;
  if (c2 < 0) c2 = 0;
  if (c2 >= TERM_COLS) c2 = TERM_COLS - 1;

  clipboard_buf_reset(app);

  for (int v = l1; v <= l2; v++) {
    int from, to;
    if (l1 == l2) { from = c1; to = c2; }
    else if (v == l1) { from = c1; to = TERM_COLS - 1; }
    else if (v == l2) { from = 0;  to = c2; }
    else { from = 0; to = TERM_COLS - 1; }

    int eff_to = to;
    if (to == TERM_COLS - 1) {
      while (eff_to >= from) {
        uint32_t ch = 0;
        int w = sb_get_cell_virtual(app, v, eff_to, &ch);
        if (w == 0) { eff_to--; continue; }
        if (ch != ' ') break;
        eff_to--;
      }
      if (eff_to < from) eff_to = from - 1;
    }

    for (int col = from; col <= eff_to; col++) {
      uint32_t ch = 0;
      int w = sb_get_cell_virtual(app, v, col, &ch);

      if (w == 0) continue;
      if (ch == 0) ch = ' ';

      clipboard_buf_append_utf8(app, ch);

      if (w == 2 && col < eff_to) col++;
    }

    if (v != l2 && !sb_virtual_line_is_continuation(app, v + 1)) {
      clipboard_buf_append_byte(app, '\n');
    }
  }

  if (app->clipboard.copy_buf && app->clipboard.copy_buf[0]) {
    SDL_SetClipboardText(app->clipboard.copy_buf);
  }
}

void clipboard_paste(App* app) {
  if (SDL_HasClipboardText()) {
    char *clip = SDL_GetClipboardText();
    if (clip && clip[0]) {
      clipboard_paste_text_to_pty(app, clip);
      SDL_free(clip);
      return;
    }
    if (clip) SDL_free(clip);
  }

  if (app->clipboard.copy_buf && app->clipboard.copy_buf[0]) {
    clipboard_paste_text_to_pty(app, app->clipboard.copy_buf);
  }
}

static void clipboard_buf_reset(App* app) {
  app->clipboard.copy_len = 0;
  if (app->clipboard.copy_buf) app->clipboard.copy_buf[0] = '\0';
}

static void clipboard_buf_ensure(App* app, size_t add) {
  if (app->clipboard.copy_len + add + 1 <= app->clipboard.copy_cap) return;
  size_t newcap = app->clipboard.copy_cap ? app->clipboard.copy_cap : 1024;
  while (newcap < app->clipboard.copy_len + add + 1) newcap *= 2;
  app->clipboard.copy_buf = (char*)realloc(app->clipboard.copy_buf, newcap);
  app->clipboard.copy_cap = newcap;
}

static void clipboard_buf_append_byte(App* app, char b) {
  clipboard_buf_ensure(app, 1);
  app->clipboard.copy_buf[app->clipboard.copy_len++] = b;
  app->clipboard.copy_buf[app->clipboard.copy_len] = '\0';
}

static void clipboard_buf_append_utf8(App* app, uint32_t c) {
  char utf8[8];
  int len = utf8_encode_cp(c, utf8);

  clipboard_buf_ensure(app, (size_t)len);
  memcpy(app->clipboard.copy_buf + app->clipboard.copy_len, utf8, (size_t)len);
  app->clipboard.copy_len += (size_t)len;
  app->clipboard.copy_buf[app->clipboard.copy_len] = '\0';
}

static void clipboard_paste_text_to_pty(App* app, const char *s) {
  if (!s || !s[0]) return;
  if (SESSION(app)->pty_fd >= 0) write(SESSION(app)->pty_fd, s, strlen(s));
}

// scrollback.c からコピーしたヘルパー関数（重複を避けるため static）
static int sb_phys_index(App* app, int i) {
  int sb_head = SESSION(app)->sb_head;
  int sb_count = SESSION(app)->sb_count;
  int base = sb_head - sb_count;
  while (base < 0) base += SCROLLBACK_LINES;
  return (base + i) % SCROLLBACK_LINES;
}

static int sb_get_cell_virtual(App* app, int vline, int col, uint32_t *out_ch) {
  if (vline < SESSION(app)->sb_count) {
    int p = sb_phys_index(app, vline);
    ScrollbackCell *cell = &SESSION(app)->sb_buf[p][col];
    *out_ch = cell->ch;
    return (int)cell->width;
  }

  int vrow = vline - SESSION(app)->sb_count;
  if (vrow < 0 || vrow >= TERM_ROWS) { *out_ch = ' '; return 1; }

  VTermPos pos = { .row = vrow, .col = col };
  VTermScreenCell cell;
  if (!vterm_screen_get_cell(SESSION(app)->vts, pos, &cell)) { *out_ch = ' '; return 1; }

  if (cell.width == 0) { *out_ch = 0; return 0; }
  *out_ch = cell.chars[0] ? cell.chars[0] : ' ';
  return (int)cell.width;
}

static int sb_virtual_line_is_continuation(App* app, int vline) {
  if (vline < 0) return 0;

  if (vline < SESSION(app)->sb_count) {
    int p = sb_phys_index(app, vline);
    return SESSION(app)->sb_cont[p] ? 1 : 0;
  }
  return 0;
}
