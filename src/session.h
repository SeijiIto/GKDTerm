#pragma once
#include "app.h"

int session_is_locked(const Session *s);
int session_create(App *app, int idx);
void session_destroy(App *app, int idx);
void session_switch(App *app, int idx);
int sessions_pump_io(App *app);
int sessions_alive_count(App *app);
int find_next_alive(App *app, int from);
