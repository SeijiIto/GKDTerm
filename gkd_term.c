#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#include <vterm.h>

/*
  GKDTerm: SDL2 + libvterm の簡易端末

  - PTY 上で /bin/bash（無ければ /bin/sh）を起動
  - libvterm で端末制御を解釈し、SDL2 で描画
  - スクロールバック保持（sb_pushline4 で押し出し行を蓄積）
  - リージョン選択（リージョンカーソル移動→開始→コピー）
  - ペースト（OSクリップボード優先→内部バッファ）
  - マルチセッション（最大5、MENUで切替/作成/削除）
  - dirty 描画（変化があった時だけ再描画）
*/

#define SESS() (&g_sessions[g_active_sess])

/* ===== 定数（画面/フォント/端末/入力） ===== */
#define SCREEN_W 640
#define SCREEN_H 480

#define FONT_W 12
#define FONT_H 24

#define TERM_COLS 53
#define TERM_ROWS 13
#define TERM_Y 26

#define STATUS_Y 0

#define KEY_ROWS 4
#define KEY_COLS 10

#define SCROLLBACK_LINES 2000

#define CUR_KEY_ROW 0
#define CUR_KEY_COL 9

#define PASTE_DELAY_MS 120

#define MAX_SESSIONS 5

#define CURSOR_BLINK_HALF_MS 250
#define BATT_UPDATE_MS 5000

typedef enum {
  BTN_B = 0,
  BTN_A = 1,
  BTN_X = 2,
  BTN_Y = 3,
  BTN_L1 = 4,
  BTN_R1 = 5,
  BTN_L2 = 6,
  BTN_R2 = 7,
  BTN_SELECT = 8,
  BTN_START = 9,
  BTN_UP = 10,
  BTN_DOWN = 11,
  BTN_LEFT = 12,
  BTN_RIGHT = 13,
  BTN_MENU = 14,
} ButtonId;

/* ===== 端末セル/スクロールバック/セッション状態 ===== */
typedef struct {
  uint32_t ch;
  SDL_Color fg, bg;
  uint8_t width;
  uint8_t reverse;
} sb_cell_t;

typedef struct {
  int used;

  int pty_fd;
  pid_t pid;

  VTerm *vt;
  VTermScreen *vts;
  VTermState *vts_state;

  sb_cell_t sb_buf[SCROLLBACK_LINES][TERM_COLS];
  uint8_t sb_cont[SCROLLBACK_LINES];
  int sb_head;
  int sb_count;
  int view_offset_lines;

  int region_mode;
  int selecting;
  int reg_line, reg_col;
  int sel_line, sel_col;
} session_t;

/* ===== グローバル状態（アプリ） ===== */
static session_t g_sessions[MAX_SESSIONS];
static int g_active_sess = 0;

static int g_menu_active = 0;
static int g_menu_sel = 0;

static SDL_Renderer *g_renderer = NULL;
static TTF_Font *g_font = NULL;
static SDL_Texture *g_char_cache[256];

static int g_quit = 0;

static int g_kbd_layer = 0;
static int g_kbd_sel_row = 0;
static int g_kbd_sel_col = 0;

static int g_mod_shift = 0;
static int g_mod_ctrl  = 0;
static int g_mod_alt   = 0;
static int g_mod_meta  = 0;

static int g_cursor_mode = 0;
static int g_saved_kbd_row = 0, g_saved_kbd_col = 0;

static SDL_Color g_def_fg = {240,240,240,255};
static SDL_Color g_def_bg = {0,0,0,255};

static SDL_Color g_ansi_colors[16] = {
  {   0,   0,   0, 255 },
  { 255,  50,  50, 255 },
  {  50, 255,  50, 255 },
  { 255, 255,  50, 255 },
  {  80,  80, 255, 255 },
  { 255,  50, 255, 255 },
  {  50, 255, 255, 255 },
  { 240, 240, 240, 255 },
  { 100, 100, 100, 255 },
  { 255, 100, 100, 255 },
  { 100, 255, 100, 255 },
  { 255, 255, 100, 255 },
  { 120, 120, 255, 255 },
  { 255, 120, 255, 255 },
  { 120, 255, 255, 255 },
  { 255, 255, 255, 255 }
};

static const char *g_layers[3][KEY_ROWS][KEY_COLS] = {
  {
    { "Ctrl","Alt","Meta","Shift","Tab","Esc","SP","BS","ENT","CUR" },
    { "q","w","e","r","t","y","u","i","o","p" },
    { "a","s","d","f","g","h","j","k","l",";" },
    { "z","x","c","v","b","n","m",",",".","/" }
  },
  {
    { "Ctrl","Alt","Meta","Shift","Tab","Esc","SP","BS","ENT","CUR" },
    { "1","2","3","4","5","6","7","8","9","0" },
    { "!","@","#","$","%","^","&","*","(",")" },
    { "-","_","=","+","[","]","{","}",";",":" }
  },
  {
    { "Ctrl","Alt","Meta","Shift","Tab","Esc","SP","BS","ENT","CUR" },
    { "|","\\","`","~","<",">","?","\"","'","$" },
    { "{","}","[","]","(",")","_","-","+","=" },
    { "/","*","&","^","%","!","#","@","|","\\" }
  }
};

static char *g_copy_buf = NULL;
static size_t g_copy_len = 0;
static size_t g_copy_cap = 0;

static int g_btn_start_down = 0;
static int g_btn_select_down = 0;

static int g_paste_pending = 0;
static Uint32 g_paste_pending_since = 0;

static int g_need_redraw = 1;

static int g_prev_cursor_on = -1;
static int g_prev_minute = -1;
static Uint32 g_last_batt_tick = 0;
static int g_cached_batt = -1;

/* ===== プロトタイプ ===== */
static int get_battery_level(void);
static inline int is_dpad(int b);

static int init_font(const char *path, int size);
static void draw_cell(int x, int y, uint32_t c, unsigned char fg, unsigned char bg, int highlight, int wide);
static void draw_cell_rgb(int x, int y, uint32_t c, SDL_Color fg_c, SDL_Color bg_c, int highlight, int wide);

static SDL_Color vterm_color_to_rgb(VTermState *st, VTermColor c);
static SDL_Color vterm_fg_to_sdl(VTermState *st, VTermColor c);
static SDL_Color vterm_bg_to_sdl(VTermState *st, VTermColor c);

static int sb_oldest_index(void);
static int sb_phys_index(int i);

static int virtual_total_lines(void);
static int virtual_start_line(void);
static void region_ensure_visible(void);
static void region_enter(void);
static void region_exit(void);
static void region_norm(int *l1, int *c1, int *l2, int *c2);
static void region_line_hl_range(int vline, int *from, int *to);
static int clampi(int v, int lo, int hi);

