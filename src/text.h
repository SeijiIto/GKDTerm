#pragma once

#include "app.h"
#include <stdbool.h>

// int init_font(App* app, const char *path, int size);
int init_font_with_fallbacks(App *app, const char *path, int size);

void glyph_cache_clear(App *app);
SDL_Texture *glyph_get_texture(App *app, uint32_t cp, int *out_w, int *out_h);

uint32_t utf8_sanitize_cp(uint32_t c);
int utf8_encode_cp(uint32_t c, char out[8]);

bool font_has_glyph_utf8(App *app, const char *utf8_1char);
bool font_has_all_glyphs_utf8(App *app, const char *const *list);

