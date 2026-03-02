#include "scrollback.h"
#include "text.h"

#include <unistd.h>

static void region_norm(App* app, int *l1, int *c1, int *l2, int *c2);
static void copybuf_reset(App* app);
static void copybuf_ensure(App* app, size_t add);
static void copybuf_append_byte(App* app, char b);
static void copybuf_append_utf8(App* app, uint32_t c);
static int get_cell_virtual(App* app, int vline, int col, uint32_t *out_ch);
static int virtual_line_is_continuation(App* app, int vline);
static int sb_oldest_index(App* app);
static void paste_text_to_pty(App* app, const char *s);

int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void region_copy_selection_stream(App* app) {
  if (!SESS(app)->region_mode || !SESS(app)->selecting) return;

  int l1,c1,l2,c2;
  region_norm(app, &l1,&c1,&l2,&c2);

  int total = virtual_total_lines(app);
  if (total <= 0) return;

  l1 = clampi(l1, 0, total - 1);
  l2 = clampi(l2, 0, total - 1);
  c1 = clampi(c1, 0, TERM_COLS - 1);
  c2 = clampi(c2, 0, TERM_COLS - 1);

  copybuf_reset(app);

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
	int w = get_cell_virtual(app, v, eff_to, &ch);
	if (w == 0) { eff_to--; continue; } // 後半セルは飛ばして前半へ	
	if (ch != ' ') break;
	eff_to--;
      }
      if (eff_to < from) eff_to = from - 1;
    }

    for (int col = from; col <= eff_to; col++) {
      uint32_t ch = 0;
      int w = get_cell_virtual(app, v, col, &ch);
      
      if (w == 0) continue;
      if (ch == 0) ch = ' ';
      
      copybuf_append_utf8(app, ch);
      
      if (w == 2 && col < eff_to) col++;
    }

    if (v != l2 && !virtual_line_is_continuation(app, v + 1)) {
      copybuf_append_byte(app, '\n');
    }
  }

  if (app->copy_buf && app->copy_buf[0]) {
    SDL_SetClipboardText(app->copy_buf);
  }
}

void region_ensure_visible(App* app) {
  int start = virtual_start_line(app);
  int end   = start + (TERM_ROWS - 1);

  if (SESS(app)->reg_line < start) {
    int delta = start - SESS(app)->reg_line;
    SESS(app)->view_offset_lines += delta;
    if (SESS(app)->view_offset_lines > SESS(app)->sb_count) SESS(app)->view_offset_lines = SESS(app)->sb_count;
  } else if (SESS(app)->reg_line > end) {
    int delta = SESS(app)->reg_line - end;
    SESS(app)->view_offset_lines -= delta;
    if (SESS(app)->view_offset_lines < 0) SESS(app)->view_offset_lines = 0;
  }
}

void region_enter(App* app) {
  SESS(app)->region_mode = 1;
  SESS(app)->selecting = 0;

  int start = virtual_start_line(app);
  int v = start + (TERM_ROWS - 1);
  int total = virtual_total_lines(app);
  if (total > 0) v = clampi(v, 0, total - 1);

  SESS(app)->reg_line = v;
  SESS(app)->reg_col = 0;
}

void region_exit(App* app) {
  SESS(app)->region_mode = 0;
  SESS(app)->selecting = 0;
}

void region_line_hl_range(App* app, int vline, int *from, int *to) {
  *from = 1;
  *to = 0;

  if (!SESS(app)->region_mode || !SESS(app)->selecting) return;

  int l1,c1,l2,c2;
  region_norm(app, &l1,&c1,&l2,&c2);

  if (vline < l1 || vline > l2) return;

  if (l1 == l2) { *from = c1; *to = c2; return; }
  if (vline == l1) { *from = c1; *to = TERM_COLS - 1; return; }
  if (vline == l2) { *from = 0;  *to = c2; return; }

  *from = 0;
  *to = TERM_COLS - 1;
}

int sb_phys_index(App* app, int i) {
  int base = sb_oldest_index(app);
  return (base + i) % SCROLLBACK_LINES;
}

