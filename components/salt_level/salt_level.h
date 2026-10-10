#pragma once

#include <array>
#include <cmath>
#include <string>

#include <esp_camera.h>

#include "esphome/core/component.h"
#include "image_analysis.h"

namespace esphome::number {
class Number;
}
namespace esphome::sensor {
class Sensor;
}
namespace esphome::text_sensor {
class TextSensor;
}

namespace esphome::salt_level {

enum class CameraState : uint8_t {
  OFF,       ///< camera powered down, XCLK stopped (the normal state)
  STARTING,  ///< a capture is running: init attempts in progress
};

/// Which reference the next measurement should become.
enum class Calibration : uint8_t { NONE, FULL, EMPTY };

/// Owns the camera (esp32-camera driver, grayscale) instead of ESPHome's esp32_camera component.
///
/// The camera is only powered up for the duration of a capture: with the 20 MHz sensor clock
/// running, WiFi on this board drops to ~90 % packet loss (measured), and the OV3660 needs a
/// second init attempt about half of the time. A capture therefore does
///   init (with retries) -> discard settling frames -> grab one grayscale frame ->
///   camera off -> publish (white share, level, JPEG of the same frame) over MQTT.
///
/// Tunables (threshold, interval, exposure, gain, calibration references) are ESPHome number
/// entities read at use time, so they can be changed at runtime and survive reboots.
class SaltLevelComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  /// After the MQTT client, PSRAM and the entities have been set up.
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_data_pins(std::array<uint8_t, 8> pins) { this->data_pins_ = pins; }
  void set_xclk_pin(int pin) { this->xclk_pin_ = pin; }
  void set_pclk_pin(int pin) { this->pclk_pin_ = pin; }
  void set_vsync_pin(int pin) { this->vsync_pin_ = pin; }
  void set_href_pin(int pin) { this->href_pin_ = pin; }
  void set_sccb_sda_pin(int pin) { this->sccb_sda_pin_ = pin; }
  void set_sccb_scl_pin(int pin) { this->sccb_scl_pin_ = pin; }
  void set_power_down_pin(int pin) { this->power_down_pin_ = pin; }
  void set_reset_pin(int pin) { this->reset_pin_ = pin; }
  void set_flash_pin(int pin) { this->flash_pin_ = pin; }
  void set_xclk_frequency(uint32_t hz) { this->xclk_frequency_ = hz; }
  void set_frame_size(framesize_t size) { this->frame_size_ = size; }
  void set_jpeg_quality(uint8_t quality) { this->jpeg_quality_ = quality; }
  void set_vertical_flip(bool flip) { this->vertical_flip_ = flip; }
  void set_horizontal_mirror(bool mirror) { this->horizontal_mirror_ = mirror; }
  void set_auto_exposure(bool enable) { this->auto_exposure_ = enable; }
  void set_exposure_value(uint16_t value) { this->exposure_value_ = value; }
  void set_auto_gain(bool enable) { this->auto_gain_ = enable; }
  void set_gain_value(uint8_t value) { this->gain_value_ = value; }
  void set_brightness(int8_t value) { this->brightness_ = value; }
  void set_contrast(int8_t value) { this->contrast_ = value; }

  void set_image_topic(const std::string &topic) { this->image_topic_ = topic; }
  void set_meta_topic(const std::string &topic) { this->meta_topic_ = topic; }
  void set_max_image_bytes(uint32_t bytes) { this->max_image_bytes_ = bytes; }
  void set_min_capture_interval_ms(uint32_t ms) { this->min_capture_interval_ms_ = ms; }
  void set_camera_init_delay_ms(uint32_t ms) { this->init_delay_ms_ = ms; }
  void set_capture_on_start(bool enable) { this->capture_on_start_ = enable; }
  void set_white_threshold(uint8_t threshold) { this->white_threshold_ = threshold; }

  // Entities (all optional). Numbers are read at use time; sensors are published after a capture.
  void set_threshold_number(number::Number *n) { this->threshold_number_ = n; }
  void set_interval_number(number::Number *n) { this->interval_number_ = n; }
  void set_exposure_number(number::Number *n) { this->exposure_number_ = n; }
  void set_gain_number(number::Number *n) { this->gain_number_ = n; }
  void set_flash_number(number::Number *n) { this->flash_number_ = n; }
  void set_full_reference_number(number::Number *n) { this->full_reference_number_ = n; }
  void set_empty_reference_number(number::Number *n) { this->empty_reference_number_ = n; }
  void set_level_sensor(sensor::Sensor *s) { this->level_sensor_ = s; }
  void set_white_share_sensor(sensor::Sensor *s) { this->white_share_sensor_ = s; }
  void set_error_text_sensor(text_sensor::TextSensor *t) { this->error_text_sensor_ = t; }

  /// Request one capture. Returns false if refused (rate limited, broker not connected);
  /// a request while one is pending is coalesced and returns true.
  bool trigger_capture();
  /// The next measurement becomes the 100 % / 0 % reference of the level scale.
  void calibrate_full() { this->request_calibration_(Calibration::FULL); }
  void calibrate_empty() { this->request_calibration_(Calibration::EMPTY); }

