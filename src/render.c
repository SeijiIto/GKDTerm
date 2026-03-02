#include "render.h"
#include "ui.h"
#include "text.h"
#include "util.h"
#include "term.h"
#include "input.h"
#include "scrollback.h"
#include <time.h>

void render_frame(App* app) {
  SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
  SDL_RenderClear(app->renderer);

  SDL_Rect s_bar = {0, 0, SCREEN_W, FONT_H + 2};
  SDL_SetRenderDrawColor(app->renderer, 30, 30, 30, 255);
  SDL_RenderFillRect(app->renderer, &s_bar);

  // 左側: レイヤ表示
  const char *mode_s = "";
  if (app->kbd_layer == 0) mode_s = "[ABC]";
  else if (app->kbd_layer == 1) mode_s = "[123]";
  else mode_s = "[#&!]";
  ui_draw_text_utf8(app, 5, 1, (SDL_Color){200,200,200,255}, mode_s);

  // 中央付近: 修飾キー・モード
  int x = 150;
  int step = ui_text_width_utf8(app, "  "); // だいたいの間隔（フォント依存）

  /*
  if (mod_active(app->mod_ctrl))  { ui_draw_text_utf8(app, x, 1, (SDL_Color){255,200,255,255}, "󰘴"); x += step + ui_text_width_utf8(app, "󰘴"); }
  if (mod_active(app->mod_alt))   { ui_draw_text_utf8(app, x, 1, (SDL_Color){255,220,180,255}, "󰘵"); x += step + ui_text_width_utf8(app, "󰘵"); }
  if (mod_active(app->mod_meta))  { ui_draw_text_utf8(app, x, 1, (SDL_Color){180,220,255,255}, "󰘳"); x += step + ui_text_width_utf8(app, "󰘳"); }
  if (mod_active(app->mod_shift)) { ui_draw_text_utf8(app, x, 1, (SDL_Color){255,180,180,255}, "󰘶"); x += step + ui_text_width_utf8(app, "󰘶"); }
  */

  const char *ctrl_icon = app->ui_use_nerd_icons ? "󰘴" : "CTRL";
  const char *alt_icon  = app->ui_use_nerd_icons ? "󰘵" : "ALT";
  const char *meta_icon  = app->ui_use_nerd_icons ? "󰘳" : "META";
  const char *shift_icon  = app->ui_use_nerd_icons ? "󰘶" : "SHIFT";
  
  x = ui_draw_mod_indicator(app, x, 1, (SDL_Color){255,200,255,255}, ctrl_icon, app->mod_ctrl);
  x = ui_draw_mod_indicator(app, x, 1, (SDL_Color){255,220,180,255}, alt_icon, app->mod_alt);
  x = ui_draw_mod_indicator(app, x, 1, (SDL_Color){180,220,255,255}, meta_icon, app->mod_meta);
  x = ui_draw_mod_indicator(app, x, 1, (SDL_Color){255,180,180,255}, shift_icon, app->mod_shift);

  if (app->cursor_mode) {
    const char *cursor_icon  = app->ui_use_nerd_icons ? " CURSOR" : "CURSOR";
    ui_draw_text_utf8(app, 220, 1, (SDL_Color){200,200,200,255}, cursor_icon);
  }

  if (SESS(app)->region_mode) {
    const char *region_icon  = app->ui_use_nerd_icons ? "󰩭 REGION" : "REGION";
    const char *selecting_icon  = app->ui_use_nerd_icons ? "󰩭 REGION SEL" : "REGION_SEL";
    ui_draw_text_utf8(app, 330, 1, (SDL_Color){200,200,200,255},
                      SESS(app)->selecting ? selecting_icon : region_icon);
  }

  // 右側: battery / time を「幅計測して右寄せ」
  char batt_s[16];
  int batt_lv = (app->cached_batt >= 0) ? app->cached_batt : get_battery_level();
  if (batt_lv < 0) batt_lv = 0;
  snprintf(batt_s, sizeof(batt_s), "%d%%", batt_lv);

  time_t t = time(NULL);
  struct tm *tm_now = localtime(&t);
  char time_s[10];
  snprintf(time_s, sizeof(time_s), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);

  int batt_w = ui_text_width_utf8(app, batt_s);
  int time_w = ui_text_width_utf8(app, time_s);

  int right_margin = 10;
  int gap = ui_text_width_utf8(app, "   "); // batteryとtimeの間隔

  int batt_x = SCREEN_W - right_margin - batt_w;
  int time_x = batt_x - gap - time_w;

  ui_draw_text_utf8(app, time_x, 1, (SDL_Color){240,240,240,255}, time_s);
  ui_draw_text_utf8(app, batt_x, 1, (SDL_Color){180,255,180,255}, batt_s);
  
  draw_with_scrollback(app);

  if (app->menu_active) draw_session_menu_overlay(app);

  int sep_y = (TERM_ROWS * FONT_H) + TERM_Y + 4;
  SDL_SetRenderDrawColor(app->renderer, 96, 96, 96, 255);
  SDL_RenderDrawLine(app->renderer, 0, sep_y, SCREEN_W, sep_y);

  int key_w = SCREEN_W / KEY_COLS;
  int key_h = FONT_H + 10;
  sep_y -= 2;

  for (int r = 0; r < KEY_ROWS; r++) {
    for (int c = 0; c < KEY_COLS; c++) {
      int x0 = c * key_w;
      int y0 = sep_y + 8 + (r * key_h);
      int selected = (r == app->kbd_sel_row && c == app->kbd_sel_col);
      const KeyDefinition *k = &app->layers[app->kbd_layer][r][c];
      draw_key_button(app, x0 + 2, y0, key_w - 4, FONT_H + 6, k->label, selected);
    }
  }

  if (SESS(app)->region_mode) {
    int start = virtual_start_line(app);
    int screen_r = SESS(app)->reg_line - start;
    if (screen_r >= 0 && screen_r < TERM_ROWS) {
      SDL_Rect rr = { SESS(app)->reg_col * FONT_W, TERM_Y + screen_r * FONT_H, FONT_W, FONT_H };
      SDL_SetRenderDrawColor(app->renderer, 255, 255, 0, 255);
      SDL_RenderDrawRect(app->renderer, &rr);
    }
  } else if (!app->menu_active) {
    VTermPos cpos;
    vterm_state_get_cursorpos(SESS(app)->vts_state, &cpos);

    Uint32 now = SDL_GetTicks();
    int cursor_on = ((now / CURSOR_BLINK_HALF_MS) % 2) == 0;

    if (cursor_on) {
      SDL_Rect cr = { cpos.col * FONT_W, TERM_Y + cpos.row * FONT_H, FONT_W, FONT_H };
      SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
      SDL_RenderFillRect(app->renderer, &cr);
    }
  }

  SDL_RenderPresent(app->renderer);
}

