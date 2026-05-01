#pragma once
#include "app.h"

void render_frame(App *app);
void render_draw_scrollback_line(App* app, int logical_i, int screen_r, int hl_from, int hl_to);
void render_draw_vterm_line(App* app, int vterm_row, int screen_r, int hl_from, int hl_to);
void render_draw_with_scrollback(App* app);
void render_draw_cell_rgb(App* app,
						  int x, int y, uint32_t c,
						  SDL_Color fg_c, SDL_Color bg_c,
						  int highlight, int wide);
