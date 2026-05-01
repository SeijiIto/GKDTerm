#pragma once
#include "app.h"

void ui_draw_text_utf8(App *app, int x, int y, SDL_Color fg, const char *s);
int  ui_text_width_utf8(App *app, const char *s);

void ui_draw_rect_thick_inset(App *app, const SDL_Rect *r, int thickness, SDL_Color c);
void ui_draw_key_button(App *app, int x0, int y0, int w, int h, const char *label, int selected);
void ui_draw_session_menu_overlay(App *app);

void ui_session_menu_open(App* app);
void ui_session_menu_close(App* app);
void ui_session_menu_delete_selected(App* app);

void ui_update_timers_and_io(App* app);

int ui_draw_mod_indicator(App *app, int x, int y, SDL_Color base, const char *icon, ModState st);
