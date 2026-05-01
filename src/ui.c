#include "ui.h"

#include "battery.h"
#include "clipboard.h"
#include "screenshot.h"
#include "session.h"

#include <time.h>

static SDL_Color ui_mod_color(ModState st, SDL_Color base);
static const char *ui_mod_suffix(App* app, ModState st);

void ui_draw_text_utf8(App* app, int x, int y, SDL_Color fg, const char *s) {
  if (!s || !s[0]) return;

  SDL_Surface *surf = TTF_RenderUTF8_Blended(app->font, s, fg);
  if (!surf) return;

  SDL_Texture *tex = SDL_CreateTextureFromSurface(app->renderer, surf);
  if (!tex) { SDL_FreeSurface(surf); return; }

  SDL_Rect dst = { x, y, surf->w, surf->h };
  SDL_RenderCopy(app->renderer, tex, NULL, &dst);

  SDL_DestroyTexture(tex);
  SDL_FreeSurface(surf);
}

int ui_text_width_utf8(App* app, const char *s) {
  int w = 0, h = 0;
  if (!s || !s[0]) return 0;
  if (TTF_SizeUTF8(app->font, s, &w, &h) != 0) return 0;
  return w;
}

void ui_draw_session_menu_overlay(App* app) {
  SDL_Rect r = { MENU_OVERLAY_MARGIN_X, MENU_OVERLAY_MARGIN_Y,
                 SCREEN_W - MENU_OVERLAY_WIDTH_REDUCE, SCREEN_H - MENU_OVERLAY_HEIGHT_REDUCE };

  // 背景
  SDL_SetRenderDrawColor(app->renderer, MENU_OVERLAY_BG_R, MENU_OVERLAY_BG_G, MENU_OVERLAY_BG_B, 255);
  SDL_RenderFillRect(app->renderer, &r);

  // 枠
  SDL_SetRenderDrawColor(app->renderer, 180, 180, 180, 255);
  SDL_RenderDrawRect(app->renderer, &r);

  // タイトル
  const char *title = app->ui.ui_use_nerd_icons ?
    "󰆍 SESSIONS   (󰘌:switch  X:new  Y:del  B:close)" :
    "SESSIONS   (ENT:switch  X:new  Y:del  B:close)" ;
  ui_draw_text_utf8(app, r.x + MENU_OVERLAY_TITLE_X, r.y + MENU_OVERLAY_TITLE_Y, (SDL_Color){240,240,240,255}, title);

  // 仕切り線
  int line_y = r.y + MENU_OVERLAY_TITLE_Y + FONT_H + MENU_OVERLAY_LINE_OFFSET;
  SDL_SetRenderDrawColor(app->renderer, 120, 120, 120, 255);
  SDL_RenderDrawLine(app->renderer, r.x + 10, line_y, r.x + r.w - 10, line_y);

  // リスト
  int list_x = r.x + MENU_OVERLAY_LIST_X;
  int list_y0 = r.y + MENU_OVERLAY_TITLE_Y + FONT_H + MENU_OVERLAY_LIST_Y_BASE;

  const char *cursor = app->ui.ui_use_nerd_icons ? "" : ">";
  int y;
  int hl;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    y = list_y0 + i * (FONT_H + MENU_OVERLAY_LIST_Y_SPACING);
    hl = (i == app->ui.menu_sel);

    const char *state = app->sessions[i].used ? "USED" : "EMPTY";
    int locked = (app->sessions[i].used && session_is_locked(&app->sessions[i]));
    int active = (i == app->active_sess);

    // 行テキスト
    char line[128];
    const char *locked_icon = app->ui.ui_use_nerd_icons ? " 󰌾 LOCK" : " LOCK";
    const char *active_icon  = app->ui.ui_use_nerd_icons ? " 󰄬" : "*";
    snprintf(line, sizeof(line), "%d: %s%s%s",
             i + 1,
             state,
             locked ? locked_icon : "",
             active ? active_icon : "");

    // 選択カーソル
    if (hl) {
      ui_draw_text_utf8(app, r.x + MENU_OVERLAY_LIST_CURSOR_X, y, (SDL_Color){255,200,255,255}, cursor);
    }

    // 選択行は少し明るく
    SDL_Color fg = hl ? (SDL_Color){255,255,255,255} : (SDL_Color){210,210,210,255};
    ui_draw_text_utf8(app, list_x, y, fg, line);
  }

  line_y = list_y0 + MAX_SESSIONS * (FONT_H + MENU_OVERLAY_LIST_Y_SPACING);
  y = line_y + MENU_OVERLAY_SEPARATOR_Y_OFFSET;
  hl = (app->ui.menu_sel == MENU_SCREEN_BLANK_IDX);

  // 仕切り線2
  SDL_SetRenderDrawColor(app->renderer, 64, 64, 64, 255);
  SDL_RenderDrawLine(app->renderer, r.x + 10, line_y, r.x + r.w - 10, line_y);

  if (hl) ui_draw_text_utf8(app, r.x + MENU_OVERLAY_LIST_CURSOR_X, y, (SDL_Color){255,200,255,255}, cursor);

  const char *screen_blank_icon = app->ui.ui_use_nerd_icons ? "󰒲  Screen blank" : "Screen blank";
    
  ui_draw_text_utf8(app, list_x, y,
		    hl ? (SDL_Color){255,255,255,255} : (SDL_Color){210,210,210,255},
		    screen_blank_icon);
}

