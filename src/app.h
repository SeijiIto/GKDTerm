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

#define MENU_ITEMS (MAX_SESSIONS + 1)
#define MENU_SCREEN_BLANK_IDX (MAX_SESSIONS)

// Input timing constants
#define WAKE_DOUBLE_PUSH_LIMIT_MS 350
#define BUTTON_REPEAT_INITIAL_DELAY_MS 300
#define BUTTON_REPEAT_INTERVAL_MS 80

// UI Layout constants
#define STATUSBAR_BG_R 30
#define STATUSBAR_BG_G 30
#define STATUSBAR_BG_B 30
#define STATUSBAR_LAYER_X 5
#define STATUSBAR_LAYER_Y 1
#define STATUSBAR_MOD_X 150
#define STATUSBAR_CURSOR_X 220
#define STATUSBAR_REGION_X 330
#define STATUSBAR_RIGHT_MARGIN 10
#define STATUSBAR_TIME_BATT_GAP_STR "   "

#define HIGHLIGHT_BG_R 128
#define HIGHLIGHT_BG_G 0
#define HIGHLIGHT_BG_B 128

#define KEYBOARD_SEP_OFFSET_Y 4
#define KEYBOARD_SEP_COLOR_R 96
#define KEYBOARD_SEP_COLOR_G 96
#define KEYBOARD_SEP_COLOR_B 96
#define KEYBOARD_KEY_HEIGHT_EXTRA 10
#define KEYBOARD_KEY_MARGIN 2
#define KEYBOARD_SEP_ADJUST 2

#define MENU_OVERLAY_MARGIN_X 40
#define MENU_OVERLAY_MARGIN_Y 60
#define MENU_OVERLAY_WIDTH_REDUCE 80
#define MENU_OVERLAY_HEIGHT_REDUCE 228
#define MENU_OVERLAY_BG_R 32
#define MENU_OVERLAY_BG_G 32
#define MENU_OVERLAY_BG_B 32
#define MENU_OVERLAY_TITLE_X 20
#define MENU_OVERLAY_TITLE_Y 10
#define MENU_OVERLAY_LINE_OFFSET 4
#define MENU_OVERLAY_LIST_X 30
#define MENU_OVERLAY_LIST_Y_BASE 12
#define MENU_OVERLAY_LIST_CURSOR_X 15
#define MENU_OVERLAY_LIST_Y_SPACING 4
#define MENU_OVERLAY_SEPARATOR_Y_OFFSET 4

// Config constants
#define CONFIG_FONT_SIZE_MIN 6
#define CONFIG_FONT_SIZE_MAX 96

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

typedef struct {
  // キーボード選択
  int kbd_layer;
  int kbd_sel_row;
  int kbd_sel_col;

  // モディファイアキー
  ModState mod_shift;
  ModState mod_ctrl;
  ModState mod_alt;
  ModState mod_meta;

  // カーソルモード
  int cursor_mode;
  int saved_kbd_row;
  int saved_kbd_col;

  // ボタン状態
  int btn_start_down;
  int btn_select_down;
} InputState;

typedef struct {
  int menu_active;
  int menu_sel;
  bool ui_use_nerd_icons;
  const KeyDefinition (*layers)[KEY_ROWS][KEY_COLS];
} UIState;

typedef struct {
  char *copy_buf;
  size_t copy_len;
  size_t copy_cap;
} ClipboardState;

typedef struct {
  int paste_pending;
  Uint32 paste_pending_since;
  int screenshot_pending;
  Uint32 screenshot_pending_since;
} PendingActions;

typedef struct {
  int prev_cursor_on;
  int prev_minute;
  Uint32 last_batt_tick;
  int cached_batt;
} StatusCache;

typedef struct {
  int screen_blank;
  int backlight_ok;
  char backlight_dir[256];
  int backlight_max;
  int backlight_saved;

  int wake_armed;
  int wake_btn;
  Uint32 wake_since;
} BacklightState;

typedef struct {
  SDL_Color def_fg;
  SDL_Color def_bg;
  GlyphCacheEntry glyph_cache[GLYPH_CACHE_SIZE];
} RenderResources;

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

  // input state
  InputState input;

  // UI state
  UIState ui;

  // clipboard
  ClipboardState clipboard;

  // pending actions
  PendingActions pending;

  // status cache
  StatusCache status_cache;

  // backlight state
  BacklightState backlight;

  // render resources
  RenderResources render;
} App;

static inline Session *SESSION(App *app) {
  return &app->sessions[app->active_sess];
}

void app_enter_blank(App *app);
void app_exit_blank(App *app);
