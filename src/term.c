#include "term.h"
#include "input.h"
#include <unistd.h>

static void pty_send_str(App* app, const char *s);
static SDL_Color vterm_color_to_rgb(VTermState *st, VTermColor c);

SDL_Color vterm_fg_to_sdl(App* app, VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_DEFAULT_FG) return app->def_fg;
  return vterm_color_to_rgb(st, c);
}

SDL_Color vterm_bg_to_sdl(App* app, VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_DEFAULT_BG) return app->def_bg;
  return vterm_color_to_rgb(st, c);
}

void send_arrow_up(App* app)    { pty_send_str(app, "\x1b[A"); }
void send_arrow_down(App* app)  { pty_send_str(app, "\x1b[B"); }
void send_arrow_right(App* app) { pty_send_str(app, "\x1b[C"); }
void send_arrow_left(App* app)  { pty_send_str(app, "\x1b[D"); }

void pty_send_byte(App* app, unsigned char b) {
  if (SESS(app)->pty_fd >= 0) write(SESS(app)->pty_fd, &b, 1);
}

void pty_send_byte_with_altmeta(App* app, unsigned char b) {
  if (mod_active(app->mod_alt) || mod_active(app->mod_meta)) pty_send_byte(app, 0x1B);
  pty_send_byte(app, b);
}

static void pty_send_str(App* app, const char *s) {
  if (SESS(app)->pty_fd >= 0) write(SESS(app)->pty_fd, s, strlen(s));
}

static SDL_Color vterm_color_to_rgb(VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_RGB)
    return (SDL_Color){c.rgb.red, c.rgb.green, c.rgb.blue, 255};

  vterm_state_convert_color_to_rgb(st, &c);
  return (SDL_Color){c.rgb.red, c.rgb.green, c.rgb.blue, 255};
}