void draw_with_scrollback(App* app) {
  int start = virtual_start_line(app);

  for (int r = 0; r < TERM_ROWS; r++) {
    int vline = start + r;

    int hl_from, hl_to;
    region_line_hl_range(app, vline, &hl_from, &hl_to);

    if (vline < SESS(app)->sb_count) {
      draw_scrollback_line(app, vline, r, hl_from, hl_to);
    } else {
      int vrow = vline - SESS(app)->sb_count;
      if (vrow >= 0 && vrow < TERM_ROWS)
        draw_vterm_line(app, vrow, r, hl_from, hl_to);
    }
  }
}

void draw_vterm_line(App* app, int vterm_row, int screen_r, int hl_from, int hl_to) {
  VTermPos pos;
  VTermScreenCell cell;

  for (int c = 0; c < TERM_COLS; c++) {
    pos.row = vterm_row;
    pos.col = c;

    if (!vterm_screen_get_cell(SESS(app)->vts, pos, &cell))
      continue;

    if (cell.width == 0) continue;

    uint32_t ch = cell.chars[0] ? cell.chars[0] : ' ';
    SDL_Color fg = vterm_fg_to_sdl(app, SESS(app)->vts_state, cell.fg);
    SDL_Color bg = vterm_bg_to_sdl(app, SESS(app)->vts_state, cell.bg);

    if (cell.attrs.reverse) { SDL_Color tmp = fg; fg = bg; bg = tmp; }

    int hl = (c >= hl_from && c <= hl_to) ? 1 : 0;

    int wide = (cell.width == 2) ? 1 : 0;
    draw_cell_rgb(app, c * FONT_W, TERM_Y + screen_r * FONT_H, ch, fg, bg, hl, wide);

    if (cell.width == 2) c++;
  }
}

void draw_scrollback_line(App* app, int logical_i, int screen_r, int hl_from, int hl_to) {
  int p = sb_phys_index(app, logical_i);
  ScrollbackCell *line = SESS(app)->sb_buf[p];

  for (int c = 0; c < TERM_COLS; c++) {
    ScrollbackCell *cell = &line[c];

    if (cell->width == 0) continue; // 追加（安全）

    SDL_Color fg = cell->fg;
    SDL_Color bg = cell->bg;
    if (cell->reverse) { SDL_Color tmp = fg; fg = bg; bg = tmp; }

    int hl = (c >= hl_from && c <= hl_to) ? 1 : 0;
    int wide = (cell->width == 2) ? 1 : 0;

    draw_cell_rgb(app, c * FONT_W, TERM_Y + screen_r * FONT_H,
                  cell->ch ? cell->ch : ' ', fg, bg, hl, wide);

    if (cell->width == 2) c++; // 後半セルをスキップ
  }
}

void draw_cell_rgb(App* app,
		   int x, int y, uint32_t c,
		   SDL_Color fg_c, SDL_Color bg_c,
		   int highlight, int wide) {
  if (wide == 2) return;

  int draw_w = (wide == 1) ? FONT_W * 2 : FONT_W;
  SDL_Rect cell = { x, y, draw_w, FONT_H };

  SDL_Color bg = highlight ? (SDL_Color){128, 0, 128, 255} : bg_c;
  SDL_SetRenderDrawColor(app->renderer, bg.r, bg.g, bg.b, 255);
  SDL_RenderFillRect(app->renderer, &cell);

  SDL_Color fg = highlight ? (SDL_Color){255, 255, 255, 255} : fg_c;

  int gw = 0, gh = 0;
  SDL_Texture *tex = glyph_get_texture(app, c, &gw, &gh);
  if (!tex) return;

  SDL_SetTextureColorMod(tex, fg.r, fg.g, fg.b);

  // グリフをセル中央に配置（フォントサイズ誤差や全角での見た目安定用）
  SDL_Rect dst = {
    x + (draw_w - gw) / 2,
    y + (FONT_H - gh) / 2,
    gw,
    gh
  };
  // セルサイズにそのまま貼る
  // SDL_Rect dst = { x, y, draw_w, FONT_H };
  // SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  // SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
  SDL_RenderCopy(app->renderer, tex, NULL, &dst);
}