static int virtual_line_is_continuation(int vline);
static uint32_t get_cell_ch_virtual(int vline, int col, int *out_width2);

static void copybuf_reset(void);
static void copybuf_ensure(size_t add);
static void copybuf_append_byte(char b);
static void copybuf_append_utf8(uint32_t c);

static void region_copy_selection_stream(void);

static void draw_scrollback_line(int logical_i, int screen_r, int hl_from, int hl_to);
static void draw_vterm_line(int vterm_row, int screen_r, int hl_from, int hl_to);
static void draw_with_scrollback(void);

static void pty_send_byte(unsigned char b);
static void pty_send_byte_with_altmeta(unsigned char b);
static void pty_send_str(const char *s);

static void send_arrow_up(void);
static void send_arrow_down(void);
static void send_arrow_right(void);
static void send_arrow_left(void);

static void send_key(const char *k);
static void process_key_move(int btn);

static int cb_sb_clear(void *user);
static int cb_sb_pushline4(int cols, const VTermScreenCell *cells, bool continuation, void *user);

static void session_init(session_t *s);
static int  session_create(int idx);
static void session_destroy(int idx);
static void session_switch(int idx);
static int  session_is_locked(const session_t *s);
static int  sessions_alive_count(void);
static int  find_next_alive(int from);
static int  sessions_pump_io(void);

static void session_menu_open(void);
static void session_menu_close(void);
static void session_menu_delete_selected(void);
static void process_session_menu(int btn);

static void draw_rect_thick_inset(const SDL_Rect *r, int thickness, SDL_Color c);
static void draw_key_button(int x0, int y0, int w, int h, const char *label, int selected);

static void handle_input(void);
static void update_timers_and_io(void);
static void render_frame(void);

/* ===== util ===== */
static int get_battery_level(void) {
  int capacity = 0;
  FILE *f = fopen("/sys/class/power_supply/battery/capacity", "r");
  if (f) { fscanf(f, "%d", &capacity); fclose(f); }
  return capacity;
}

static inline int is_dpad(int b) {
  return (b >= BTN_UP && b <= BTN_RIGHT);
}

/* ===== vterm色変換 ===== */
static SDL_Color vterm_color_to_rgb(VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_RGB)
    return (SDL_Color){c.rgb.red, c.rgb.green, c.rgb.blue, 255};

  vterm_state_convert_color_to_rgb(st, &c);
  return (SDL_Color){c.rgb.red, c.rgb.green, c.rgb.blue, 255};
}

static SDL_Color vterm_fg_to_sdl(VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_DEFAULT_FG) return g_def_fg;
  return vterm_color_to_rgb(st, c);
}

static SDL_Color vterm_bg_to_sdl(VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_DEFAULT_BG) return g_def_bg;
  return vterm_color_to_rgb(st, c);
}

/* ===== スクロールバック（リング）補助 ===== */
static int sb_oldest_index(void) {
  int idx = SESS()->sb_head - SESS()->sb_count;
  while (idx < 0) idx += SCROLLBACK_LINES;
  return idx;
}

static int sb_phys_index(int i) {
  int base = sb_oldest_index();
  return (base + i) % SCROLLBACK_LINES;
}

/* ===== 仮想行（scrollback + 現画面） ===== */
static int virtual_total_lines(void) {
  return SESS()->sb_count + TERM_ROWS;
}

static int virtual_start_line(void) {
  if (SESS()->view_offset_lines < 0) SESS()->view_offset_lines = 0;
  if (SESS()->view_offset_lines > SESS()->sb_count) SESS()->view_offset_lines = SESS()->sb_count;
  return SESS()->sb_count - SESS()->view_offset_lines;
}

