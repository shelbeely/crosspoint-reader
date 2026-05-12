#pragma once
#include <Arduino.h>
#include <EInkDisplay.h>

/**
 * @brief HAL wrapper for the Xteink X4 e-ink display.
 *
 * Owns the single 48 KB (800×480 / 8 bytes) framebuffer and provides the interface
 * to push it to the physical panel via SPI.
 *
 * Display pipeline:
 *   GfxRenderer (draw calls) → HalDisplay::displayBuffer() → EInkDisplay (open-x4-sdk) → SPI → panel
 *
 * Refresh modes:
 *   FAST_REFRESH  — custom LUT, ~300 ms, used for normal page turns
 *   HALF_REFRESH  — balanced quality/speed, ~1720 ms
 *   FULL_REFRESH  — full waveform, ~3 s, used to remove deep ghosting
 *
 * Single-buffer constraint: EINK_DISPLAY_SINGLE_BUFFER_MODE=1 is mandatory because the ESP32-C3 has
 * only ~380 KB SRAM. A second 48 KB buffer would waste ~25 % of available RAM.
 *
 * Global singleton: extern HalDisplay display;
 * Do not use EInkDisplay directly in application code — always go through HalDisplay.
 */
class HalDisplay {
 public:
  // Constructor with pin configuration
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Initialize the display hardware and driver
  void begin();

  // Display dimensions
  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool turnOffScreen = false);

  // Runtime geometry passthrough
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
  EInkDisplay einkDisplay;
};

extern HalDisplay display;
