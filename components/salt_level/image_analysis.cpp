#include "image_analysis.h"

#include <cmath>

namespace esphome::salt_level {

GrayStats analyze_gray(const uint8_t *data, size_t width, size_t height, uint8_t threshold) {
  GrayStats stats;
  const size_t total = width * height;
  if (data == nullptr || total == 0) {
    return stats;
  }

  uint32_t white = 0;
  uint64_t sum = 0;
  for (size_t i = 0; i < total; i++) {
    const uint8_t value = data[i];
    sum += value;
    if (value >= threshold) {
      white++;
    }
  }

  stats.total_pixels = static_cast<uint32_t>(total);
  stats.white_pixels = white;
  stats.mean = static_cast<float>(sum) / static_cast<float>(total);
  stats.white_percent = static_cast<float>(white) * 100.0f / static_cast<float>(total);
  return stats;
}

float compute_level(float white_percent, float empty_reference, float full_reference) {
  if (std::isnan(white_percent) || std::isnan(empty_reference) || std::isnan(full_reference)) {
    return NAN;
  }
  const float span = full_reference - empty_reference;
  if (!(span >= 0.5f)) {
    return NAN;
  }
  const float level = (white_percent - empty_reference) / span * 100.0f;
  return std::fmin(100.0f, std::fmax(0.0f, level));
}

}  // namespace esphome::salt_level
