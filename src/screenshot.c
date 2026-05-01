#include "screenshot.h"

#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <time.h>

void screenshot_save(SDL_Renderer *renderer) {
  int w, h;
  SDL_GetRendererOutputSize(renderer, &w, &h);

  // 32bit surface を作成
  SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!surf) {
    printf("Can't create screenshot: %s\n", SDL_GetError());
    return;
  }

  // レンダラーからピクセルを読み取る
  if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) != 0) {
    printf("Can't locad pixels: %s\n", SDL_GetError());
    SDL_FreeSurface(surf);
    return;
  }

  // 時刻ベースのファイル名を生成
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  char filename[128];
  snprintf(filename, sizeof(filename),
	   "/storage/roms/screenshots/GKDTerm_%04d%02d%02d_%02d%02d%02d.png",
	   tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
	   tm->tm_hour, tm->tm_min, tm->tm_sec);

  if (IMG_SavePNG(surf, filename) == 0) {
    printf("Succeeded: %s\n", filename);
  } else {
    printf("Failed: %s\n", SDL_GetError());
  }

  SDL_FreeSurface(surf);
}
