#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int config_load_ini(App *app, const char *cfg_path);
static int config_write_default(const char *cfg_path);
static int file_exists(const char *path);
static int mkdir_p(const char *path, mode_t mode);
static void config_set_defaults(App *app);
static void trim(char *s);

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
      if (sz >= CONFIG_FONT_SIZE_MIN && sz <= CONFIG_FONT_SIZE_MAX) app->cfg.font_size = sz;
    }
  }

  fclose(f);
  return 0;
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
