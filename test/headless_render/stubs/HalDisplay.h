#pragma once

// Host stub for lib/hal/HalDisplay.h. A malloc'd 48KB framebuffer with the
// X4 panel geometry and no-op refresh methods — GfxRenderer draws into the
// buffer exactly as on device; the harness dumps it to an image instead of
// driving a panel. Grayscale paths report unsupported so the renderer stays
// on the plain BW path.

#include <cstdint>
#include <cstdlib>
#include <cstring>

class HalDisplay {
 public:
  HalDisplay() {
    frameBuffer_ = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
    if (frameBuffer_) memset(frameBuffer_, 0xFF, BUFFER_SIZE);
  }
  ~HalDisplay() { free(frameBuffer_); }
  HalDisplay(const HalDisplay&) = delete;
  HalDisplay& operator=(const HalDisplay&) = delete;

  enum RefreshMode {
    FULL_REFRESH,
    HALF_REFRESH,
    FAST_REFRESH,
  };

  void begin(bool = false) {}

  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  void clearScreen(uint8_t color = 0xFF) const {
    if (frameBuffer_) memset(frameBuffer_, color, BUFFER_SIZE);
  }
  void drawImage(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) const {}
  void drawImageTransparent(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) const {}

  void displayBuffer(RefreshMode = FAST_REFRESH, bool = false) {}
  void displayBufferAsync(RefreshMode = FAST_REFRESH) {}
  void waitRefreshComplete() {}
  bool supportsAsyncRefresh() const { return false; }
  void refreshDisplay(RefreshMode = FAST_REFRESH, bool = false) {}

  void deepSleep() {}

  uint8_t* getFrameBuffer() const { return frameBuffer_; }

  // Same contract as device: storage is loaned out, comes back white.
  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut) {
    if (lent_ || !frameBuffer_) return nullptr;
    lent_ = true;
    if (sizeOut) *sizeOut = BUFFER_SIZE;
    return frameBuffer_;
  }
  void returnFrameBufferStorage() {
    if (!lent_) return;
    lent_ = false;
    if (frameBuffer_) memset(frameBuffer_, 0xFF, BUFFER_SIZE);
  }

  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
  void displayGrayscaleBase(RefreshMode = HALF_REFRESH, bool = false) {}
  void copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t*) {}
  void displayGrayBuffer(bool = false) {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
  bool supportsStripGrayscale() const { return false; }

  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }

 private:
  uint8_t* frameBuffer_ = nullptr;
  bool lent_ = false;
};

// Same global the firmware defines in main.cpp; cover converters size
// against it. Defined by the harness executable.
extern HalDisplay display;
