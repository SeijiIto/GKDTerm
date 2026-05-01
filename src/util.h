#pragma once

#include "app.h"

static inline int util_is_dpad(int b) {
  return (b >= BTN_UP && b <= BTN_RIGHT);
}
