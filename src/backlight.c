#include "backlight.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int backlight_set(App *app, int brightness);
static int read_int_file(const char *path, int *out);
static int write_int_file(const char *path, int v);

int backlight_init(App *app) {
  app->backlight.backlight_ok = 0;
  snprintf(app->backlight.backlight_dir, sizeof(app->backlight.backlight_dir), "%s", "/sys/class/backlight/backlight");

  char pmax[512], pcur[512];
  snprintf(pmax, sizeof(pmax), "%s/max_brightness", app->backlight.backlight_dir);
  snprintf(pcur, sizeof(pcur), "%s/brightness", app->backlight.backlight_dir);

  if (read_int_file(pmax, &app->backlight.backlight_max) != 0) {
    fprintf(stderr, "backlight: cannot read %s\n", pmax);
    return -1;
  }

  int cur = 0;
  if (read_int_file(pcur, &cur) != 0) {
    fprintf(stderr, "backlight: cannot read %s\n", pcur);
    return -1;
  }

  app->backlight.backlight_saved = cur;
  app->backlight.backlight_ok = 1;
  fprintf(stderr, "backlight: ok dir=%s max=%d cur=%d\n", app->backlight.backlight_dir, app->backlight.backlight_max, cur);
  return 0;
}

int backlight_off(App *app) {
  if (!app->backlight.backlight_ok) return -1;

  char pcur[512];
  snprintf(pcur, sizeof(pcur), "%s/brightness", app->backlight.backlight_dir);

  int cur = 0;
  if (read_int_file(pcur, &cur) == 0) {
    // 0 なら、最低でも 1 は保存しておく（復帰不能防止）
    app->backlight.backlight_saved = (cur > 0) ? cur : (app->backlight.backlight_max / 2);
  } else {
    app->backlight.backlight_saved = (app->backlight.backlight_max / 2);
  }
  return backlight_set(app, 0);
}

int backlight_restore(App *app) {
  if (!app->backlight.backlight_ok) return -1;
  int v = app->backlight.backlight_saved;
  if (v <= 0) v = app->backlight.backlight_max / 2;
  return backlight_set(app, v);
}

static int backlight_set(App *app, int brightness) {
  if (!app->backlight.backlight_ok) return -1;
  if (brightness < 0) brightness = 0;
  if (brightness > app->backlight.backlight_max) brightness = app->backlight.backlight_max;

  char pcur[512];
  snprintf(pcur, sizeof(pcur), "%s/brightness", app->backlight.backlight_dir);
  if (write_int_file(pcur, brightness) != 0) {
    fprintf(stderr, "backlight: write failed %s (%s)\n", pcur, strerror(errno));
    return -1;
  }
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
