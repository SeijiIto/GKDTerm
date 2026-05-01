#pragma once

#include "app.h"

SDL_Color term_fg_to_sdl(App *app, VTermState *st, VTermColor c);
SDL_Color term_bg_to_sdl(App *app, VTermState *st, VTermColor c);

void term_send_arrow_up(App* app);
void term_send_arrow_down(App* app);
void term_send_arrow_right(App* app);
void term_send_arrow_left(App* app);

void term_pty_send_byte(App* app, unsigned char b);
void term_pty_send_byte_with_altmeta(App* app, unsigned char b);
