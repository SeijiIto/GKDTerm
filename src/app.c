#include "app.h"

#include "backlight.h"
#include "config.h"
#include "input.h"
#include "render.h"
#include "session.h"
#include "text.h"
#include "ui.h"

#include <unistd.h>

static const char *const k_required_nerd_icons[] = {
  "󰘴", "󰘵", "󰘳", "󰘶", "", "", "󰘌", "󰘠", "⎋", "␣", "⌫", "󰩭", "󰄬", "", "¹", NULL
};

int app_init(App *app) {
  if (config_load_or_create(app, "gkd_term") != 0) {
    printf("Failed to load or create config.\n");
  }
  
  app->render.def_fg = (SDL_Color){240,240,240,255};
  app->render.def_bg = (SDL_Color){0,0,0,255};
  app->need_redraw = 1;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) return -1;

  app->win = SDL_CreateWindow("GKDTerm", 0, 0, SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
  if (!app->win) return -1;

  app->renderer = SDL_CreateRenderer(app->win, -1, SDL_RENDERER_ACCELERATED);
  if (!app->renderer) return -1;

  if (init_font_with_fallbacks(app, app->cfg.font_path, app->cfg.font_size) < 0) {
    printf("Failed to load font.\n");
    return -1;
  }

  app->ui.ui_use_nerd_icons = font_has_all_glyphs_utf8(app, k_required_nerd_icons);
  app->ui.layers = app->ui.ui_use_nerd_icons ? layers : layers_ascii;

  app->joy = (SDL_NumJoysticks() > 0) ? SDL_JoystickOpen(0) : NULL;

  app->active_sess = 0;
  session_create(app, 0);

  (void)backlight_init(app);

  return 0;
}

void app_run(App *app) {
  while (!app->quit) {
    input_handle_input(app);
    ui_update_timers_and_io(app);

    int did_render = 0;
    if (app->need_redraw) {
      app->need_redraw = 0;
      did_render = 1;
      render_frame(app);
    }
    
    if (app->backlight.screen_blank) SDL_Delay(50); 
    else SDL_Delay(did_render ? 1 : 12);
  }
}

void app_shutdown(App *app) {
  for (int i = 0; i < MAX_SESSIONS; i++) session_destroy(app, i);

  glyph_cache_clear(app);
  if (app->font) { TTF_CloseFont(app->font); app->font = NULL; }
  TTF_Quit();

  if (app->joy) { SDL_JoystickClose(app->joy); app->joy = NULL; }

  if (app->renderer) { SDL_DestroyRenderer(app->renderer); app->renderer = NULL; }
  if (app->win) { SDL_DestroyWindow(app->win); app->win = NULL; }

  SDL_Quit();
}

void app_enter_blank(App *app) {
  app->backlight.screen_blank = 1;
  app->backlight.wake_armed = 0;
  (void)backlight_off(app);
  app->need_redraw = 1;
}

void app_exit_blank(App *app) {
  (void)backlight_restore(app);
  app->backlight.screen_blank = 0;
  app->backlight.wake_armed = 0;
  app->need_redraw = 1;
}
