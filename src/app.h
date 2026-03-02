#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <vterm.h>

#define SCREEN_W 640
#define SCREEN_H 480

#define FONT_W 12
#define FONT_H 24

#define TERM_COLS 53
#define TERM_ROWS 13
#define TERM_Y 26

#define KEY_ROWS 4
#define KEY_COLS 10

#define SCROLLBACK_LINES 2000
#define MAX_SESSIONS 5

#define STATUS_Y 0

#define CUR_KEY_ROW 0
#define CUR_KEY_COL 9

#define PASTE_DELAY_MS 120

#define SCREENSHOT_DELAY_MS 120

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

typedef enum {
  MOD_OFF = 0,
  MOD_ONESHOT,
  MOD_LOCKED
} ModState;

typedef struct {
  uint32_t ch;
  SDL_Color fg, bg;
  uint8_t width;     // 0/1/2
  uint8_t reverse;
} ScrollbackCell;

typedef struct {
  struct App *app;
  
  int used;

  int pty_fd;
  pid_t pid;

  VTerm *vt;
  VTermScreen *vts;
  VTermState *vts_state;

  ScrollbackCell sb_buf[SCROLLBACK_LINES][TERM_COLS];
  uint8_t sb_cont[SCROLLBACK_LINES];
  int sb_head;
  int sb_count;
  int view_offset_lines;

  int region_mode;
  int selecting;
  int reg_line, reg_col;
  int sel_line, sel_col;
} Session;

typedef struct {
  const char *label; // UTF-8
  const char *send;  // send_key()用
} KeyDefinition;

typedef struct {
  uint32_t cp;
  SDL_Texture *tex;
  int w, h;
  uint8_t used;
} GlyphCacheEntry;

#define GLYPH_CACHE_SIZE 4096

typedef struct {
  char font_path[512];  // 空文字列なら未指定扱い
  int  font_size;       // 例: 18
} AppConfig;

typedef struct App {
  AppConfig cfg;
  
  // SDL
  SDL_Window *win;
  SDL_Renderer *renderer;
  TTF_Font *font;
  SDL_Joystick *joy;

  // app loop
  int quit;
  int need_redraw;

  // sessions
  Session sessions[MAX_SESSIONS];
  int active_sess;

  // menu
  int menu_active;
  int menu_sel;

  // keyboard
  int kbd_layer;
  int kbd_sel_row;
  int kbd_sel_col;

  ModState mod_shift;
  ModState mod_ctrl;
  ModState mod_alt;
  ModState mod_meta;

  int cursor_mode;
  int saved_kbd_row;
  int saved_kbd_col;

  // input state
  int btn_start_down;
  int btn_select_down;

  // copy buffer
  char *copy_buf;
  size_t copy_len;
  size_t copy_cap;

  // pending actions
  int paste_pending;
  Uint32 paste_pending_since;

  int screenshot_pending;
  Uint32 screenshot_pending_since;

  // status cache
  int prev_cursor_on;
  int prev_minute;
  Uint32 last_batt_tick;
  int cached_batt;

  // terminal default colors
  SDL_Color def_fg;
  SDL_Color def_bg;

  // glyph cache
  GlyphCacheEntry glyph_cache[GLYPH_CACHE_SIZE];

  // key layers
  const KeyDefinition (*layers)[KEY_ROWS][KEY_COLS];

  bool ui_use_nerd_icons;
} App;

static inline Session *SESS(App *app) {
  return &app->sessions[app->active_sess];
}
