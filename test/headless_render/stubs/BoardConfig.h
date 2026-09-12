#pragma once
// Host stub for freeink-sdk's BoardConfig.h. GfxRenderer only reads the active
// profile's bezel insets (getOrientedViewableTRBL); the real header pulls in
// Arduino/gpio and is not host-compilable. Values are the X4 profile defaults,
// which are also the ones CrossPoint hardcoded before they moved into the SDK.
#include <cstdint>

namespace BoardConfig {

struct ViewableInsets {
  uint8_t top = 9;
  uint8_t right = 3;
  uint8_t bottom = 3;
  uint8_t left = 3;
};

struct BoardProfile {
  ViewableInsets viewableInsets = {};
};

inline BoardProfile ACTIVE = {};

}  // namespace BoardConfig
