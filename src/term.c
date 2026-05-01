#include "term.h"

#include "input.h"

#include <unistd.h>

static void term_pty_send_str(App* app, const char *s);
static SDL_Color term_color_to_rgb(VTermState *st, VTermColor c);

SDL_Color term_fg_to_sdl(App* app, VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_DEFAULT_FG) return app->render.def_fg;
  return term_color_to_rgb(st, c);
}

SDL_Color term_bg_to_sdl(App* app, VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_DEFAULT_BG) return app->render.def_bg;
  return term_color_to_rgb(st, c);
}

void term_send_arrow_up(App* app)    { term_pty_send_str(app, "\x1b[A"); }
void term_send_arrow_down(App* app)  { term_pty_send_str(app, "\x1b[B"); }
void term_send_arrow_right(App* app) { term_pty_send_str(app, "\x1b[C"); }
void term_send_arrow_left(App* app)  { term_pty_send_str(app, "\x1b[D"); }

void term_pty_send_byte(App* app, unsigned char b) {
  if (SESSION(app)->pty_fd >= 0) write(SESSION(app)->pty_fd, &b, 1);
}

void term_pty_send_byte_with_altmeta(App* app, unsigned char b) {
  if (mod_active(app->input.mod_alt) || mod_active(app->input.mod_meta)) term_pty_send_byte(app, 0x1B);
  term_pty_send_byte(app, b);
}

static void term_pty_send_str(App* app, const char *s) {
  if (SESSION(app)->pty_fd >= 0) write(SESSION(app)->pty_fd, s, strlen(s));
}

static SDL_Color term_color_to_rgb(VTermState *st, VTermColor c) {
  if (c.type == VTERM_COLOR_RGB)
    return (SDL_Color){c.rgb.red, c.rgb.green, c.rgb.blue, 255};

  vterm_state_convert_color_to_rgb(st, &c);
  return (SDL_Color){c.rgb.red, c.rgb.green, c.rgb.blue, 255};
}
