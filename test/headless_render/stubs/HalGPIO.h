#pragma once

// Host stub for lib/hal/HalGPIO.h. GfxRenderer includes the header but the
// layout/render path never touches input; an empty shell satisfies the include.

class HalGPIO {
 public:
  enum {
    BTN_UP = 0,
    BTN_DOWN = 1,
    BTN_A = 2,
    BTN_B = 3,
    BTN_C = 4,
    BTN_D = 5,
  };
};
