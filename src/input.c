#include "input.h"
#include "session.h"
#include "ui.h"
#include "term.h"
#include "util.h"
#include "scrollback.h"

static void mod_cycle(ModState *s);
static int is_modifier_token(const char *k);

void handle_input(App* app) {
  static Uint32 last_repeat_time = 0;
  static int active_button = -1;
  static int repeat_count = 0;

  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_JOYBUTTONDOWN) {
      int b = e.jbutton.button;

      if (b == BTN_SELECT) {
        app->btn_select_down = 1;
        if (app->btn_start_down || app->paste_pending) {
          app->quit = 1;
          active_button = -1;
	  continue;
        }
        app->screenshot_pending = 1;
        app->screenshot_pending_since = SDL_GetTicks();
        continue;
      }
      if (b == BTN_START) {
        app->btn_start_down = 1;
        if (app->btn_select_down || app->screenshot_pending) {
          app->quit = 1;
          active_button = -1;
          continue;
        }
        app->paste_pending = 1;
        app->paste_pending_since = SDL_GetTicks();
        app->need_redraw = 1;
        continue;
      }

      if (app->btn_select_down && app->btn_start_down) {
        app->quit = 1;
        active_button = -1;
        continue;
      }

      if (b == BTN_MENU) {
        if (!app->menu_active) session_menu_open(app);
        else session_menu_close(app);
        app->need_redraw = 1;
        active_button = -1;
        continue;
      }

      if (app->menu_active) {
        process_session_menu(app, b);
        app->need_redraw = 1;
      } else {
        active_button = b;
        last_repeat_time = SDL_GetTicks();
        repeat_count = 0;
        process_key_move(app, active_button);
        app->need_redraw = 1;
      }
    }
    else if (e.type == SDL_JOYBUTTONUP) {
      int b = e.jbutton.button;

      if (b == BTN_SELECT) { app->btn_select_down = 0; continue; }
      if (b == BTN_START)  { app->btn_start_down  = 0; continue; }

      if (b == active_button) active_button = -1;
    }
  }

  if (active_button != -1 && is_dpad(active_button)) {
    Uint32 now = SDL_GetTicks();
    Uint32 delay = (repeat_count == 0) ? 300 : 80;

    if (now - last_repeat_time > delay) {
      if (app->menu_active) process_session_menu(app, active_button);
      else process_key_move(app, active_button);
      app->need_redraw = 1;
      last_repeat_time = now;
      repeat_count++;
    }
  }
}

void process_key_move(App* app, int btn) {
  switch (btn) {
  case BTN_B:
    if (SESS(app)->region_mode) {
      if (SESS(app)->selecting) {
        SESS(app)->selecting = 0;
      } else {
        region_exit(app);
      }
    } else {
      send_key(app, "BS");
    }
    break;

  case BTN_A:
    const KeyDefinition *k = &app->layers[app->kbd_layer][app->kbd_sel_row][app->kbd_sel_col];
    send_key(app, k->send);
    break;

  case BTN_X:
    if (SESS(app)->region_mode) {
      if (!SESS(app)->selecting) {
        SESS(app)->selecting = 1;
        SESS(app)->sel_line = SESS(app)->reg_line;
        SESS(app)->sel_col  = SESS(app)->reg_col;
      } else {
        SESS(app)->selecting = 0;
      }
    }
    else if (app->cursor_mode) {
      region_enter(app);
    } else {
      send_key(app, "ENT");
    }
    break;

  case BTN_Y:
    if (SESS(app)->region_mode) {
      region_copy_selection_stream(app);
      region_exit(app);
    } else {
      send_key(app, "SP");
    }
    break;

  case BTN_L1:
    app->kbd_layer = (app->kbd_layer + 1) % 3;
    break;

  case BTN_R1:
    send_key(app, "Tab");
    break;

  case BTN_L2:
    SESS(app)->view_offset_lines += 5;
    if (SESS(app)->view_offset_lines > SESS(app)->sb_count) SESS(app)->view_offset_lines = SESS(app)->sb_count;
    break;

  case BTN_R2:
    SESS(app)->view_offset_lines -= 5;
    if (SESS(app)->view_offset_lines < 0) SESS(app)->view_offset_lines = 0;
    break;

  case BTN_UP:
    if (SESS(app)->region_mode) {
      int total = virtual_total_lines(app);
      if (total > 0) SESS(app)->reg_line = clampi(SESS(app)->reg_line - 1, 0, total - 1);
      region_ensure_visible(app);
    }
    else if (app->cursor_mode) send_arrow_up(app);
    else app->kbd_sel_row = (app->kbd_sel_row - 1 + KEY_ROWS) % KEY_ROWS;
    break;

  case BTN_DOWN:
    if (SESS(app)->region_mode) {
      int total = virtual_total_lines(app);
      if (total > 0) SESS(app)->reg_line = clampi(SESS(app)->reg_line + 1, 0, total - 1);
      region_ensure_visible(app);
    }
    else if (app->cursor_mode) send_arrow_down(app);
    else app->kbd_sel_row = (app->kbd_sel_row + 1) % KEY_ROWS;
    break;

  case BTN_LEFT:
    if (SESS(app)->region_mode) {
      SESS(app)->reg_col = clampi(SESS(app)->reg_col - 1, 0, TERM_COLS - 1);
    }
    else if (app->cursor_mode) send_arrow_left(app);
    else app->kbd_sel_col = (app->kbd_sel_col - 1 + KEY_COLS) % KEY_COLS;
    break;

  case BTN_RIGHT:
    if (SESS(app)->region_mode) {
      SESS(app)->reg_col = clampi(SESS(app)->reg_col + 1, 0, TERM_COLS - 1);
    }
    else if (app->cursor_mode) send_arrow_right(app);
    else app->kbd_sel_col = (app->kbd_sel_col + 1) % KEY_COLS;
    break;

  default:
    break;
  }
}