void ui_draw_rect_thick_inset(App* app, const SDL_Rect *r, int thickness, SDL_Color c) {
  SDL_SetRenderDrawColor(app->renderer, c.r, c.g, c.b, 255);

  SDL_Rect rr = *r;
  for (int i = 0; i < thickness; i++) {
    if (rr.w <= 1 || rr.h <= 1) break;
    SDL_RenderDrawRect(app->renderer, &rr);
    rr.x += 1;
    rr.y += 1;
    rr.w -= 2;
    rr.h -= 2;
  }
}

void ui_draw_key_button(App* app, int x0, int y0, int w, int h, const char *label, int selected) {
  SDL_Rect r = { x0, y0, w, h };

  SDL_SetRenderDrawColor(app->renderer, 16, 16, 16, 255);
  SDL_RenderFillRect(app->renderer, &r);

  SDL_Color border = selected ? (SDL_Color){184, 0, 184, 255}
                              : (SDL_Color){32, 32, 32, 255};
  ui_draw_rect_thick_inset(app, &r, selected ? 2 : 1, border);

  int tw = 0, th = 0;
  if (TTF_SizeUTF8(app->font, label, &tw, &th) != 0) return;

  // ボタン内にクリップ（はみ出し保険）
  SDL_Rect clip = { x0 + 2, y0 + 2, w - 4, h - 4 };
  SDL_RenderSetClipRect(app->renderer, &clip);

  int text_x = x0 + (w - tw) / 2;
  int text_y = y0 + (h - th) / 2;
  ui_draw_text_utf8(app, text_x, text_y, (SDL_Color){240,240,240,255}, label);

  SDL_RenderSetClipRect(app->renderer, NULL);
}

void ui_session_menu_open(App* app) {
  app->ui.menu_active = 1;
  app->ui.menu_sel = app->active_sess;
}

void ui_session_menu_close(App* app) {
  app->ui.menu_active = 0;
}

void ui_session_menu_delete_selected(App* app) {
  int idx = app->ui.menu_sel;
  if (!app->sessions[idx].used) return;

  if (session_is_locked(&app->sessions[idx])) {
    return;
  }

  int was_active = (idx == app->active_sess);

  session_destroy(app, idx);

  if (sessions_alive_count(app) == 0) {
    session_create(app, 0);
    app->active_sess = 0;
    ui_session_menu_close(app);
    return;
  }

  if (was_active) {
    int next = session_find_next_alive(app, idx);
    if (next >= 0) app->active_sess = next;
  }

  if (!app->sessions[app->ui.menu_sel].used) {
    int next = session_find_next_alive(app, app->ui.menu_sel);
    if (next >= 0) app->ui.menu_sel = next;
  }
}

