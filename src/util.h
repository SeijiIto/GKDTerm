#pragma once

#include "app.h"

#include <SDL2/SDL_image.h>

static inline int is_dpad(int b) {
  return (b >= BTN_UP && b <= BTN_RIGHT);
}

int get_battery_level(void);

void save_screenshot(SDL_Renderer *renderer);

int config_load_or_create(App *app, const char *appname_dir);