/* ===== リージョン選択 ===== */
static void region_ensure_visible(void) {
  int start = virtual_start_line();
  int end   = start + (TERM_ROWS - 1);

  if (SESS()->reg_line < start) {
    int delta = start - SESS()->reg_line;
    SESS()->view_offset_lines += delta;
    if (SESS()->view_offset_lines > SESS()->sb_count) SESS()->view_offset_lines = SESS()->sb_count;
  } else if (SESS()->reg_line > end) {
    int delta = SESS()->reg_line - end;
    SESS()->view_offset_lines -= delta;
    if (SESS()->view_offset_lines < 0) SESS()->view_offset_lines = 0;
  }
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void region_enter(void) {
  SESS()->region_mode = 1;
  SESS()->selecting = 0;

  int start = virtual_start_line();
  int v = start + (TERM_ROWS - 1);
  int total = virtual_total_lines();
  if (total > 0) v = clampi(v, 0, total - 1);

  SESS()->reg_line = v;
  SESS()->reg_col = 0;
}

static void region_exit(void) {
  SESS()->region_mode = 0;
  SESS()->selecting = 0;
}

static void region_norm(int *l1, int *c1, int *l2, int *c2) {
  int aL = SESS()->sel_line, aC = SESS()->sel_col;
  int bL = SESS()->reg_line, bC = SESS()->reg_col;

  if (aL > bL || (aL == bL && aC > bC)) {
    int t;
    t=aL; aL=bL; bL=t;
    t=aC; aC=bC; bC=t;
  }
  *l1=aL; *c1=aC; *l2=bL; *c2=bC;
}

static void region_line_hl_range(int vline, int *from, int *to) {
  *from = 1;
  *to = 0;

  if (!SESS()->region_mode || !SESS()->selecting) return;

  int l1,c1,l2,c2;
  region_norm(&l1,&c1,&l2,&c2);

  if (vline < l1 || vline > l2) return;

  if (l1 == l2) { *from = c1; *to = c2; return; }
  if (vline == l1) { *from = c1; *to = TERM_COLS - 1; return; }
  if (vline == l2) { *from = 0;  *to = c2; return; }

  *from = 0;
  *to = TERM_COLS - 1;
}

/* ===== continuation（折り返し連結） ===== */
static int virtual_line_is_continuation(int vline) {
  if (vline < 0) return 0;

  if (vline < SESS()->sb_count) {
    int p = sb_phys_index(vline);
    return SESS()->sb_cont[p] ? 1 : 0;
  }
  return 0;
}

/* ===== 仮想座標から1セル取得 ===== */
static uint32_t get_cell_ch_virtual(int vline, int col, int *out_width2) {
  *out_width2 = 0;

  if (vline < SESS()->sb_count) {
    int p = sb_phys_index(vline);
    sb_cell_t *line = SESS()->sb_buf[p];
    sb_cell_t *cell = &line[col];
    if (cell->width == 2) *out_width2 = 1;
    return cell->ch ? cell->ch : ' ';
  }

  int vrow = vline - SESS()->sb_count;
  if (vrow < 0 || vrow >= TERM_ROWS) return ' ';

  VTermPos pos = { .row = vrow, .col = col };
  VTermScreenCell cell;
  if (!vterm_screen_get_cell(SESS()->vts, pos, &cell)) return ' ';

  if (cell.width == 0) return ' ';
  if (cell.width == 2) *out_width2 = 1;

  return cell.chars[0] ? cell.chars[0] : ' ';
}

/* ===== コピー用バッファ（UTF-8） ===== */
static void copybuf_reset(void) {
  g_copy_len = 0;
  if (g_copy_buf) g_copy_buf[0] = '\0';
}

static void copybuf_ensure(size_t add) {
  if (g_copy_len + add + 1 <= g_copy_cap) return;
  size_t newcap = g_copy_cap ? g_copy_cap : 1024;
  while (newcap < g_copy_len + add + 1) newcap *= 2;
  g_copy_buf = (char*)realloc(g_copy_buf, newcap);
  g_copy_cap = newcap;
}

static void copybuf_append_byte(char b) {
  copybuf_ensure(1);
  g_copy_buf[g_copy_len++] = b;
  g_copy_buf[g_copy_len] = '\0';
}

static void copybuf_append_utf8(uint32_t c) {
  char utf8[8];
  int len = 0;

  if (c <= 0x7F) {
    utf8[len++] = (char)c;
  } else if (c <= 0x7FF) {
    utf8[len++] = (char)(0xC0 | (c >> 6));
    utf8[len++] = (char)(0x80 | (c & 0x3F));
  } else if (c <= 0xFFFF) {
    utf8[len++] = (char)(0xE0 | (c >> 12));
    utf8[len++] = (char)(0x80 | ((c >> 6) & 0x3F));
    utf8[len++] = (char)(0x80 | (c & 0x3F));
  } else {
    utf8[len++] = (char)(0xF0 | (c >> 18));
    utf8[len++] = (char)(0x80 | ((c >> 12) & 0x3F));
    utf8[len++] = (char)(0x80 | ((c >> 6) & 0x3F));
    utf8[len++] = (char)(0x80 | (c & 0x3F));
  }

  copybuf_ensure((size_t)len);
  memcpy(g_copy_buf + g_copy_len, utf8, (size_t)len);
  g_copy_len += (size_t)len;
  g_copy_buf[g_copy_len] = '\0';
}

/* ===== リージョン選択範囲をコピー（continuationは改行無し連結） ===== */
static void region_copy_selection_stream(void) {
  if (!SESS()->region_mode || !SESS()->selecting) return;

  int l1,c1,l2,c2;
  region_norm(&l1,&c1,&l2,&c2);

  int total = virtual_total_lines();
  if (total <= 0) return;

  l1 = clampi(l1, 0, total - 1);
  l2 = clampi(l2, 0, total - 1);
  c1 = clampi(c1, 0, TERM_COLS - 1);
  c2 = clampi(c2, 0, TERM_COLS - 1);

  copybuf_reset();

  for (int v = l1; v <= l2; v++) {
    int from, to;
    if (l1 == l2) { from = c1; to = c2; }
    else if (v == l1) { from = c1; to = TERM_COLS - 1; }
    else if (v == l2) { from = 0;  to = c2; }
    else { from = 0; to = TERM_COLS - 1; }

    int eff_to = to;
    if (to == TERM_COLS - 1) {
      while (eff_to >= from) {
        int w2 = 0;
        uint32_t ch = get_cell_ch_virtual(v, eff_to, &w2);
        if (ch != ' ') break;
        eff_to--;
      }
      if (eff_to < from) eff_to = from - 1;
    }

    for (int col = from; col <= eff_to; col++) {
      int width2 = 0;
      uint32_t ch = get_cell_ch_virtual(v, col, &width2);
      copybuf_append_utf8(ch);
      if (width2 && col < eff_to) col++;
    }

    if (v != l2 && !virtual_line_is_continuation(v + 1)) {
      copybuf_append_byte('\n');
    }
  }

  if (g_copy_buf && g_copy_buf[0]) {
    SDL_SetClipboardText(g_copy_buf);
  }
}

/* ===== ペースト（OSクリップボード優先→内部バッファ） ===== */
static void paste_text_to_pty(const char *s) {
  if (!s || !s[0]) return;
  if (SESS()->pty_fd >= 0) write(SESS()->pty_fd, s, strlen(s));
}

static void paste_from_buffers(void) {
  if (SDL_HasClipboardText()) {
    char *clip = SDL_GetClipboardText();
    if (clip && clip[0]) {
      paste_text_to_pty(clip);
      SDL_free(clip);
      return;
    }
    if (clip) SDL_free(clip);
  }

  if (g_copy_buf && g_copy_buf[0]) {
    paste_text_to_pty(g_copy_buf);
  }
}

/* ===== PTY送信 ===== */
static void pty_send_byte(unsigned char b) {
  if (SESS()->pty_fd >= 0) write(SESS()->pty_fd, &b, 1);
}

static void pty_send_byte_with_altmeta(unsigned char b) {
  if (g_mod_alt || g_mod_meta) pty_send_byte(0x1B);
  pty_send_byte(b);
}

static void pty_send_str(const char *s) {
  if (SESS()->pty_fd >= 0) write(SESS()->pty_fd, s, strlen(s));
}

static void send_arrow_up(void)    { pty_send_str("\x1b[A"); }
static void send_arrow_down(void)  { pty_send_str("\x1b[B"); }
static void send_arrow_right(void) { pty_send_str("\x1b[C"); }
static void send_arrow_left(void)  { pty_send_str("\x1b[D"); }

/* ===== ソフトウェアキーボード入力 → PTY ===== */
static void send_key(const char *k) {
  if (strcmp(k, "SP") == 0) pty_send_byte_with_altmeta(' ');
  else if (strcmp(k, "BS") == 0) pty_send_byte_with_altmeta(0x7f);
  else if (strcmp(k, "ENT") == 0) pty_send_byte_with_altmeta('\n');
  else if (strcmp(k, "Tab") == 0) pty_send_byte_with_altmeta('\t');
  else if (strcmp(k, "Esc") == 0) pty_send_byte(0x1B);

  else if (strcmp(k, "Ctrl") == 0)  g_mod_ctrl  = !g_mod_ctrl;
  else if (strcmp(k, "Shift") == 0) g_mod_shift = !g_mod_shift;
  else if (strcmp(k, "Alt") == 0)   g_mod_alt   = !g_mod_alt;
  else if (strcmp(k, "Meta") == 0)  g_mod_meta  = !g_mod_meta;

  else if (strcmp(k, "CUR") == 0) {
    if (!g_cursor_mode) {
      g_mod_ctrl = 0;
      g_mod_alt = 0;
      g_mod_meta = 0;
      g_mod_shift = 0;

      g_cursor_mode = 1;
      g_saved_kbd_row = g_kbd_sel_row;
      g_saved_kbd_col = g_kbd_sel_col;
      g_kbd_sel_row = CUR_KEY_ROW;
      g_kbd_sel_col = CUR_KEY_COL;
    } else {
      g_cursor_mode = 0;
      g_kbd_sel_row = g_saved_kbd_row;
      g_kbd_sel_col = g_saved_kbd_col;

      SESS()->region_mode = 0;
    }
  }

  else if (k[0] != '\0') {
    unsigned char c = (unsigned char)k[0];
    if (g_mod_shift && c >= 'a' && c <= 'z') c -= 32;
    if (g_mod_ctrl) c &= 0x1f;
    pty_send_byte_with_altmeta(c);
  }
}

/* ===== フォントと描画 ===== */
static int init_font(const char *path, int size) {
  if (TTF_Init() < 0) return -1;

  g_font = TTF_OpenFont(path, size);
  if (!g_font) return -1;

  TTF_SetFontHinting(g_font, TTF_HINTING_MONO);

  SDL_Color white = {255, 255, 255, 255};
  for (int i = 0; i < 256; i++) {
    char s[2] = {(char)i, '\0'};
    SDL_Surface *surf = TTF_RenderText_Solid(g_font, (i < 32 || i == 127) ? " " : s, white);
    g_char_cache[i] = SDL_CreateTextureFromSurface(g_renderer, surf);
    SDL_SetTextureScaleMode(g_char_cache[i], SDL_ScaleModeNearest);
    SDL_FreeSurface(surf);
  }
  return 0;
}

static void draw_cell_rgb(int x, int y, uint32_t c, SDL_Color fg_c, SDL_Color bg_c, int highlight, int wide) {
  if (wide == 2) return;

  int draw_w = (wide == 1) ? FONT_W * 2 : FONT_W;
  SDL_Rect dst = { x, y, draw_w, FONT_H };

  SDL_Color bg = highlight ? (SDL_Color){128, 0, 128, 255} : bg_c;
  SDL_SetRenderDrawColor(g_renderer, bg.r, bg.g, bg.b, 255);
  SDL_RenderFillRect(g_renderer, &dst);

  SDL_Color fg = highlight ? (SDL_Color){255, 255, 255, 255} : fg_c;

  if (c < 256) {
    if (g_char_cache[c]) {
      SDL_SetTextureColorMod(g_char_cache[c], fg.r, fg.g, fg.b);
      SDL_RenderCopy(g_renderer, g_char_cache[c], NULL, &dst);
    }
    return;
  }

  char utf8[8] = {0};
  int len = 0;

  if (c <= 0x7FF) {
    utf8[len++] = 0xC0 | (c >> 6);
    utf8[len++] = 0x80 | (c & 0x3F);
  } else if (c <= 0xFFFF) {
    utf8[len++] = 0xE0 | (c >> 12);
    utf8[len++] = 0x80 | ((c >> 6) & 0x3F);
    utf8[len++] = 0x80 | (c & 0x3F);
  } else {
    utf8[len++] = 0xF0 | (c >> 18);
    utf8[len++] = 0x80 | ((c >> 12) & 0x3F);
    utf8[len++] = 0x80 | ((c >> 6) & 0x3F);
    utf8[len++] = 0x80 | (c & 0x3F);
  }

  SDL_Surface *surf = TTF_RenderUTF8_Solid(g_font, utf8, (SDL_Color){255,255,255,255});
  if (!surf) return;

  SDL_Texture *tmp_tex = SDL_CreateTextureFromSurface(g_renderer, surf);
  if (tmp_tex) {
    SDL_SetTextureColorMod(tmp_tex, fg.r, fg.g, fg.b);
    SDL_RenderCopy(g_renderer, tmp_tex, NULL, &dst);
    SDL_DestroyTexture(tmp_tex);
  }
  SDL_FreeSurface(surf);
}

static void draw_cell(int x, int y, uint32_t c, unsigned char fg, unsigned char bg, int highlight, int wide) {
  if (wide == 2) return;

  int draw_w = (wide == 1) ? FONT_W * 2 : FONT_W;
  SDL_Rect dst = { x, y, draw_w, FONT_H };

  SDL_Color bg_c = highlight ? (SDL_Color){128, 0, 128, 255} : g_ansi_colors[bg % 16];
  SDL_SetRenderDrawColor(g_renderer, bg_c.r, bg_c.g, bg_c.b, 255);
  SDL_RenderFillRect(g_renderer, &dst);

  if (c < 256) {
    if (g_char_cache[c]) {
      SDL_Color fg_c = highlight ? (SDL_Color){255, 255, 255, 255} : g_ansi_colors[fg % 16];
      SDL_SetTextureColorMod(g_char_cache[c], fg_c.r, fg_c.g, fg_c.b);
      SDL_RenderCopy(g_renderer, g_char_cache[c], NULL, &dst);
    }
  } else {
    char utf8[4] = {0};
    utf8[0] = (char)(0xE0 | (c >> 12));
    utf8[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    utf8[2] = (char)(0x80 | (c & 0x3F));

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Solid(g_font, utf8, white);
    if (surf) {
      SDL_Texture *tmp_tex = SDL_CreateTextureFromSurface(g_renderer, surf);
      if (tmp_tex) {
        SDL_Color fg_c = highlight ? (SDL_Color){255, 255, 255, 255} : g_ansi_colors[fg % 16];
        SDL_SetTextureColorMod(tmp_tex, fg_c.r, fg_c.g, fg_c.b);
        SDL_RenderCopy(g_renderer, tmp_tex, NULL, &dst);
        SDL_DestroyTexture(tmp_tex);
      }
      SDL_FreeSurface(surf);
    }
  }
}

/* ===== vtermコールバック（スクロールバック蓄積） ===== */
static VTermScreenCallbacks g_screen_cb = {
  .sb_clear     = cb_sb_clear,
  .sb_pushline4 = cb_sb_pushline4,
};

static void session_init(session_t *s) {
  memset(s, 0, sizeof(*s));
  s->pty_fd = -1;
}

static void session_start_shell(session_t *s) {
  struct winsize ws = { TERM_ROWS, TERM_COLS, 0, 0 };
  pid_t pid = forkpty(&s->pty_fd, NULL, NULL, &ws);

  if (pid == 0) {
    setenv("TERM", "linux", 1);

    const char *home = getenv("HOME");
    if (home && home[0]) chdir(home);
    else { setenv("HOME", "/storage", 1); chdir("/storage"); }

    if (access("/bin/bash", X_OK) == 0) {
      execl("/bin/bash", "bash", "-l", NULL);
    } else {
      execl("/bin/sh", "sh", "-i", NULL);
    }
    _exit(1);
  }

  s->pid = pid;
  fcntl(s->pty_fd, F_SETFL, O_NONBLOCK);
}

static int cb_sb_clear(void *user) {
  session_t *s = (session_t*)user;
  s->sb_head = 0;
  s->sb_count = 0;
  s->view_offset_lines = 0;
  memset(s->sb_cont, 0, sizeof(s->sb_cont));
  return 1;
}

static int cb_sb_pushline4(int cols, const VTermScreenCell *cells, bool continuation, void *user) {
  session_t *s = (session_t*)user;

  sb_cell_t *dst = s->sb_buf[s->sb_head];

  int maxc = cols;
  if (maxc > TERM_COLS) maxc = TERM_COLS;

  for (int c = 0; c < maxc; c++) {
    const VTermScreenCell *cell = &cells[c];
    sb_cell_t out;

    if (cell->width == 0) {
      out.ch = ' ';
      out.width = 1;
    } else {
      out.ch = cell->chars[0] ? cell->chars[0] : ' ';
      out.width = (cell->width == 2) ? 2 : 1;
    }

    out.fg = vterm_fg_to_sdl(s->vts_state, cell->fg);
    out.bg = vterm_bg_to_sdl(s->vts_state, cell->bg);
    out.reverse = cell->attrs.reverse ? 1 : 0;

    dst[c] = out;
  }

  for (int c = maxc; c < TERM_COLS; c++) {
    dst[c] = (sb_cell_t){ .ch=' ', .fg=g_def_fg, .bg=g_def_bg, .width=1, .reverse=0 };
  }

  s->sb_cont[s->sb_head] = continuation ? 1 : 0;

  s->sb_head = (s->sb_head + 1) % SCROLLBACK_LINES;
  if (s->sb_count < SCROLLBACK_LINES) s->sb_count++;

  return 1;
}

static void session_init_vterm(session_t *s) {
  s->vt = vterm_new(TERM_ROWS, TERM_COLS);
  vterm_set_utf8(s->vt, 1);

  s->vts = vterm_obtain_screen(s->vt);
  s->vts_state = vterm_obtain_state(s->vt);

  vterm_screen_set_callbacks(s->vts, &g_screen_cb, s);
  vterm_screen_callbacks_has_pushline4(s->vts);

  vterm_screen_set_damage_merge(s->vts, VTERM_DAMAGE_SCROLL);
  vterm_screen_reset(s->vts, 1);
}

static int session_create(int idx) {
  if (idx < 0 || idx >= MAX_SESSIONS) return -1;
  session_t *s = &g_sessions[idx];
  if (s->used) return 0;

  session_init(s);
  s->used = 1;

  session_start_shell(s);
  session_init_vterm(s);

  s->sb_head = 0;
  s->sb_count = 0;
  s->view_offset_lines = 0;
  memset(s->sb_cont, 0, sizeof(s->sb_cont));

  s->region_mode = 0;
  s->selecting = 0;

  return 0;
}

static int session_is_locked(const session_t *s) {
  if (!s->used || s->pty_fd < 0 || s->pid <= 0) return 0;

  pid_t fg = tcgetpgrp(s->pty_fd);
  if (fg < 0) return 0;

  pid_t sh_pgrp = getpgid(s->pid);
  if (sh_pgrp < 0) return 0;

  return (fg != sh_pgrp);
}

static void session_destroy(int idx) {
  if (idx < 0 || idx >= MAX_SESSIONS) return;
  session_t *s = &g_sessions[idx];
  if (!s->used) return;

  if (s->pid > 0) {
    kill(s->pid, SIGHUP);
    waitpid(s->pid, NULL, WNOHANG);
  }

  if (s->pty_fd >= 0) close(s->pty_fd);
  if (s->vt) vterm_free(s->vt);

  session_init(s);
  s->used = 0;
}

static int sessions_alive_count(void) {
  int n = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) if (g_sessions[i].used) n++;
  return n;
}

static int find_next_alive(int from) {
  for (int i = 1; i <= MAX_SESSIONS; i++) {
    int idx = (from + i) % MAX_SESSIONS;
    if (g_sessions[idx].used) return idx;
  }
  return -1;
}

static void session_switch(int idx) {
  if (idx < 0 || idx >= MAX_SESSIONS) return;
  if (!g_sessions[idx].used) session_create(idx);

  g_cursor_mode = 0;
  g_mod_ctrl = g_mod_alt = g_mod_meta = g_mod_shift = 0;

  g_active_sess = idx;
}

/* ===== 全セッションのPTY出力を吸い上げる（詰まり防止） ===== */
static int sessions_pump_io(void) {
  unsigned char buf[512];
  int active_changed = 0;

  for (int i = 0; i < MAX_SESSIONS; i++) {
    session_t *s = &g_sessions[i];
    if (!s->used || s->pty_fd < 0) continue;

    int n;
    while ((n = read(s->pty_fd, buf, sizeof(buf))) > 0) {
      vterm_input_write(s->vt, (const char*)buf, n);
      vterm_screen_flush_damage(s->vts);
      if (i == g_active_sess) active_changed = 1;
    }
  }
  return active_changed;
}

/* ===== セッションメニュー ===== */
static void session_menu_open(void) {
  g_menu_active = 1;
  g_menu_sel = g_active_sess;
}

static void session_menu_close(void) {
  g_menu_active = 0;
}

static void session_menu_delete_selected(void) {
  int idx = g_menu_sel;
  if (!g_sessions[idx].used) return;

  if (session_is_locked(&g_sessions[idx])) {
    return;
  }

  int was_active = (idx == g_active_sess);

  session_destroy(idx);

  if (sessions_alive_count() == 0) {
    session_create(0);
    g_active_sess = 0;
    session_menu_close();
    return;
  }

  if (was_active) {
    int next = find_next_alive(idx);
    if (next >= 0) g_active_sess = next;
  }

  if (!g_sessions[g_menu_sel].used) {
    int next = find_next_alive(g_menu_sel);
    if (next >= 0) g_menu_sel = next;
  }
}

static void process_session_menu(int btn) {
  switch(btn) {
    case BTN_MENU:
    case BTN_B:
      session_menu_close();
      break;

    case BTN_UP:
      g_menu_sel = (g_menu_sel + MAX_SESSIONS - 1) % MAX_SESSIONS;
      break;

    case BTN_DOWN:
      g_menu_sel = (g_menu_sel + 1) % MAX_SESSIONS;
      break;

    case BTN_A:
      session_switch(g_menu_sel);
      session_menu_close();
      break;

    case BTN_X:
      session_create(g_menu_sel);
      break;

    case BTN_Y:
      session_menu_delete_selected();
      break;
  }
}

/* ===== 端末描画（scrollback + 現画面） ===== */
static void draw_scrollback_line(int logical_i, int screen_r, int hl_from, int hl_to) {
  int p = sb_phys_index(logical_i);
  sb_cell_t *line = SESS()->sb_buf[p];

  for (int c = 0; c < TERM_COLS; c++) {
    sb_cell_t *cell = &line[c];

    SDL_Color fg = cell->fg;
    SDL_Color bg = cell->bg;
    if (cell->reverse) { SDL_Color tmp = fg; fg = bg; bg = tmp; }

    int hl = (c >= hl_from && c <= hl_to) ? 1 : 0;

    int wide = (cell->width == 2) ? 1 : 0;
    draw_cell_rgb(c * FONT_W, TERM_Y + screen_r * FONT_H,
                  cell->ch ? cell->ch : ' ', fg, bg, hl, wide);

    if (cell->width == 2) c++;
  }
}

static void draw_vterm_line(int vterm_row, int screen_r, int hl_from, int hl_to) {
  VTermPos pos;
  VTermScreenCell cell;

  for (int c = 0; c < TERM_COLS; c++) {
    pos.row = vterm_row;
    pos.col = c;

    if (!vterm_screen_get_cell(SESS()->vts, pos, &cell))
      continue;

    if (cell.width == 0) continue;

    uint32_t ch = cell.chars[0] ? cell.chars[0] : ' ';
    SDL_Color fg = vterm_fg_to_sdl(SESS()->vts_state, cell.fg);
    SDL_Color bg = vterm_bg_to_sdl(SESS()->vts_state, cell.bg);

    if (cell.attrs.reverse) { SDL_Color tmp = fg; fg = bg; bg = tmp; }

    int hl = (c >= hl_from && c <= hl_to) ? 1 : 0;

    int wide = (cell.width == 2) ? 1 : 0;
    draw_cell_rgb(c * FONT_W, TERM_Y + screen_r * FONT_H, ch, fg, bg, hl, wide);

    if (cell.width == 2) c++;
  }
}

static void draw_with_scrollback(void) {
  int start = virtual_start_line();

  for (int r = 0; r < TERM_ROWS; r++) {
    int vline = start + r;

    int hl_from, hl_to;
    region_line_hl_range(vline, &hl_from, &hl_to);

    if (vline < SESS()->sb_count) {
      draw_scrollback_line(vline, r, hl_from, hl_to);
    } else {
      int vrow = vline - SESS()->sb_count;
      if (vrow >= 0 && vrow < TERM_ROWS)
        draw_vterm_line(vrow, r, hl_from, hl_to);
    }
  }
}

/* ===== メニュー/キーボードUI描画 ===== */
static void draw_session_menu_overlay(void) {
  SDL_Rect r = { 40, 60, SCREEN_W - 80, SCREEN_H - 228 };

  SDL_SetRenderDrawColor(g_renderer, 32, 32, 32, 255);
  SDL_RenderFillRect(g_renderer, &r);

  SDL_SetRenderDrawColor(g_renderer, 180, 180, 180, 255);
  SDL_RenderDrawRect(g_renderer, &r);

  const char *title = "SESSIONS (A:switch X:new Y:del B:close)";
  int title_x = r.x + 20;
  int title_y = r.y + 10;
  for (int i = 0; title[i]; i++)
    draw_cell(title_x + i*FONT_W, title_y, (unsigned char)title[i], 7, 0, 0, 0);

  SDL_SetRenderDrawColor(g_renderer, 120, 120, 120, 255);
  SDL_RenderDrawLine(g_renderer, r.x + 10, r.y + 10 + FONT_H + 4, r.x + r.w - 10, r.y + 10 + FONT_H + 4);

  int list_x = r.x + 30;
  int list_y0 = r.y + 10 + FONT_H + 12;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    int y = list_y0 + i * (FONT_H + 4);
    int hl = (i == g_menu_sel);

    char line[64];
    const char *state = g_sessions[i].used ? "USED" : "EMPTY";
    const char *lock  = (g_sessions[i].used && session_is_locked(&g_sessions[i])) ? " LOCK" : "";
    snprintf(line, sizeof(line), "%d: %s%s%s", i+1, state, lock, (i==g_active_sess) ? " *" : "");

    if (hl) draw_cell(r.x + 15, y, '>', 3, 0, 0, 0);
    for (int k = 0; line[k]; k++) {
      draw_cell(list_x + k * FONT_W, y, (unsigned char)line[k], 7, 0, hl, 0);
    }
  }
}

