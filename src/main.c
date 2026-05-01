#include "app.h"
#include <stdlib.h>

int app_init(App *app);
void app_run(App *app);
void app_shutdown(App *app);

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  App *app = (App*)calloc(1, sizeof(App));
  if (!app) return 1;

  if (app_init(app) != 0) {
    free(app);
    return 1;
  }
  app_run(app);
  app_shutdown(app);

  free(app);
  return 0;
}
