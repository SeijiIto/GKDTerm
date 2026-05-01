#include "battery.h"

#include <stdio.h>

int battery_get_level(void) {
  int capacity = 0;
  FILE *f = fopen("/sys/class/power_supply/battery/capacity", "r");
  if (f) { fscanf(f, "%d", &capacity); fclose(f); }
  return capacity;
}
