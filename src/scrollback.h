#pragma once

#include "app.h"

int clampi(int v, int lo, int hi);
void region_copy_selection_stream(App* app);
void region_ensure_visible(App* app);
void region_enter(App* app);
void region_exit(App* app);
void region_line_hl_range(App *app, int vline, int *from, int *to);
int sb_phys_index(App *app, int i);
int virtual_start_line(App *app);
int virtual_total_lines(App* app);
void paste_from_buffers(App* app);