static void draw_rect_thick_inset(const SDL_Rect *r, int thickness, SDL_Color c) {
  SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, 255);

  SDL_Rect rr = *r;
  for (int i = 0; i < thickness; i++) {
    if (rr.w <= 1 || rr.h <= 1) break;
    SDL_RenderDrawRect(g_renderer, &rr);
    rr.x += 1;
    rr.y += 1;
    rr.w -= 2;
    rr.h -= 2;
  }
}

static void draw_key_button(int x0, int y0, int w, int h, const char *label, int selected) {
  SDL_Rect r = { x0, y0, w, h };

  SDL_Color bg = (SDL_Color){16, 16, 16, 255};
  SDL_SetRenderDrawColor(g_renderer, bg.r, bg.g, bg.b, 255);
  SDL_RenderFillRect(g_renderer, &r);

  SDL_Color border = selected ? (SDL_Color){184, 0, 184, 255}
                              : (SDL_Color){32, 32, 32, 255};
  int thick = selected ? 2 : 1;
  draw_rect_thick_inset(&r, thick, border);

  int len = (int)strlen(label);
  int text_w = len * FONT_W;

  int max_w = w - 8;
  int max_chars = (max_w > 0) ? (max_w / FONT_W) : 0;
  if (len > max_chars) len = max_chars;
  text_w = len * FONT_W;

  int text_x = x0 + (w - text_w) / 2;
  int text_y = y0 + (h - FONT_H) / 2;

  for (int i = 0; i < len; i++) {
    draw_cell(text_x + i * FONT_W, text_y, (unsigned char)label[i], 7, 0, 0, 0);
  }
}