 protected:
  static constexpr uint32_t CAPTURE_REQUEST_TIMEOUT_MS = 90000;
  static constexpr uint32_t INIT_RETRY_PAUSE_MS = 2000;
  static constexpr uint32_t MAX_INIT_ATTEMPTS = 20;  // ~65 % of attempts fail here; 20 tries keep the miss rate tiny
  static constexpr int SETTLING_FRAMES = 4;
  static constexpr uint32_t RETRY_AFTER_FAILURE_MS = 600000;  // 10 min
  static constexpr uint8_t FAILURES_BEFORE_UNKNOWN = 3;       // then the sensors go to "unknown"
  static constexpr float DEFAULT_INTERVAL_HOURS = 12.0f;
  static constexpr float DEFAULT_FLASH_PERCENT = 50.0f;

  /// Result of one grab: the JPEG buffer (malloc'ed, owned by the caller) and the statistics.
  struct CaptureResult {
    uint8_t *jpg{nullptr};
    size_t jpg_len{0};
    GrayStats stats;
    uint16_t width{0};
    uint16_t height{0};
  };

  void step_capture_(uint32_t now);
  esp_err_t init_camera_();
  void configure_sensor_();
  bool grab_and_encode_(CaptureResult &out);
  void camera_shutdown_();
  void finish_capture_(bool ok);
  void publish_measurement_(const CaptureResult &result);
  void publish_capture_(CaptureResult &result);
  void request_calibration_(Calibration target);

  // Values from the entities, falling back to the YAML defaults when an entity is not configured.
  uint8_t threshold_() const;
  float interval_hours_() const;
  uint16_t exposure_() const;  ///< 0 = automatic exposure and gain
  uint8_t gain_() const;
  uint8_t flash_percent_() const;  ///< 0..100, 0 = flash stays off
  void flash_on_();
  void flash_off_();
  static float number_or_(const number::Number *n, float fallback);
  static void set_number_(number::Number *n, float value);
  void set_error_text_(const std::string &text);

  void publish_discovery_();
  void publish_meta_(const char *event, const char *error, size_t bytes, const GrayStats *stats, uint16_t width,
                     uint16_t height);
  void report_error_(const char *code, const char *detail, size_t bytes = 0);

  // Camera hardware / tuning (set from YAML)
  std::array<uint8_t, 8> data_pins_{};
  int xclk_pin_{-1};
  int pclk_pin_{-1};
  int vsync_pin_{-1};
  int href_pin_{-1};
  int sccb_sda_pin_{-1};
  int sccb_scl_pin_{-1};
  int power_down_pin_{-1};
  int reset_pin_{-1};
  int flash_pin_{-1};
  bool flash_ready_{false};  ///< LEDC timer/channel for the flash configured
  uint32_t xclk_frequency_{20000000};
  framesize_t frame_size_{FRAMESIZE_QVGA};
  uint8_t jpeg_quality_{50};
  bool vertical_flip_{false};
  bool horizontal_mirror_{false};
  bool auto_exposure_{true};
  uint16_t exposure_value_{300};
  bool auto_gain_{true};
  uint8_t gain_value_{0};
  int8_t brightness_{0};
  int8_t contrast_{0};

  // MQTT / behaviour
  std::string image_topic_;
  std::string meta_topic_;
  uint32_t max_image_bytes_{100000};
  uint32_t min_capture_interval_ms_{5000};
  uint32_t init_delay_ms_{2000};  ///< no capture before this uptime (boot settling)
  bool capture_on_start_{true};
  uint8_t white_threshold_{115};

  // Entities
  number::Number *threshold_number_{nullptr};
  number::Number *interval_number_{nullptr};
  number::Number *exposure_number_{nullptr};
  number::Number *gain_number_{nullptr};
  number::Number *flash_number_{nullptr};
  number::Number *full_reference_number_{nullptr};
  number::Number *empty_reference_number_{nullptr};
  sensor::Sensor *level_sensor_{nullptr};
  sensor::Sensor *white_share_sensor_{nullptr};
  text_sensor::TextSensor *error_text_sensor_{nullptr};

  // Capture state
  CameraState camera_state_{CameraState::OFF};
  uint32_t camera_state_since_{0};
  uint32_t init_attempts_{0};  ///< init attempts of the current / last capture
  bool capture_requested_{false};
  uint32_t capture_requested_at_{0};
  uint32_t last_capture_at_{0};
  bool has_captured_{false};
  bool capture_on_start_pending_{false};

  // Measurement bookkeeping
  bool has_done_{false};
  uint32_t last_done_at_{0};
  bool last_failed_{false};
  uint8_t consecutive_failures_{0};
  Calibration calibration_pending_{Calibration::NONE};
  float last_level_{NAN};
  float last_empty_reference_{0.0f};
  float last_full_reference_{100.0f};

  bool mqtt_was_connected_{false};
  uint32_t sequence_{0};
};

}  // namespace esphome::salt_level
