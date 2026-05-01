#include "scrollback.h"

int sb_clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void sb_region_ensure_visible(App* app) {
  int start = sb_virtual_start_line(app);
  int end   = start + (TERM_ROWS - 1);

  if (SESSION(app)->reg_line < start) {
    int delta = start - SESSION(app)->reg_line;
    SESSION(app)->view_offset_lines += delta;
    if (SESSION(app)->view_offset_lines > SESSION(app)->sb_count) SESSION(app)->view_offset_lines = SESSION(app)->sb_count;
  } else if (SESSION(app)->reg_line > end) {
    int delta = SESSION(app)->reg_line - end;
    SESSION(app)->view_offset_lines -= delta;
    if (SESSION(app)->view_offset_lines < 0) SESSION(app)->view_offset_lines = 0;
  }
}

void sb_region_enter(App* app) {
  SESSION(app)->region_mode = 1;
  SESSION(app)->selecting = 0;

  int start = sb_virtual_start_line(app);
  int v = start + (TERM_ROWS - 1);
  int total = sb_virtual_total_lines(app);
  if (total > 0) v = sb_clampi(v, 0, total - 1);

  SESSION(app)->reg_line = v;
  SESSION(app)->reg_col = 0;
}

void sb_region_exit(App* app) {
  SESSION(app)->region_mode = 0;
  SESSION(app)->selecting = 0;
}

void sb_region_line_hl_range(App* app, int vline, int *from, int *to) {
  *from = 1;
  *to = 0;

  if (!SESSION(app)->region_mode || !SESSION(app)->selecting) return;

  int l1 = SESSION(app)->sel_line;
  int c1 = SESSION(app)->sel_col;
  int l2 = SESSION(app)->reg_line;
  int c2 = SESSION(app)->reg_col;

  // 正規化
  if (l1 > l2 || (l1 == l2 && c1 > c2)) {
    int t;
    t = l1; l1 = l2; l2 = t;
    t = c1; c1 = c2; c2 = t;
  }

  if (vline < l1 || vline > l2) return;

  if (l1 == l2) { *from = c1; *to = c2; return; }
  if (vline == l1) { *from = c1; *to = TERM_COLS - 1; return; }
  if (vline == l2) { *from = 0;  *to = c2; return; }

  *from = 0;
  *to = TERM_COLS - 1;
}

int sb_phys_index(App* app, int i) {
  int sb_head = SESSION(app)->sb_head;
  int sb_count = SESSION(app)->sb_count;
  int base = sb_head - sb_count;
  while (base < 0) base += SCROLLBACK_LINES;
  return (base + i) % SCROLLBACK_LINES;
}

int sb_virtual_start_line(App* app) {
  if (SESSION(app)->view_offset_lines < 0) SESSION(app)->view_offset_lines = 0;
  if (SESSION(app)->view_offset_lines > SESSION(app)->sb_count) SESSION(app)->view_offset_lines = SESSION(app)->sb_count;
  return SESSION(app)->sb_count - SESSION(app)->view_offset_lines;
}

int sb_virtual_total_lines(App* app) {
  return SESSION(app)->sb_count + TERM_ROWS;
}