/* ===== 入力：端末操作/メニュー操作 ===== */
static void process_key_move(int btn) {
  switch (btn) {
  case BTN_B:
    if (SESS()->region_mode) {
      if (SESS()->selecting) {
        SESS()->selecting = 0;
      } else {
        region_exit();
      }
    } else {
      send_key("BS");
    }
    break;

  case BTN_A:
    send_key(g_layers[g_kbd_layer][g_kbd_sel_row][g_kbd_sel_col]);
    break;

  case BTN_X:
    if (SESS()->region_mode) {
      if (!SESS()->selecting) {
        SESS()->selecting = 1;
        SESS()->sel_line = SESS()->reg_line;
        SESS()->sel_col  = SESS()->reg_col;
      } else {
        SESS()->selecting = 0;
      }
    }
    else if (g_cursor_mode) {
      region_enter();
    } else {
      send_key("ENT");
    }
    break;

  case BTN_Y:
    if (SESS()->region_mode) {
      region_copy_selection_stream();
      region_exit();
    } else {
      send_key("SP");
    }
    break;

  case BTN_L1:
    g_kbd_layer = (g_kbd_layer + 1) % 3;
    break;

  case BTN_R1:
    send_key("Tab");
    break;

  case BTN_L2:
    SESS()->view_offset_lines += 5;
    if (SESS()->view_offset_lines > SESS()->sb_count) SESS()->view_offset_lines = SESS()->sb_count;
    break;

  case BTN_R2:
    SESS()->view_offset_lines -= 5;
    if (SESS()->view_offset_lines < 0) SESS()->view_offset_lines = 0;
    break;

  case BTN_UP:
    if (SESS()->region_mode) {
      int total = virtual_total_lines();
      if (total > 0) SESS()->reg_line = clampi(SESS()->reg_line - 1, 0, total - 1);
      region_ensure_visible();
    }
    else if (g_cursor_mode) send_arrow_up();
    else g_kbd_sel_row = (g_kbd_sel_row - 1 + KEY_ROWS) % KEY_ROWS;
    break;

  case BTN_DOWN:
    if (SESS()->region_mode) {
      int total = virtual_total_lines();
      if (total > 0) SESS()->reg_line = clampi(SESS()->reg_line + 1, 0, total - 1);
      region_ensure_visible();
    }
    else if (g_cursor_mode) send_arrow_down();
    else g_kbd_sel_row = (g_kbd_sel_row + 1) % KEY_ROWS;
    break;

  case BTN_LEFT:
    if (SESS()->region_mode) {
      SESS()->reg_col = clampi(SESS()->reg_col - 1, 0, TERM_COLS - 1);
    }
    else if (g_cursor_mode) send_arrow_left();
    else g_kbd_sel_col = (g_kbd_sel_col - 1 + KEY_COLS) % KEY_COLS;
    break;

  case BTN_RIGHT:
    if (SESS()->region_mode) {
      SESS()->reg_col = clampi(SESS()->reg_col + 1, 0, TERM_COLS - 1);
    }
    else if (g_cursor_mode) send_arrow_right();
    else g_kbd_sel_col = (g_kbd_sel_col + 1) % KEY_COLS;
    break;

  default:
    break;
  }
}

