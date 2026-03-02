#include "text.h"
#include <SDL2/SDL_ttf.h>
#include <stdint.h>

static uint32_t glyph_hash(uint32_t x);
static int try_open_font(App *app, const char *path, int size);
static int utf8_decode_1(const char *s, uint32_t *out_cp);

int init_font_with_fallbacks(App *app, const char *path, int size) {
  if (TTF_WasInit() == 0) {
    if (TTF_Init() < 0) return -1;
  }

  // ユーザー指定を最優先
  if (try_open_font(app, path, size) == 0) return 0;

  // システムフォールバック候補
  static const char *fallbacks[] = {
    "/usr/share/fonts/TTF/font.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "./UDEVGothicNFLG-Regular.ttf",
    NULL
  };

  for (int i = 0; fallbacks[i]; i++) {
    if (try_open_font(app, fallbacks[i], size) == 0) return 0;
  }

  fprintf(stderr, "No usable font found.\n");
  fprintf(stderr, "Tried user font: %s\n", path ? path : "(null)");
  fprintf(stderr, "Tried fallbacks:\n");
  for (int i = 0; fallbacks[i]; i++) fprintf(stderr, "  %s\n", fallbacks[i]);

  return -1;
}

void glyph_cache_clear(App* app) {
  for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
    if (app->glyph_cache[i].used && app->glyph_cache[i].tex) {
      SDL_DestroyTexture(app->glyph_cache[i].tex);
    }
    app->glyph_cache[i] = (GlyphCacheEntry){0};
  }
}

SDL_Texture *glyph_get_texture(App* app, uint32_t cp, int *out_w, int *out_h) {
  if (!out_w || !out_h) return NULL;
  *out_w = 0; *out_h = 0;

  if (cp == 0) cp = ' ';
  // 制御文字は空白扱い
  if (cp < 0x20 || cp == 0x7F) cp = ' ';

  uint32_t h = glyph_hash(cp);
  uint32_t idx = h & (GLYPH_CACHE_SIZE - 1);

  // GLYPH_CACHE_SIZE は 2の冪推奨（4096等）
  for (uint32_t probe = 0; probe < GLYPH_CACHE_SIZE; probe++) {
    GlyphCacheEntry *e = &app->glyph_cache[idx];

    if (e->used) {
      if (e->cp == cp) {
        *out_w = e->w;
        *out_h = e->h;
        return e->tex;
      }
    } else {
      // 空きに挿入
      char utf8[8];
      utf8_encode_cp(cp, utf8);

      SDL_Color white = {255, 255, 255, 255};
      // SDL_Surface *surf = TTF_RenderUTF8_Solid(app->font, utf8, white);
      SDL_Surface *surf = TTF_RenderUTF8_Blended(app->font, utf8, white);
      if (!surf) return NULL;

      SDL_Texture *tex = SDL_CreateTextureFromSurface(app->renderer, surf);
      if (!tex) { SDL_FreeSurface(surf); return NULL; }

      SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

      e->used = 1;
      e->cp = cp;
      e->tex = tex;
      e->w = surf->w;
      e->h = surf->h;

      *out_w = e->w;
      *out_h = e->h;

      SDL_FreeSurface(surf);
      return tex;
    }

    idx = (idx + 1) & (GLYPH_CACHE_SIZE - 1);
  }

  // キャッシュ満杯（本気ならLRU化。まずは諦める）
  return NULL;
}

uint32_t utf8_sanitize_cp(uint32_t c) {
  if (c > 0x10FFFF) return 0xFFFD;
  if (c >= 0xD800 && c <= 0xDFFF) return 0xFFFD;
  return c;
}

int utf8_encode_cp(uint32_t c, char out[8]) {
  c = utf8_sanitize_cp(c);

  int len = 0;
  if (c <= 0x7F) {
    out[len++] = (char)c;
  } else if (c <= 0x7FF) {
    out[len++] = (char)(0xC0 | (c >> 6));
    out[len++] = (char)(0x80 | (c & 0x3F));
  } else if (c <= 0xFFFF) {
    out[len++] = (char)(0xE0 | (c >> 12));
    out[len++] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[len++] = (char)(0x80 | (c & 0x3F));
  } else {
    out[len++] = (char)(0xF0 | (c >> 18));
    out[len++] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[len++] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[len++] = (char)(0x80 | (c & 0x3F));
  }
  out[len] = '\0';
  return len;
}

bool font_has_glyph_utf8(App *app, const char *utf8_1char) {
  if (!app || !app->font || !utf8_1char || !utf8_1char[0]) return false;

  uint32_t cp = 0;
  if (utf8_decode_1(utf8_1char, &cp) < 0) return false;

  // 0なら「無い」、それ以外ならglyph indexが返る
  return TTF_GlyphIsProvided32(app->font, cp) != 0;
}

bool font_has_all_glyphs_utf8(App *app, const char *const *list) {
  for (int i = 0; list[i]; i++) {
    if (!font_has_glyph_utf8(app, list[i])) return false;
  }
  return true;
}

static int utf8_decode_1(const char *s, uint32_t *out_cp) {
  const unsigned char *p = (const unsigned char*)s;
  if (!p || !p[0]) return -1;

  if (p[0] < 0x80) { *out_cp = p[0]; return 1; }

  if ((p[0] & 0xE0) == 0xC0) {
    if ((p[1] & 0xC0) != 0x80) return -1;
    *out_cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    return 2;
  }
  if ((p[0] & 0xF0) == 0xE0) {
    if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return -1;
    *out_cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return 3;
  }
  if ((p[0] & 0xF8) == 0xF0) {
    if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return -1;
    *out_cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    return 4;
  }
  return -1;
}

static uint32_t glyph_hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static int try_open_font(App *app, const char *path, int size) {
  if (!path || !path[0]) return -1;

  TTF_Font *f = TTF_OpenFont(path, size);
  if (!f) return -1;

  // 既存フォント破棄 + キャッシュ破棄
  if (app->font) {
    TTF_CloseFont(app->font);
    app->font = NULL;
  }
  glyph_cache_clear(app);

  app->font = f;

  TTF_SetFontKerning(app->font, 0);
  TTF_SetFontHinting(app->font, TTF_HINTING_NORMAL);

  fprintf(stderr, "Font loaded: %s (size=%d)\n", path, size);
  return 0;
}
