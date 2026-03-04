#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void trim(char *s);
static int mkdir_p(const char *path, mode_t mode);
static int file_exists(const char *path);
static void config_set_defaults(App *app);
static int config_write_default(const char *cfg_path);
static int config_load_ini(App *app, const char *cfg_path);
static int backlight_set(App *app, int brightness);

int get_battery_level(void) {
  int capacity = 0;
  FILE *f = fopen("/sys/class/power_supply/battery/capacity", "r");
  if (f) { fscanf(f, "%d", &capacity); fclose(f); }
  return capacity;
}

void save_screenshot(SDL_Renderer *renderer) {
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

int config_load_or_create(App *app, const char *appname_dir) {
  config_set_defaults(app);

  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");

  char base[512];
  if (xdg && xdg[0]) {
    snprintf(base, sizeof(base), "%s", xdg);
  } else if (home && home[0]) {
    snprintf(base, sizeof(base), "%s/.config", home);
  } else {
    // 組み込み環境向け最終フォールバック
    snprintf(base, sizeof(base), "/storage/.config");
  }

  char dir[512];
  snprintf(dir, sizeof(dir), "%s/%s", base, appname_dir);

  if (mkdir_p(dir, 0755) != 0) {
    fprintf(stderr, "mkdir_p failed: %s\n", dir);
    return -1;
  }

  char cfg_path[512];
  snprintf(cfg_path, sizeof(cfg_path), "%s/config.ini", dir);

  if (!file_exists(cfg_path)) {
    if (config_write_default(cfg_path) != 0) {
      fprintf(stderr, "config write failed: %s\n", cfg_path);
      return -1;
    }
  }

  // 読み込み（デフォルト生成直後でも読み込む）
  if (config_load_ini(app, cfg_path) != 0) {
    fprintf(stderr, "config read failed: %s\n", cfg_path);
    return -1;
  }

  fprintf(stderr, "Config: %s\n", cfg_path);
  fprintf(stderr, " font_path='%s'\n", app->cfg.font_path);
  fprintf(stderr, " font_size=%d\n", app->cfg.font_size);
  return 0;
}

static int read_int_file(const char *path, int *out) {
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  int v = 0;
  int ok = fscanf(f, "%d", &v);
  fclose(f);
  if (ok != 1) return -1;
  *out = v;
  return 0;
}

static int write_int_file(const char *path, int v) {
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "%d\n", v);
  fclose(f);
  return 0;
}

int backlight_init(App *app) {
  app->backlight_ok = 0;
  snprintf(app->backlight_dir, sizeof(app->backlight_dir), "%s", "/sys/class/backlight/backlight");

  char pmax[512], pcur[512];
  snprintf(pmax, sizeof(pmax), "%s/max_brightness", app->backlight_dir);
  snprintf(pcur, sizeof(pcur), "%s/brightness", app->backlight_dir);

  if (read_int_file(pmax, &app->backlight_max) != 0) {
    fprintf(stderr, "backlight: cannot read %s\n", pmax);
    return -1;
  }

  int cur = 0;
  if (read_int_file(pcur, &cur) != 0) {
    fprintf(stderr, "backlight: cannot read %s\n", pcur);
    return -1;
  }

  app->backlight_saved = cur;
  app->backlight_ok = 1;
  fprintf(stderr, "backlight: ok dir=%s max=%d cur=%d\n", app->backlight_dir, app->backlight_max, cur);
  return 0;
}

int backlight_off(App *app) {
  if (!app->backlight_ok) return -1;

  char pcur[512];
  snprintf(pcur, sizeof(pcur), "%s/brightness", app->backlight_dir);

  int cur = 0;
  if (read_int_file(pcur, &cur) == 0) {
    // 0 なら、最低でも 1 は保存しておく（復帰不能防止）
    app->backlight_saved = (cur > 0) ? cur : (app->backlight_max / 2);
  } else {
    app->backlight_saved = (app->backlight_max / 2);
  }
  return backlight_set(app, 0);
}

int backlight_restore(App *app) {
  if (!app->backlight_ok) return -1;
  int v = app->backlight_saved;
  if (v <= 0) v = app->backlight_max / 2;
  return backlight_set(app, v);
}

static void trim(char *s) {
  // 前後の空白を除去（簡易）
  char *p = s;
  while (*p && isspace((unsigned char)*p)) p++;
  if (p != s) memmove(s, p, strlen(p) + 1);

  size_t n = strlen(s);
  while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static int mkdir_p(const char *path, mode_t mode) {
  char tmp[512];
  if (!path || !path[0]) return -1;
  if (strlen(path) >= sizeof(tmp)) return -1;

  strcpy(tmp, path);

  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
  return 0;
}

static int file_exists(const char *path) {
  return access(path, F_OK) == 0;
}

static void config_set_defaults(App *app) {
  app->cfg.font_path[0] = '\0'; // 未指定
  app->cfg.font_size = 18;      // デフォルト
}

static int config_write_default(const char *cfg_path) {
  FILE *f = fopen(cfg_path, "w");
  if (!f) return -1;

  fprintf(f,
    "# gkd_term config\n"
    "# font_path: absolute path recommended. Empty => system fallback.\n"
    "font_path=\n"
    "font_size=18\n"
  );

  fclose(f);
  return 0;
}

static int config_load_ini(App *app, const char *cfg_path) {
  FILE *f = fopen(cfg_path, "r");
  if (!f) return -1;

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    // コメント/空行
    char *p = line;
    trim(p);
    if (!p[0] || p[0] == '#' || p[0] == ';') continue;

    char *eq = strchr(p, '=');
    if (!eq) continue;

    *eq = '\0';
    char *key = p;
    char *val = eq + 1;
    trim(key);
    trim(val);

    if (strcmp(key, "font_path") == 0) {
      strncpy(app->cfg.font_path, val, sizeof(app->cfg.font_path) - 1);
      app->cfg.font_path[sizeof(app->cfg.font_path) - 1] = '\0';
    } else if (strcmp(key, "font_size") == 0) {
      int sz = atoi(val);
      if (sz >= 6 && sz <= 96) app->cfg.font_size = sz;
    }
  }

  fclose(f);
  return 0;
}

static int backlight_set(App *app, int brightness) {
  if (!app->backlight_ok) return -1;
  if (brightness < 0) brightness = 0;
  if (brightness > app->backlight_max) brightness = app->backlight_max;

  char pcur[512];
  snprintf(pcur, sizeof(pcur), "%s/brightness", app->backlight_dir);
  if (write_int_file(pcur, brightness) != 0) {
    fprintf(stderr, "backlight: write failed %s (%s)\n", pcur, strerror(errno));
    return -1;
  }
  return 0;
}