static void handle_input(void) {
  static Uint32 last_repeat_time = 0;
  static int active_button = -1;
  static int repeat_count = 0;

  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_JOYBUTTONDOWN) {
      int b = e.jbutton.button;

      if (b == BTN_SELECT) {
        g_btn_select_down = 1;
        if (g_btn_start_down || g_paste_pending) {
          g_quit = 1;
          active_button = -1;
        }
        continue;
      }
      if (b == BTN_START) {
        g_btn_start_down = 1;
        if (g_btn_select_down) {
          g_quit = 1;
          active_button = -1;
          continue;
        }
        g_paste_pending = 1;
        g_paste_pending_since = SDL_GetTicks();
        g_need_redraw = 1;
        continue;
      }

      if (g_btn_select_down && g_btn_start_down) {
        g_quit = 1;
        active_button = -1;
        continue;
      }

      if (b == BTN_MENU) {
        if (!g_menu_active) session_menu_open();
        else session_menu_close();
        g_need_redraw = 1;
        active_button = -1;
        continue;
      }

      if (g_menu_active) {
        process_session_menu(b);
        g_need_redraw = 1;
      } else {
        active_button = b;
        last_repeat_time = SDL_GetTicks();
        repeat_count = 0;
        process_key_move(active_button);
        g_need_redraw = 1;
      }
    }
    else if (e.type == SDL_JOYBUTTONUP) {
      int b = e.jbutton.button;

      if (b == BTN_SELECT) { g_btn_select_down = 0; continue; }
      if (b == BTN_START)  { g_btn_start_down  = 0; continue; }

      if (b == active_button) active_button = -1;
    }
  }

  if (active_button != -1 && is_dpad(active_button)) {
    Uint32 now = SDL_GetTicks();
    Uint32 delay = (repeat_count == 0) ? 300 : 80;

    if (now - last_repeat_time > delay) {
      if (g_menu_active) process_session_menu(active_button);
      else process_key_move(active_button);
      g_need_redraw = 1;
      last_repeat_time = now;
      repeat_count++;
    }
  }
}

