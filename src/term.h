#pragma once

#include "app.h"

SDL_Color vterm_fg_to_sdl(App *app, VTermState *st, VTermColor c);
SDL_Color vterm_bg_to_sdl(App *app, VTermState *st, VTermColor c);

void send_arrow_up(App* app);
void send_arrow_down(App* app);
void send_arrow_right(App* app);
void send_arrow_left(App* app);

void pty_send_byte(App* app, unsigned char b);
void pty_send_byte_with_altmeta(App* app, unsigned char b);
