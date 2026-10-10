// Host-side tests for the pure image logic. No framework: plain asserts, exit code != 0 on failure.
//   scripts/test_host.sh
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "image_analysis.h"

using esphome::salt_level::analyze_gray;
using esphome::salt_level::compute_level;
using esphome::salt_level::GrayStats;

static int failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

static void test_analyze_uniform() {
  std::vector<uint8_t> img(320 * 240, 150);
  GrayStats s = analyze_gray(img.data(), 320, 240, 115);
  CHECK(s.total_pixels == 320u * 240u);
  CHECK(s.white_pixels == 320u * 240u);
  CHECK(near(s.white_percent, 100.0f));
  CHECK(near(s.mean, 150.0f));

  GrayStats t = analyze_gray(img.data(), 320, 240, 151);
  CHECK(t.white_pixels == 0u);
  CHECK(near(t.white_percent, 0.0f));
}

static void test_analyze_threshold_is_inclusive() {
  const uint8_t px[4] = {114, 115, 116, 0};
  GrayStats s = analyze_gray(px, 2, 2, 115);
  CHECK(s.white_pixels == 2u);  // 115 and 116 count, 114 and 0 do not
  CHECK(near(s.white_percent, 50.0f));
}

static void test_analyze_half_split() {
  std::vector<uint8_t> img(100 * 100);
  for (size_t i = 0; i < img.size(); i++) img[i] = (i < img.size() / 4) ? 200 : 50;  // 25 % bright
  GrayStats s = analyze_gray(img.data(), 100, 100, 115);
  CHECK(near(s.white_percent, 25.0f));
  CHECK(near(s.mean, 0.25f * 200 + 0.75f * 50));
}

static void test_analyze_degenerate_input() {
  GrayStats a = analyze_gray(nullptr, 320, 240, 100);
  CHECK(a.total_pixels == 0u);
  CHECK(near(a.white_percent, 0.0f));
  const uint8_t px[1] = {255};
  GrayStats b = analyze_gray(px, 0, 10, 100);
  CHECK(b.total_pixels == 0u);
}

static void test_level_linear_between_references() {
  CHECK(near(compute_level(35.0f, 0.0f, 70.0f), 50.0f));
  CHECK(near(compute_level(70.0f, 0.0f, 70.0f), 100.0f));
  CHECK(near(compute_level(0.0f, 0.0f, 70.0f), 0.0f));
  CHECK(near(compute_level(45.0f, 20.0f, 70.0f), 50.0f));  // empty reference above zero
}

static void test_level_is_clamped() {
  CHECK(near(compute_level(90.0f, 0.0f, 70.0f), 100.0f));
  CHECK(near(compute_level(5.0f, 20.0f, 70.0f), 0.0f));
}

static void test_level_uncalibrated_defaults_pass_white_share_through() {
  // Defaults empty=0, full=100: the level equals the white share.
  CHECK(near(compute_level(42.5f, 0.0f, 100.0f), 42.5f));
}

static void test_level_rejects_unusable_references() {
  CHECK(std::isnan(compute_level(50.0f, 70.0f, 70.0f)));   // full == empty
  CHECK(std::isnan(compute_level(50.0f, 80.0f, 70.0f)));   // full < empty
  CHECK(std::isnan(compute_level(50.0f, 0.0f, 0.3f)));     // spread below 0.5 pp
  CHECK(std::isnan(compute_level(NAN, 0.0f, 70.0f)));      // no measurement
}

int main() {
  test_analyze_uniform();
  test_analyze_threshold_is_inclusive();
  test_analyze_half_split();
  test_analyze_degenerate_input();
  test_level_linear_between_references();
  test_level_is_clamped();
  test_level_uncalibrated_defaults_pass_white_share_through();
  test_level_rejects_unusable_references();
  if (failures == 0) {
    std::printf("all host tests passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", failures);
  return 1;
}
