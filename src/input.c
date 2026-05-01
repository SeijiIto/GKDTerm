#include "input.h"
#include "clipboard.h"
#include "session.h"
#include "ui.h"
#include "term.h"
#include "util.h"
#include "scrollback.h"

static void input_mod_cycle(ModState *s);
static int input_is_modifier_token(const char *k);
static int input_wake_handle_event(App *app, int btn);

typedef struct {
  Uint32 last_repeat_time;
  int active_button;
  int repeat_count;
} InputRepeatState;

static InputRepeatState g_repeat_state = {0, -1, 0};

static void handle_button_down_event(App* app, int btn, InputRepeatState* state);
static void handle_button_up_event(App* app, int btn, InputRepeatState* state);
static void handle_button_repeat(App* app, InputRepeatState* state);

static void handle_btn_b(App* app);
static void handle_btn_a(App* app);
static void handle_btn_x(App* app);
static void handle_btn_y(App* app);
static void handle_btn_l1(App* app);
static void handle_btn_r1(App* app);
static void handle_btn_l2(App* app);
static void handle_btn_r2(App* app);
static void handle_btn_up(App* app);
static void handle_btn_down(App* app);
static void handle_btn_left(App* app);
static void handle_btn_right(App* app);


void input_handle_input(App* app) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (app->backlight.screen_blank) {
      if (e.type == SDL_JOYBUTTONDOWN) {
        input_wake_handle_event(app, e.jbutton.button);
      }
      continue;
    }

    if (e.type == SDL_JOYBUTTONDOWN) {
      handle_button_down_event(app, e.jbutton.button, &g_repeat_state);
    }
    else if (e.type == SDL_JOYBUTTONUP) {
      handle_button_up_event(app, e.jbutton.button, &g_repeat_state);
    }
  }

  handle_button_repeat(app, &g_repeat_state);
}

void input_key_move(App* app, int btn) {
  switch (btn) {
  case BTN_B:     handle_btn_b(app);     break;
  case BTN_A:     handle_btn_a(app);     break;
  case BTN_X:     handle_btn_x(app);     break;
  case BTN_Y:     handle_btn_y(app);     break;
  case BTN_L1:    handle_btn_l1(app);    break;
  case BTN_R1:    handle_btn_r1(app);    break;
  case BTN_L2:    handle_btn_l2(app);    break;
  case BTN_R2:    handle_btn_r2(app);    break;
  case BTN_UP:    handle_btn_up(app);    break;
  case BTN_DOWN:  handle_btn_down(app);  break;
  case BTN_LEFT:  handle_btn_left(app);  break;
  case BTN_RIGHT: handle_btn_right(app); break;
  default:        break;
  }
}

void input_session_menu(App* app, int btn) {
  switch(btn) {
    case BTN_MENU:
    case BTN_B:
      ui_session_menu_close(app);
      break;

    case BTN_UP:
      app->ui.menu_sel = (app->ui.menu_sel + MENU_ITEMS - 1) % MENU_ITEMS;
      break;

    case BTN_DOWN:
      app->ui.menu_sel = (app->ui.menu_sel + 1) % MENU_ITEMS;
      break;

    case BTN_A:
      if (app->ui.menu_sel == MENU_SCREEN_BLANK_IDX) {
        ui_session_menu_close(app);
        app_enter_blank(app);
      } else {
        session_switch(app, app->ui.menu_sel);
        ui_session_menu_close(app);
      }
      break;

    case BTN_X:
      session_create(app, app->ui.menu_sel);
      break;

    case BTN_Y:
      ui_session_menu_delete_selected(app);
      break;
  }
}

