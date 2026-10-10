#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::salt_level {

struct GrayStats {
  uint32_t total_pixels{0};
  uint32_t white_pixels{0};
  float mean{0.0f};           ///< mean gray value, 0..255
  float white_percent{0.0f};  ///< share of pixels >= threshold, 0..100
};

/// Statistics over a whole 8-bit grayscale image (one byte per pixel, row-major).
/// A pixel counts as "white" when its value is >= `threshold`.
/// Pure C++ without ESPHome/ESP-IDF dependencies so it can be unit-tested on the host.
/// Returns zeroed stats for a null buffer or an empty image.
GrayStats analyze_gray(const uint8_t *data, size_t width, size_t height, uint8_t threshold);

/// Maps a white share to a fill level in percent, linear between the calibrated white share of an
/// empty tank (`empty_reference`) and of a full tank (`full_reference`), clamped to 0..100.
/// Returns NaN when the references are unusable (full must exceed empty by at least 0.5 points)
/// or when `white_percent` is NaN. With the defaults empty=0 / full=100 the level equals the white share.
float compute_level(float white_percent, float empty_reference, float full_reference);

}  // namespace esphome::salt_level
