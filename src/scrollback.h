#pragma once

#include "app.h"

int sb_clampi(int v, int lo, int hi);
void sb_region_ensure_visible(App* app);
void sb_region_enter(App* app);
void sb_region_exit(App* app);
void sb_region_line_hl_range(App *app, int vline, int *from, int *to);
int sb_phys_index(App *app, int i);
int sb_virtual_start_line(App *app);
int sb_virtual_total_lines(App* app);