/* ===== 更新（dirty条件判定） ===== */
static void update_timers_and_io(void) {
  if (g_paste_pending) {
    Uint32 now = SDL_GetTicks();
    if (g_btn_select_down) {
      g_paste_pending = 0;
    } else if (now - g_paste_pending_since >= PASTE_DELAY_MS) {
      paste_from_buffers();
      g_paste_pending = 0;
      g_need_redraw = 1;
    }
  }

  if (sessions_pump_io()) g_need_redraw = 1;

  time_t t = time(NULL);
  struct tm *tm_now = localtime(&t);
  if (tm_now && tm_now->tm_min != g_prev_minute) {
    g_prev_minute = tm_now->tm_min;
    g_need_redraw = 1;
  }

  Uint32 now_ms = SDL_GetTicks();
  if (now_ms - g_last_batt_tick >= BATT_UPDATE_MS) {
    g_last_batt_tick = now_ms;
    int b = get_battery_level();
    if (b != g_cached_batt) {
      g_cached_batt = b;
      g_need_redraw = 1;
    }
  }

  if (!g_menu_active && !SESS()->region_mode) {
    int cursor_on = ((now_ms / CURSOR_BLINK_HALF_MS) % 2) == 0;
    if (cursor_on != g_prev_cursor_on) {
      g_prev_cursor_on = cursor_on;
      g_need_redraw = 1;
    }
  }
}