void ui_update_timers_and_io(App* app) {
  if (app->pending.screenshot_pending) {
    Uint32 now = SDL_GetTicks();
    if (app->input.btn_start_down) {
      app->pending.screenshot_pending = 0;
    } else if (now - app->pending.screenshot_pending_since >= SCREENSHOT_DELAY_MS) {
      screenshot_save(app->renderer);
      app->pending.screenshot_pending = 0;
    }
  }

  if (app->pending.paste_pending) {
    Uint32 now = SDL_GetTicks();
    if (app->input.btn_select_down) {
      app->pending.paste_pending = 0;
    } else if (now - app->pending.paste_pending_since >= PASTE_DELAY_MS) {
      clipboard_paste(app);
      app->pending.paste_pending = 0;
      app->need_redraw = 1;
    }
  }

  if (sessions_pump_io(app)) app->need_redraw = 1;

  time_t t = time(NULL);
  struct tm *tm_now = localtime(&t);
  if (tm_now && tm_now->tm_min != app->status_cache.prev_minute) {
    app->status_cache.prev_minute = tm_now->tm_min;
    app->need_redraw = 1;
  }

  Uint32 now_ms = SDL_GetTicks();
  if (now_ms - app->status_cache.last_batt_tick >= BATT_UPDATE_MS) {
    app->status_cache.last_batt_tick = now_ms;
    int b = battery_get_level();
    if (b != app->status_cache.cached_batt) {
      app->status_cache.cached_batt = b;
      app->need_redraw = 1;
    }
  }

  if (!app->ui.menu_active && !SESSION(app)->region_mode) {
    int cursor_on = ((now_ms / CURSOR_BLINK_HALF_MS) % 2) == 0;
    if (cursor_on != app->status_cache.prev_cursor_on) {
      app->status_cache.prev_cursor_on = cursor_on;
      app->need_redraw = 1;
    }
  }
}

int ui_draw_mod_indicator(App *app, int x, int y, SDL_Color base, const char *icon, ModState st) {
  if (st == MOD_OFF) return x;

  char buf[64];
  snprintf(buf, sizeof(buf), "%s%s", icon, ui_mod_suffix(app, st));

  SDL_Color fg = (st == MOD_LOCKED)
    ? base                  // LOCKEDはベース色のまま濃くしても良い
    : (SDL_Color){ (uint8_t)(base.r * 0.8), (uint8_t)(base.g * 0.8), (uint8_t)(base.b * 0.8), 255 }; // ONESHOTを少し薄く

  ui_draw_text_utf8(app, x, y, fg, buf);
  return x + ui_text_width_utf8(app, buf) + ui_text_width_utf8(app, "  ");
}

static SDL_Color ui_mod_color(ModState st, SDL_Color base) {
  switch (st) {
    case MOD_ONESHOT: return (SDL_Color){ base.r, base.g, base.b, 255 }; // 通常
    case MOD_LOCKED:  return (SDL_Color){ 255, 255, 255, 255 };         // 白で強調（好みで）
    default:          return (SDL_Color){ 0, 0, 0, 0 };
  }
}

static const char *ui_mod_suffix(App* app, ModState st) {
  const char *oneshot_icon = app->ui.ui_use_nerd_icons ? " ¹" : " 1";
  const char *locked_icon  = app->ui.ui_use_nerd_icons ? " " : " L";
  switch (st) {
    case MOD_ONESHOT: return oneshot_icon;  // 1回だけ、の目印
    case MOD_LOCKED:  return locked_icon;  // Nerd Fontのロック（別の鍵でもOK）
    default:          return "";
  }
}