int virtual_start_line(App* app) {
  if (SESS(app)->view_offset_lines < 0) SESS(app)->view_offset_lines = 0;
  if (SESS(app)->view_offset_lines > SESS(app)->sb_count) SESS(app)->view_offset_lines = SESS(app)->sb_count;
  return SESS(app)->sb_count - SESS(app)->view_offset_lines;
}

int virtual_total_lines(App* app) {
  return SESS(app)->sb_count + TERM_ROWS;
}

void paste_from_buffers(App* app) {
  if (SDL_HasClipboardText()) {
    char *clip = SDL_GetClipboardText();
    if (clip && clip[0]) {
      paste_text_to_pty(app, clip);
      SDL_free(clip);
      return;
    }
    if (clip) SDL_free(clip);
  }

  if (app->copy_buf && app->copy_buf[0]) {
    paste_text_to_pty(app, app->copy_buf);
  }
}


static void region_norm(App* app, int *l1, int *c1, int *l2, int *c2) {
  int aL = SESS(app)->sel_line, aC = SESS(app)->sel_col;
  int bL = SESS(app)->reg_line, bC = SESS(app)->reg_col;

  if (aL > bL || (aL == bL && aC > bC)) {
    int t;
    t=aL; aL=bL; bL=t;
    t=aC; aC=bC; bC=t;
  }
  *l1=aL; *c1=aC; *l2=bL; *c2=bC;
}

static void copybuf_reset(App* app) {
  app->copy_len = 0;
  if (app->copy_buf) app->copy_buf[0] = '\0';
}

static void copybuf_ensure(App* app, size_t add) {
  if (app->copy_len + add + 1 <= app->copy_cap) return;
  size_t newcap = app->copy_cap ? app->copy_cap : 1024;
  while (newcap < app->copy_len + add + 1) newcap *= 2;
  app->copy_buf = (char*)realloc(app->copy_buf, newcap);
  app->copy_cap = newcap;
}

static void copybuf_append_byte(App* app, char b) {
  copybuf_ensure(app, 1);
  app->copy_buf[app->copy_len++] = b;
  app->copy_buf[app->copy_len] = '\0';
}

static void copybuf_append_utf8(App* app, uint32_t c) {
  char utf8[8];
  int len = utf8_encode_cp(c, utf8);

  copybuf_ensure(app, (size_t)len);
  memcpy(app->copy_buf + app->copy_len, utf8, (size_t)len);
  app->copy_len += (size_t)len;
  app->copy_buf[app->copy_len] = '\0';
}

static int get_cell_virtual(App* app, int vline, int col, uint32_t *out_ch) {
  // 戻り値: width (0/1/2)
  if (vline < SESS(app)->sb_count) {
    int p = sb_phys_index(app, vline);
    ScrollbackCell *cell = &SESS(app)->sb_buf[p][col];
    *out_ch = cell->ch;
    return (int)cell->width;
  }

  int vrow = vline - SESS(app)->sb_count;
  if (vrow < 0 || vrow >= TERM_ROWS) { *out_ch = ' '; return 1; }

  VTermPos pos = { .row = vrow, .col = col };
  VTermScreenCell cell;
  if (!vterm_screen_get_cell(SESS(app)->vts, pos, &cell)) { *out_ch = ' '; return 1; }

  if (cell.width == 0) { *out_ch = 0; return 0; }
  *out_ch = cell.chars[0] ? cell.chars[0] : ' ';
  return (int)cell.width; // 1 or 2
}

static int virtual_line_is_continuation(App* app, int vline) {
  if (vline < 0) return 0;

  if (vline < SESS(app)->sb_count) {
    int p = sb_phys_index(app, vline);
    return SESS(app)->sb_cont[p] ? 1 : 0;
  }
  return 0;
}

static int sb_oldest_index(App* app) {
  int idx = SESS(app)->sb_head - SESS(app)->sb_count;
  while (idx < 0) idx += SCROLLBACK_LINES;
  return idx;
}

static void paste_text_to_pty(App* app, const char *s) {
  if (!s || !s[0]) return;
  if (SESS(app)->pty_fd >= 0) write(SESS(app)->pty_fd, s, strlen(s));
}