void input_send_key(App* app, const char *k) {
  if (strcmp(k, "Ctrl") == 0) { input_mod_cycle(&app->input.mod_ctrl); app->need_redraw = 1; return; }
  if (strcmp(k, "Shift") == 0) { input_mod_cycle(&app->input.mod_shift); app->need_redraw = 1; return; }
  if (strcmp(k, "Alt") == 0)   { input_mod_cycle(&app->input.mod_alt); app->need_redraw = 1; return; }
  if (strcmp(k, "Meta") == 0)  { input_mod_cycle(&app->input.mod_meta); app->need_redraw = 1; return; }

  if (strcmp(k, "SP") == 0) { term_pty_send_byte_with_altmeta(app, ' '); input_mods_consume_oneshot(app); return; }
  if (strcmp(k, "BS") == 0) { term_pty_send_byte_with_altmeta(app, 0x7f); input_mods_consume_oneshot(app); return; }
  if (strcmp(k, "ENT") == 0) { term_pty_send_byte_with_altmeta(app, '\n'); input_mods_consume_oneshot(app); return; }
  if (strcmp(k, "Tab") == 0) { term_pty_send_byte_with_altmeta(app, '\t'); input_mods_consume_oneshot(app); return; }
  if (strcmp(k, "Esc") == 0) { term_pty_send_byte(app, 0x1B); input_mods_consume_oneshot(app); return; }
  if (strcmp(k, "CUR") == 0) {
    if (!app->input.cursor_mode) {
      app->input.mod_ctrl = app->input.mod_alt = app->input.mod_meta = app->input.mod_shift = MOD_OFF;

      app->input.cursor_mode = 1;
      app->input.saved_kbd_row = app->input.kbd_sel_row;
      app->input.saved_kbd_col = app->input.kbd_sel_col;
      app->input.kbd_sel_row = CUR_KEY_ROW;
      app->input.kbd_sel_col = CUR_KEY_COL;
    } else {
      app->input.cursor_mode = 0;
      app->input.kbd_sel_row = app->input.saved_kbd_row;
      app->input.kbd_sel_col = app->input.saved_kbd_col;

      SESSION(app)->region_mode = 0;
    }
    return;
  }

  if (k[0] != '\0') {
    unsigned char c = (unsigned char)k[0];
    if (mod_active(app->input.mod_shift) && c >= 'a' && c <= 'z') c -= 32;
    if (mod_active(app->input.mod_ctrl)) c &= 0x1f;
    term_pty_send_byte_with_altmeta(app, c);
    input_mods_consume_oneshot(app);
    return;
  }
}

void input_mods_consume_oneshot(App *app) {
  if (app->input.mod_ctrl  == MOD_ONESHOT) app->input.mod_ctrl  = MOD_OFF;
  if (app->input.mod_alt   == MOD_ONESHOT) app->input.mod_alt   = MOD_OFF;
  if (app->input.mod_meta  == MOD_ONESHOT) app->input.mod_meta  = MOD_OFF;
  if (app->input.mod_shift == MOD_ONESHOT) app->input.mod_shift = MOD_OFF;
}

static void input_mod_cycle(ModState *s) {
  if (*s == MOD_OFF) *s = MOD_ONESHOT;
  else if (*s == MOD_ONESHOT) *s = MOD_LOCKED;
  else *s = MOD_OFF; // LOCKED -> OFF
}

static int input_is_modifier_token(const char *k) {
  return strcmp(k, "Ctrl") == 0 ||
         strcmp(k, "Alt") == 0 ||
         strcmp(k, "Meta") == 0 ||
         strcmp(k, "Shift") == 0;
}

static int input_wake_handle_event(App *app, int btn) {
  const Uint32 now = SDL_GetTicks();

  if (!app->backlight.wake_armed) {
    app->backlight.wake_armed = 1;
    app->backlight.wake_btn = btn;
    app->backlight.wake_since = now;
    return 0; // まだ復帰しない
  }

  if (btn == app->backlight.wake_btn && (now - app->backlight.wake_since) <= WAKE_DOUBLE_PUSH_LIMIT_MS) {
    app_exit_blank(app);
    return 1; // 復帰
  }

  // 条件を満たさなかったらアームを更新（次のチャンス）
  app->backlight.wake_btn = btn;
  app->backlight.wake_since = now;
  return 0;
}

static void handle_btn_b(App* app) {
  if (SESSION(app)->region_mode) {
    if (SESSION(app)->selecting) {
      SESSION(app)->selecting = 0;
    } else {
      sb_region_exit(app);
    }
  } else {
    input_send_key(app, "BS");
  }
}

static void handle_btn_a(App* app) {
  const KeyDefinition *k = &app->ui.layers[app->input.kbd_layer][app->input.kbd_sel_row][app->input.kbd_sel_col];
  input_send_key(app, k->send);
}

static void handle_btn_x(App* app) {
  if (SESSION(app)->region_mode) {
    if (!SESSION(app)->selecting) {
      SESSION(app)->selecting = 1;
      SESSION(app)->sel_line = SESSION(app)->reg_line;
      SESSION(app)->sel_col  = SESSION(app)->reg_col;
    } else {
      SESSION(app)->selecting = 0;
    }
  }
  else if (app->input.cursor_mode) {
    sb_region_enter(app);
  } else {
    input_send_key(app, "ENT");
  }
}

static void handle_btn_y(App* app) {
  if (SESSION(app)->region_mode) {
    clipboard_copy_selection(app);
    sb_region_exit(app);
  } else {
    input_send_key(app, "SP");
  }
}

static void handle_btn_l1(App* app) {
  app->input.kbd_layer = (app->input.kbd_layer + 1) % 3;
}

static void handle_btn_r1(App* app) {
  input_send_key(app, "Tab");
}

static void handle_btn_l2(App* app) {
  SESSION(app)->view_offset_lines += 5;
  if (SESSION(app)->view_offset_lines > SESSION(app)->sb_count) {
    SESSION(app)->view_offset_lines = SESSION(app)->sb_count;
  }
}