void process_session_menu(App* app, int btn) {
  switch(btn) {
    case BTN_MENU:
    case BTN_B:
      session_menu_close(app);
      break;

    case BTN_UP:
      app->menu_sel = (app->menu_sel + MAX_SESSIONS - 1) % MAX_SESSIONS;
      break;

    case BTN_DOWN:
      app->menu_sel = (app->menu_sel + 1) % MAX_SESSIONS;
      break;

    case BTN_A:
      session_switch(app, app->menu_sel);
      session_menu_close(app);
      break;

    case BTN_X:
      session_create(app, app->menu_sel);
      break;

    case BTN_Y:
      session_menu_delete_selected(app);
      break;
  }
}

void send_key(App* app, const char *k) {
  if (strcmp(k, "Ctrl") == 0) { mod_cycle(&app->mod_ctrl); app->need_redraw = 1; return; }
  if (strcmp(k, "Shift") == 0) { mod_cycle(&app->mod_shift); app->need_redraw = 1; return; }
  if (strcmp(k, "Alt") == 0)   { mod_cycle(&app->mod_alt); app->need_redraw = 1; return; }
  if (strcmp(k, "Meta") == 0)  { mod_cycle(&app->mod_meta); app->need_redraw = 1; return; }

  if (strcmp(k, "SP") == 0) { pty_send_byte_with_altmeta(app, ' '); mods_consume_oneshot(app); return; }
  if (strcmp(k, "BS") == 0) { pty_send_byte_with_altmeta(app, 0x7f); mods_consume_oneshot(app); return; }
  if (strcmp(k, "ENT") == 0) { pty_send_byte_with_altmeta(app, '\n'); mods_consume_oneshot(app); return; }
  if (strcmp(k, "Tab") == 0) { pty_send_byte_with_altmeta(app, '\t'); mods_consume_oneshot(app); return; }
  if (strcmp(k, "Esc") == 0) { pty_send_byte(app, 0x1B); mods_consume_oneshot(app); return; }
  if (strcmp(k, "CUR") == 0) {
    if (!app->cursor_mode) {
      app->mod_ctrl = app->mod_alt = app->mod_meta = app->mod_shift = MOD_OFF;

      app->cursor_mode = 1;
      app->saved_kbd_row = app->kbd_sel_row;
      app->saved_kbd_col = app->kbd_sel_col;
      app->kbd_sel_row = CUR_KEY_ROW;
      app->kbd_sel_col = CUR_KEY_COL;
    } else {
      app->cursor_mode = 0;
      app->kbd_sel_row = app->saved_kbd_row;
      app->kbd_sel_col = app->saved_kbd_col;

      SESS(app)->region_mode = 0;
    }
    return;
  }

  if (k[0] != '\0') {
    unsigned char c = (unsigned char)k[0];
    if (mod_active(app->mod_shift) && c >= 'a' && c <= 'z') c -= 32;
    if (mod_active(app->mod_ctrl)) c &= 0x1f;
    pty_send_byte_with_altmeta(app, c);
    mods_consume_oneshot(app);
    return;
  }
}

void mods_consume_oneshot(App *app) {
  if (app->mod_ctrl  == MOD_ONESHOT) app->mod_ctrl  = MOD_OFF;
  if (app->mod_alt   == MOD_ONESHOT) app->mod_alt   = MOD_OFF;
  if (app->mod_meta  == MOD_ONESHOT) app->mod_meta  = MOD_OFF;
  if (app->mod_shift == MOD_ONESHOT) app->mod_shift = MOD_OFF;
}

static void mod_cycle(ModState *s) {
  if (*s == MOD_OFF) *s = MOD_ONESHOT;
  else if (*s == MOD_ONESHOT) *s = MOD_LOCKED;
  else *s = MOD_OFF; // LOCKED -> OFF
}

static int is_modifier_token(const char *k) {
  return strcmp(k, "Ctrl") == 0 ||
         strcmp(k, "Alt") == 0 ||
         strcmp(k, "Meta") == 0 ||
         strcmp(k, "Shift") == 0;
}
