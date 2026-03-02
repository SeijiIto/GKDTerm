#include "session.h"
#include "term.h"

#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static void session_init(App* app, Session *s);
static void session_start_shell(Session *s);
static void session_init_vterm(Session *s);

static int cb_sb_clear(void *user);
static int cb_sb_pushline4(int cols, const VTermScreenCell *cells, bool continuation, void *user);

static const VTermScreenCallbacks screen_cb = {
  .sb_clear     = cb_sb_clear,
  .sb_pushline4 = cb_sb_pushline4,
};

int session_is_locked(const Session *s) {
  if (!s->used || s->pty_fd < 0 || s->pid <= 0) return 0;

  pid_t fg = tcgetpgrp(s->pty_fd);
  if (fg < 0) return 0;

  pid_t sh_pgrp = getpgid(s->pid);
  if (sh_pgrp < 0) return 0;

  return (fg != sh_pgrp);
}

int session_create(App* app, int idx) {
  if (idx < 0 || idx >= MAX_SESSIONS) return -1;
  Session *s = &app->sessions[idx];
  if (s->used) return 0;

  session_init(app, s);
  s->used = 1;

  session_start_shell(s);
  session_init_vterm(s);

  s->sb_head = 0;
  s->sb_count = 0;
  s->view_offset_lines = 0;
  memset(s->sb_cont, 0, sizeof(s->sb_cont));

  s->region_mode = 0;
  s->selecting = 0;

  return 0;
}

void session_destroy(App* app, int idx) {
  if (idx < 0 || idx >= MAX_SESSIONS) return;
  Session *s = &app->sessions[idx];
  if (!s->used) return;

  if (s->pid > 0) {
    kill(s->pid, SIGHUP);
    waitpid(s->pid, NULL, WNOHANG);
  }

  if (s->pty_fd >= 0) close(s->pty_fd);
  if (s->vt) vterm_free(s->vt);

  session_init(app, s);
  s->used = 0;
}

void session_switch(App* app, int idx) {
  if (idx < 0 || idx >= MAX_SESSIONS) return;
  if (!app->sessions[idx].used) session_create(app, idx);

  app->cursor_mode = 0;
  app->mod_ctrl = app->mod_alt = app->mod_meta = app->mod_shift = 0;

  app->active_sess = idx;
}

int sessions_pump_io(App* app) {
  unsigned char buf[512];
  int active_changed = 0;

  for (int i = 0; i < MAX_SESSIONS; i++) {
    Session *s = &app->sessions[i];
    if (!s->used || s->pty_fd < 0) continue;

    int n;
    while ((n = read(s->pty_fd, buf, sizeof(buf))) > 0) {
      vterm_input_write(s->vt, (const char*)buf, n);
      vterm_screen_flush_damage(s->vts);
      if (i == app->active_sess) active_changed = 1;
    }
  }
  return active_changed;
}

int sessions_alive_count(App* app) {
  int n = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) if (app->sessions[i].used) n++;
  return n;
}

int find_next_alive(App* app, int from) {
  for (int i = 1; i <= MAX_SESSIONS; i++) {
    int idx = (from + i) % MAX_SESSIONS;
    if (app->sessions[idx].used) return idx;
  }
  return -1;
}

static void session_init(App* app, Session *s) {
  memset(s, 0, sizeof(*s));
  s->app = app;
  s->pty_fd = -1;
}

static void session_start_shell(Session *s) {
  struct winsize ws = { TERM_ROWS, TERM_COLS, 0, 0 };
  pid_t pid = forkpty(&s->pty_fd, NULL, NULL, &ws);

  if (pid == 0) {
    setenv("TERM", "linux", 1);

    const char *home = getenv("HOME");
    if (home && home[0]) chdir(home);
    else { setenv("HOME", "/storage", 1); chdir("/storage"); }

    if (access("/bin/bash", X_OK) == 0) {
      execl("/bin/bash", "bash", "-l", NULL);
    } else {
      execl("/bin/sh", "sh", "-i", NULL);
    }
    _exit(1);
  }

  s->pid = pid;
  fcntl(s->pty_fd, F_SETFL, O_NONBLOCK);
}

static void session_init_vterm(Session *s) {
  s->vt = vterm_new(TERM_ROWS, TERM_COLS);
  vterm_set_utf8(s->vt, 1);

  s->vts = vterm_obtain_screen(s->vt);
  s->vts_state = vterm_obtain_state(s->vt);

  vterm_screen_set_callbacks(s->vts, &screen_cb, s);
  vterm_screen_callbacks_has_pushline4(s->vts);

  vterm_screen_set_damage_merge(s->vts, VTERM_DAMAGE_SCROLL);
  vterm_screen_reset(s->vts, 1);
}

static int cb_sb_clear(void *user) {
  Session *s = (Session*)user;
  s->sb_head = 0;
  s->sb_count = 0;
  s->view_offset_lines = 0;
  memset(s->sb_cont, 0, sizeof(s->sb_cont));
  return 1;
}

static int cb_sb_pushline4(int cols, const VTermScreenCell *cells, bool continuation, void *user) {
  Session *s = (Session*)user;

  ScrollbackCell *dst = s->sb_buf[s->sb_head];

  int maxc = cols;
  if (maxc > TERM_COLS) maxc = TERM_COLS;

  for (int c = 0; c < maxc; c++) {
    const VTermScreenCell *cell = &cells[c];
    ScrollbackCell out;

    out.width = (uint8_t)cell->width;
    
    if (cell->width == 0) {
      out.ch = 0;
    } else {
      out.ch = cell->chars[0] ? cell->chars[0] : ' ';
      if (out.ch == 0) out.ch = ' ';
    }

    out.fg = vterm_fg_to_sdl(s->app, s->vts_state, cell->fg);
    out.bg = vterm_bg_to_sdl(s->app, s->vts_state, cell->bg);
    out.reverse = cell->attrs.reverse ? 1 : 0;

    dst[c] = out;
  }

  for (int c = maxc; c < TERM_COLS; c++) {
    dst[c] = (ScrollbackCell){ .ch=' ', .fg=s->app->def_fg, .bg=s->app->def_bg, .width=1, .reverse=0 };
  }

  s->sb_cont[s->sb_head] = continuation ? 1 : 0;

  s->sb_head = (s->sb_head + 1) % SCROLLBACK_LINES;
  if (s->sb_count < SCROLLBACK_LINES) s->sb_count++;

  return 1;
}