static void handle_btn_r2(App* app) {
  SESSION(app)->view_offset_lines -= 5;
  if (SESSION(app)->view_offset_lines < 0) {
    SESSION(app)->view_offset_lines = 0;
  }
}

static void handle_btn_up(App* app) {
  if (SESSION(app)->region_mode) {
    int total = sb_virtual_total_lines(app);
    if (total > 0) {
      SESSION(app)->reg_line = sb_clampi(SESSION(app)->reg_line - 1, 0, total - 1);
    }
    sb_region_ensure_visible(app);
  }
  else if (app->input.cursor_mode) {
    term_send_arrow_up(app);
  }
  else {
    app->input.kbd_sel_row = (app->input.kbd_sel_row - 1 + KEY_ROWS) % KEY_ROWS;
  }
}

static void handle_btn_down(App* app) {
  if (SESSION(app)->region_mode) {
    int total = sb_virtual_total_lines(app);
    if (total > 0) {
      SESSION(app)->reg_line = sb_clampi(SESSION(app)->reg_line + 1, 0, total - 1);
    }
    sb_region_ensure_visible(app);
  }
  else if (app->input.cursor_mode) {
    term_send_arrow_down(app);
  }
  else {
    app->input.kbd_sel_row = (app->input.kbd_sel_row + 1) % KEY_ROWS;
  }
}

static void handle_btn_left(App* app) {
  if (SESSION(app)->region_mode) {
    SESSION(app)->reg_col = sb_clampi(SESSION(app)->reg_col - 1, 0, TERM_COLS - 1);
  }
  else if (app->input.cursor_mode) {
    term_send_arrow_left(app);
  }
  else {
    app->input.kbd_sel_col = (app->input.kbd_sel_col - 1 + KEY_COLS) % KEY_COLS;
  }
}

static void handle_btn_right(App* app) {
  if (SESSION(app)->region_mode) {
    SESSION(app)->reg_col = sb_clampi(SESSION(app)->reg_col + 1, 0, TERM_COLS - 1);
  }
  else if (app->input.cursor_mode) {
    term_send_arrow_right(app);
  }
  else {
    app->input.kbd_sel_col = (app->input.kbd_sel_col + 1) % KEY_COLS;
  }
}

static void handle_button_down_event(App* app, int btn, InputRepeatState* state) {
  if (btn == BTN_SELECT) {
    app->input.btn_select_down = 1;
    if (app->input.btn_start_down || app->pending.paste_pending) {
      app->quit = 1;
      state->active_button = -1;
      return;
    }
    app->pending.screenshot_pending = 1;
    app->pending.screenshot_pending_since = SDL_GetTicks();
    return;
  }

  if (btn == BTN_START) {
    app->input.btn_start_down = 1;
    if (app->input.btn_select_down || app->pending.screenshot_pending) {
      app->quit = 1;
      state->active_button = -1;
      return;
    }
    app->pending.paste_pending = 1;
    app->pending.paste_pending_since = SDL_GetTicks();
    app->need_redraw = 1;
    return;
  }

  if (app->input.btn_select_down && app->input.btn_start_down) {
    app->quit = 1;
    state->active_button = -1;
    return;
  }

  if (btn == BTN_MENU) {
    if (!app->ui.menu_active) ui_session_menu_open(app);
    else ui_session_menu_close(app);
    app->need_redraw = 1;
    state->active_button = -1;
    return;
  }

  if (app->ui.menu_active) {
    input_session_menu(app, btn);
    app->need_redraw = 1;
  } else {
    state->active_button = btn;
    state->last_repeat_time = SDL_GetTicks();
    state->repeat_count = 0;
    input_key_move(app, state->active_button);
    app->need_redraw = 1;
  }
}

static void handle_button_up_event(App* app, int btn, InputRepeatState* state) {
  if (btn == BTN_SELECT) {
    app->input.btn_select_down = 0;
    return;
  }
  if (btn == BTN_START) {
    app->input.btn_start_down = 0;
    return;
  }

  if (btn == state->active_button) {
    state->active_button = -1;
  }
}

static void handle_button_repeat(App* app, InputRepeatState* state) {
  if (state->active_button == -1 || !util_is_dpad(state->active_button)) {
    return;
  }

  Uint32 now = SDL_GetTicks();
  Uint32 delay = (state->repeat_count == 0) ? BUTTON_REPEAT_INITIAL_DELAY_MS : BUTTON_REPEAT_INTERVAL_MS;

  if (now - state->last_repeat_time > delay) {
    if (app->ui.menu_active) {
      input_session_menu(app, state->active_button);
    } else {
      input_key_move(app, state->active_button);
    }
    app->need_redraw = 1;
    state->last_repeat_time = now;
    state->repeat_count++;
  }
}