/* ===== 描画 ===== */
static void render_frame(void) {
  SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
  SDL_RenderClear(g_renderer);

  SDL_Rect s_bar = {0, 0, SCREEN_W, FONT_H + 2};
  SDL_SetRenderDrawColor(g_renderer, 30, 30, 30, 255);
  SDL_RenderFillRect(g_renderer, &s_bar);

  const char *mode_s = "";
  if (g_kbd_layer == 0) mode_s = "[ABC]";
  else if (g_kbd_layer == 1) mode_s = "[123]";
  else mode_s = "[#&!]";
  for (int i = 0; mode_s[i]; i++)
    draw_cell(5 + i * FONT_W, 1, (unsigned char)mode_s[i], 6, 0, 0, 0);

  if (g_mod_ctrl) {
    const char *s_str = "CTRL";
    for (int i = 0; s_str[i]; i++) draw_cell(150 + i * FONT_W, 1, (unsigned char)s_str[i], 3, 0, 0, 0);
  }
  if (g_mod_alt) {
    const char *a_str = "ALT";
    for (int i = 0; a_str[i]; i++) draw_cell(220 + i * FONT_W, 1, (unsigned char)a_str[i], 5, 0, 0, 0);
  }
  if (g_mod_meta) {
    const char *m_str = "META";
    for (int i = 0; m_str[i]; i++) draw_cell(290 + i * FONT_W, 1, (unsigned char)m_str[i], 4, 0, 0, 0);
  }
  if (g_mod_shift) {
    const char *c_str = "SHIFT";
    for (int i = 0; c_str[i]; i++) draw_cell(360 + i * FONT_W, 1, (unsigned char)c_str[i], 1, 0, 0, 0);
  }
  if (g_cursor_mode) {
    const char *n_str = "CURSOR";
    for (int i = 0; n_str[i]; i++) draw_cell(150 + i * FONT_W, 1, (unsigned char)n_str[i], 6, 0, 0, 0);
  }
  if (SESS()->region_mode) {
    const char *r_str = "REGION";
    for (int i = 0; r_str[i]; i++) draw_cell(220 + i * FONT_W, 1, (unsigned char)r_str[i], 5, 0, 0, 0);
    if (SESS()->selecting) {
      const char *s_str = "SEL";
      for (int i = 0; s_str[i]; i++) draw_cell(220 + 7 * FONT_W + i * FONT_W, 1, (unsigned char)s_str[i], 3, 0, 0, 0);
    }
  }

  char batt_s[16];
  int batt_lv = (g_cached_batt >= 0) ? g_cached_batt : get_battery_level();
  if (batt_lv < 0) batt_lv = 0;
  sprintf(batt_s, "%d%%", batt_lv);
  int batt_x = SCREEN_W - (int)(strlen(batt_s) * FONT_W) - 10;
  for (int i = 0; batt_s[i]; i++)
    draw_cell(batt_x + i * FONT_W, 1, (unsigned char)batt_s[i], 2, 0, 0, 0);

  time_t t = time(NULL);
  struct tm *tm_now = localtime(&t);
  char time_s[10];
  sprintf(time_s, "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);
  int time_x = batt_x - (int)(strlen(time_s) * FONT_W) - 20;
  for (int i = 0; time_s[i]; i++)
    draw_cell(time_x + i * FONT_W, 1, (unsigned char)time_s[i], 7, 0, 0, 0);

  draw_with_scrollback();

  if (g_menu_active) draw_session_menu_overlay();

  int sep_y = (TERM_ROWS * FONT_H) + TERM_Y + 4;
  SDL_SetRenderDrawColor(g_renderer, 96, 96, 96, 255);
  SDL_RenderDrawLine(g_renderer, 0, sep_y, SCREEN_W, sep_y);

  int key_w = SCREEN_W / KEY_COLS;
  int key_h = FONT_H + 10;
  sep_y -= 2;

  for (int r = 0; r < KEY_ROWS; r++) {
    for (int c = 0; c < KEY_COLS; c++) {
      const char *s = g_layers[g_kbd_layer][r][c];
      int x0 = c * key_w;
      int y0 = sep_y + 8 + (r * key_h);
      int selected = (r == g_kbd_sel_row && c == g_kbd_sel_col);
      draw_key_button(x0 + 2, y0, key_w - 4, FONT_H + 6, s, selected);
    }
  }

  if (SESS()->region_mode) {
    int start = virtual_start_line();
    int screen_r = SESS()->reg_line - start;
    if (screen_r >= 0 && screen_r < TERM_ROWS) {
      SDL_Rect rr = { SESS()->reg_col * FONT_W, TERM_Y + screen_r * FONT_H, FONT_W, FONT_H };
      SDL_SetRenderDrawColor(g_renderer, 255, 255, 0, 255);
      SDL_RenderDrawRect(g_renderer, &rr);
    }
  } else if (!g_menu_active) {
    VTermPos cpos;
    vterm_state_get_cursorpos(SESS()->vts_state, &cpos);

    Uint32 now = SDL_GetTicks();
    int cursor_on = ((now / CURSOR_BLINK_HALF_MS) % 2) == 0;

    if (cursor_on) {
      SDL_Rect cr = { cpos.col * FONT_W, TERM_Y + cpos.row * FONT_H, FONT_W, FONT_H };
      SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
      SDL_RenderFillRect(g_renderer, &cr);
    }
  }

  SDL_RenderPresent(g_renderer);
}

/* ===== main ===== */
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) return 1;

  SDL_Window *win = SDL_CreateWindow("GKDTerm", 0, 0, 640, 480, SDL_WINDOW_FULLSCREEN);
  (void)win;

  g_renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

  if (init_font("PixelMplus12-Regular.ttf", 12) < 0) {
    char path[256];
    getcwd(path, sizeof(path));
    printf("Font not found! Current Dir: %s\n", path);
    return 1;
  }

  SDL_Joystick *joy = (SDL_NumJoysticks() > 0) ? SDL_JoystickOpen(0) : NULL;

  session_create(0);
  g_active_sess = 0;

  while (!g_quit) {
    handle_input();
    update_timers_and_io();

    int did_render = 0;
    if (g_need_redraw) {
      g_need_redraw = 0;
      did_render = 1;
      render_frame();
    }
    SDL_Delay(did_render ? 1 : 12);
  }

  if (joy) SDL_JoystickClose(joy);
  SDL_Quit();
  return 0;
}
