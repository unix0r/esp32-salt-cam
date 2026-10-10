#include "salt_level.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <driver/ledc.h>
#include <esp_heap_caps.h>
#include <img_converters.h>

#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::salt_level {

static const char *const TAG = "salt_level";

void SaltLevelComponent::setup() { this->capture_on_start_pending_ = this->capture_on_start_; }

void SaltLevelComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Salt level:\n"
                "  Image topic: %s\n"
                "  Meta topic: %s\n"
                "  Max image bytes: %u\n"
                "  Min capture interval: %u ms\n"
                "  Boot settling delay: %u ms\n"
                "  Capture on start: %s\n"
                "  Default white threshold: %u\n"
                "  JPEG quality: %u\n"
                "  Entities: threshold=%s interval=%s exposure=%s gain=%s refs=%s/%s level=%s share=%s error=%s",
                this->image_topic_.c_str(), this->meta_topic_.c_str(), (unsigned) this->max_image_bytes_,
                (unsigned) this->min_capture_interval_ms_, (unsigned) this->init_delay_ms_,
                YESNO(this->capture_on_start_), (unsigned) this->white_threshold_, (unsigned) this->jpeg_quality_,
                YESNO(this->threshold_number_ != nullptr), YESNO(this->interval_number_ != nullptr),
                YESNO(this->exposure_number_ != nullptr), YESNO(this->gain_number_ != nullptr),
                YESNO(this->full_reference_number_ != nullptr), YESNO(this->empty_reference_number_ != nullptr),
                YESNO(this->level_sensor_ != nullptr), YESNO(this->white_share_sensor_ != nullptr),
                YESNO(this->error_text_sensor_ != nullptr));
  ESP_LOGCONFIG(TAG,
                "  Camera pins: D0-D7 %u,%u,%u,%u,%u,%u,%u,%u XCLK %d PCLK %d VSYNC %d HREF %d\n"
                "               SCCB SDA %d SCL %d PWDN %d RESET %d, XCLK %u Hz",
                this->data_pins_[0], this->data_pins_[1], this->data_pins_[2], this->data_pins_[3],
                this->data_pins_[4], this->data_pins_[5], this->data_pins_[6], this->data_pins_[7], this->xclk_pin_,
                this->pclk_pin_, this->vsync_pin_, this->href_pin_, this->sccb_sda_pin_, this->sccb_scl_pin_,
                this->power_down_pin_, this->reset_pin_, (unsigned) this->xclk_frequency_);
}

// ---------------------------------------------------------------------------------------------
//  Values from entities
// ---------------------------------------------------------------------------------------------

float SaltLevelComponent::number_or_(const number::Number *n, float fallback) {
  return (n != nullptr && !std::isnan(n->state)) ? n->state : fallback;
}

void SaltLevelComponent::set_number_(number::Number *n, float value) {
  if (n == nullptr) {
    return;
  }
  auto call = n->make_call();
  call.set_value(std::round(value * 10.0f) / 10.0f);
  call.perform();
}

uint8_t SaltLevelComponent::threshold_() const {
  const float v = number_or_(this->threshold_number_, static_cast<float>(this->white_threshold_));
  return static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, std::round(v))));
}

float SaltLevelComponent::interval_hours_() const {
  return std::min(168.0f, std::max(0.25f, number_or_(this->interval_number_, DEFAULT_INTERVAL_HOURS)));
}

uint16_t SaltLevelComponent::exposure_() const {
  const float fallback = this->auto_exposure_ ? 0.0f : static_cast<float>(this->exposure_value_);
  return static_cast<uint16_t>(std::min(1200.0f, std::max(0.0f, std::round(number_or_(this->exposure_number_, fallback)))));
}

uint8_t SaltLevelComponent::gain_() const {
  return static_cast<uint8_t>(
      std::min(30.0f, std::max(0.0f, std::round(number_or_(this->gain_number_, static_cast<float>(this->gain_value_))))));
}

uint8_t SaltLevelComponent::flash_percent_() const {
  if (this->flash_pin_ < 0) {
    return 0;
  }
  return static_cast<uint8_t>(
      std::min(100.0f, std::max(0.0f, std::round(number_or_(this->flash_number_, DEFAULT_FLASH_PERCENT)))));
}

// The camera driver owns LEDC timer 0 / channel 0 (sensor clock); the flash uses timer 1 / channel 1.
static constexpr ledc_mode_t FLASH_LEDC_MODE = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t FLASH_LEDC_TIMER = LEDC_TIMER_1;
static constexpr ledc_channel_t FLASH_LEDC_CHANNEL = LEDC_CHANNEL_1;

void SaltLevelComponent::flash_on_() {
  const uint8_t percent = this->flash_percent_();
  if (percent == 0) {
    return;
  }
  if (!this->flash_ready_) {
    ledc_timer_config_t timer = {};
    timer.speed_mode = FLASH_LEDC_MODE;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.timer_num = FLASH_LEDC_TIMER;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_channel_config_t channel = {};
    channel.gpio_num = this->flash_pin_;
    channel.speed_mode = FLASH_LEDC_MODE;
    channel.channel = FLASH_LEDC_CHANNEL;
    channel.timer_sel = FLASH_LEDC_TIMER;
    channel.duty = 0;
    channel.hpoint = 0;
    if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&channel) != ESP_OK) {
      ESP_LOGW(TAG, "Flash LED PWM setup failed, capturing without flash");
      return;
    }
    this->flash_ready_ = true;
  }
  ledc_set_duty(FLASH_LEDC_MODE, FLASH_LEDC_CHANNEL, static_cast<uint32_t>(percent) * 255 / 100);
  ledc_update_duty(FLASH_LEDC_MODE, FLASH_LEDC_CHANNEL);
  ESP_LOGD(TAG, "Flash on (%u %%)", (unsigned) percent);
}

void SaltLevelComponent::flash_off_() {
  if (!this->flash_ready_) {
    return;
  }
  ledc_set_duty(FLASH_LEDC_MODE, FLASH_LEDC_CHANNEL, 0);
  ledc_update_duty(FLASH_LEDC_MODE, FLASH_LEDC_CHANNEL);
}

void SaltLevelComponent::set_error_text_(const std::string &text) {
  if (this->error_text_sensor_ != nullptr) {
    this->error_text_sensor_->publish_state(text);
  }
}

// ---------------------------------------------------------------------------------------------
//  Main loop: discovery, start-up capture, interval timer, running capture
// ---------------------------------------------------------------------------------------------

void SaltLevelComponent::loop() {
  const uint32_t now = millis();

  // (Re)publish the HA camera discovery on every MQTT (re)connect.
  auto *client = mqtt::global_mqtt_client;
  const bool connected = client != nullptr && client->is_connected();
  if (connected && !this->mqtt_was_connected_) {
    this->publish_discovery_();
  }
  this->mqtt_was_connected_ = connected;

  if (!this->capture_requested_ && connected && now >= this->init_delay_ms_) {
    if (this->capture_on_start_pending_) {
      // First capture once the network is up, so the camera clock never runs while WiFi/MQTT come up.
      this->capture_on_start_pending_ = false;
      this->trigger_capture();
    } else if (this->has_done_) {
      // Interval timer. The interval entity can change at any time; elapsed time is re-checked
      // against it on every loop. After a failed capture retry sooner than a whole interval.
      const uint32_t interval_ms = static_cast<uint32_t>(this->interval_hours_() * 3600000.0f);
      const uint32_t required = this->last_failed_ ? std::min(interval_ms, RETRY_AFTER_FAILURE_MS) : interval_ms;
      if (now - this->last_done_at_ >= required) {
        this->trigger_capture();
      }
    }
  }

  if (this->capture_requested_) {
    // Fresh timestamp: trigger_capture() above may have stamped the request after `now` was read.
    this->step_capture_(millis());
  }
}

bool SaltLevelComponent::trigger_capture() {
  if (this->capture_requested_) {
    ESP_LOGD(TAG, "Capture already pending, coalescing request");
    return true;
  }
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) {
    ESP_LOGW(TAG, "Capture refused: broker not connected");
    return false;
  }
  const uint32_t now = millis();
  if (this->has_captured_ && now - this->last_capture_at_ < this->min_capture_interval_ms_) {
    ESP_LOGW(TAG, "Capture rate limited (min interval %u ms)", (unsigned) this->min_capture_interval_ms_);
    return false;
  }
  this->has_captured_ = true;
  this->last_capture_at_ = now;
  this->capture_requested_ = true;
  this->capture_requested_at_ = now;
  this->camera_state_ = CameraState::OFF;
  this->init_attempts_ = 0;
  return true;
}

void SaltLevelComponent::request_calibration_(Calibration target) {
  const char *name = target == Calibration::FULL ? "full" : "empty";
  this->calibration_pending_ = target;
  ESP_LOGI(TAG, "Calibration '%s' requested: the next measurement becomes the reference", name);
  if (!this->trigger_capture()) {
    this->calibration_pending_ = Calibration::NONE;
    this->set_error_text_("CALIBRATION_REFUSED: try again in a few seconds");
  }
}

// ---------------------------------------------------------------------------------------------
//  One capture: camera on (init with retries) -> grab -> camera off -> publish
// ---------------------------------------------------------------------------------------------

void SaltLevelComponent::step_capture_(uint32_t now) {
  // Signed difference so a timestamp that is a few ms ahead of `now` cannot wrap into a huge value.
  if (static_cast<int32_t>(now - this->capture_requested_at_) > static_cast<int32_t>(CAPTURE_REQUEST_TIMEOUT_MS)) {
    this->camera_shutdown_();
    this->report_error_("CAPTURE_TIMEOUT", "capture did not finish within 90 s");
    this->finish_capture_(false);
    return;
  }
  if (now < this->init_delay_ms_) {
    return;  // boot settling
  }
  if (this->camera_state_ == CameraState::OFF) {
    this->camera_state_ = CameraState::STARTING;
    this->camera_state_since_ = now - INIT_RETRY_PAUSE_MS;  // first attempt right away
  }
  if (now - this->camera_state_since_ < INIT_RETRY_PAUSE_MS) {
    return;
  }

  ++this->init_attempts_;
  ESP_LOGI(TAG, "Camera init attempt %u", (unsigned) this->init_attempts_);
  const esp_err_t err = this->init_camera_();
  if (err != ESP_OK) {
    esp_camera_deinit();  // release the driver and stop the clock before the next attempt
    this->camera_state_since_ = millis();
    ESP_LOGW(TAG, "Camera init failed (%s)", esp_err_to_name(err));
    if (this->init_attempts_ >= MAX_INIT_ATTEMPTS) {
      this->camera_shutdown_();
      this->report_error_("CAMERA_INIT_FAILED", "camera init failed after all attempts");
      this->finish_capture_(false);
    }
    return;
  }

  this->configure_sensor_();
  this->flash_on_();  // before the settling frames, so auto exposure (if used) settles under the flash
  CaptureResult result;
  const bool grabbed = this->grab_and_encode_(result);
  this->camera_shutdown_();  // flash and clock off before any network work
  if (!grabbed) {
    this->finish_capture_(false);
    return;
  }
  this->finish_capture_(true);
  this->publish_measurement_(result);
  this->publish_capture_(result);
}

void SaltLevelComponent::finish_capture_(bool ok) {
  this->capture_requested_ = false;
  this->has_done_ = true;
  this->last_done_at_ = millis();
  this->last_failed_ = !ok;
  if (ok) {
    this->consecutive_failures_ = 0;
    this->set_error_text_("OK");
    return;
  }
  this->calibration_pending_ = Calibration::NONE;
  if (this->consecutive_failures_ < 255) {
    this->consecutive_failures_++;
  }
  if (this->consecutive_failures_ == FAILURES_BEFORE_UNKNOWN) {
    // Stale numbers are worse than no numbers: after repeated failures the sensors go to "unknown".
    ESP_LOGW(TAG, "%u measurements failed in a row, publishing 'unknown'", (unsigned) this->consecutive_failures_);
    if (this->level_sensor_ != nullptr) {
      this->level_sensor_->publish_state(NAN);
    }
    if (this->white_share_sensor_ != nullptr) {
      this->white_share_sensor_->publish_state(NAN);
    }
  }
}

esp_err_t SaltLevelComponent::init_camera_() {
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_d0 = this->data_pins_[0];
  cfg.pin_d1 = this->data_pins_[1];
  cfg.pin_d2 = this->data_pins_[2];
  cfg.pin_d3 = this->data_pins_[3];
  cfg.pin_d4 = this->data_pins_[4];
  cfg.pin_d5 = this->data_pins_[5];
  cfg.pin_d6 = this->data_pins_[6];
  cfg.pin_d7 = this->data_pins_[7];
  cfg.pin_xclk = this->xclk_pin_;
  cfg.pin_pclk = this->pclk_pin_;
  cfg.pin_vsync = this->vsync_pin_;
  cfg.pin_href = this->href_pin_;
  cfg.pin_sccb_sda = this->sccb_sda_pin_;
  cfg.pin_sccb_scl = this->sccb_scl_pin_;
  cfg.pin_pwdn = this->power_down_pin_;
  cfg.pin_reset = this->reset_pin_;
  cfg.xclk_freq_hz = this->xclk_frequency_;
  cfg.pixel_format = PIXFORMAT_GRAYSCALE;
  cfg.frame_size = this->frame_size_;
  cfg.jpeg_quality = 12;  // unused for grayscale
  cfg.fb_count = 1;
  cfg.fb_location = CAMERA_FB_IN_PSRAM;
  cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  return esp_camera_init(&cfg);
}

void SaltLevelComponent::configure_sensor_() {
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) {
    ESP_LOGW(TAG, "Camera sensor handle not available, keeping driver defaults");
    return;
  }
  // Exposure 0 = automatic exposure and gain. Any other value fixes both, which keeps the white
  // threshold meaningful: with auto exposure a darker scene (less salt) is brightened again.
  const uint16_t exposure = this->exposure_();
  const bool manual = exposure > 0;
  const uint8_t gain = this->gain_();
  ESP_LOGI(TAG, "Camera ready after %u attempt(s), sensor PID=0x%x, exposure %s, gain %u",
           (unsigned) this->init_attempts_, (unsigned) s->id.PID, manual ? "manual" : "auto", (unsigned) gain);
  s->set_vflip(s, this->vertical_flip_);
  s->set_hmirror(s, this->horizontal_mirror_);
  s->set_brightness(s, this->brightness_);
  s->set_contrast(s, this->contrast_);
  s->set_exposure_ctrl(s, !manual);
  if (manual) {
    s->set_aec_value(s, exposure);
  }
  s->set_gain_ctrl(s, !manual);
  if (manual) {
    s->set_agc_gain(s, gain);
  }
}

void SaltLevelComponent::camera_shutdown_() {
  this->flash_off_();  // every capture path ends here, so the LED can never stay on
  // Stops the sensor clock (the WiFi disturber); returns an error if not initialised, which is fine.
  // PWDN is deliberately left alone: forcing the OV3660 into hard power-down made the first init
  // attempt of every capture fail, a sensor that stays powered wakes up faster.
  esp_camera_deinit();
  this->camera_state_ = CameraState::OFF;
}

bool SaltLevelComponent::grab_and_encode_(CaptureResult &out) {
  // Auto exposure needs a few frames to settle after power-up; those frames are discarded.
  for (int i = 0; i < SETTLING_FRAMES; i++) {
    camera_fb_t *settling = esp_camera_fb_get();
    if (settling == nullptr) {
      this->report_error_("CAPTURE_FAILED", "no frame while the sensor settles");
      return false;
    }
    esp_camera_fb_return(settling);
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    this->report_error_("CAPTURE_FAILED", "esp_camera_fb_get returned no frame");
    return false;
  }
  if (fb->format != PIXFORMAT_GRAYSCALE) {
    esp_camera_fb_return(fb);
    this->report_error_("UNEXPECTED_FORMAT", "frame is not grayscale");
    return false;
  }

  out.width = fb->width;
  out.height = fb->height;
  out.stats = analyze_gray(fb->buf, out.width, out.height, this->threshold_());
  const bool encoded = frame2jpg(fb, this->jpeg_quality_, &out.jpg, &out.jpg_len);
  esp_camera_fb_return(fb);
  if (!encoded || out.jpg == nullptr) {
    out.jpg = nullptr;
    out.jpg_len = 0;
    this->report_error_("JPEG_ENCODE_FAILED", "frame2jpg failed");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
//  Publishing: sensors (+ calibration), image, meta
// ---------------------------------------------------------------------------------------------

void SaltLevelComponent::publish_measurement_(const CaptureResult &result) {
  const float white = result.stats.white_percent;
  float empty_ref = number_or_(this->empty_reference_number_, 0.0f);
  float full_ref = number_or_(this->full_reference_number_, 100.0f);

  // A pending calibration turns this measurement into the 100 % or 0 % reference.
  if (this->calibration_pending_ == Calibration::FULL) {
    if (white >= empty_ref + 0.5f) {
      full_ref = std::round(white * 10.0f) / 10.0f;
      set_number_(this->full_reference_number_, full_ref);
      ESP_LOGI(TAG, "Calibrated FULL reference: white share %.1f %%", full_ref);
    } else {
      this->report_error_("CALIBRATION_INVALID", "full reference must be above the empty reference");
    }
  } else if (this->calibration_pending_ == Calibration::EMPTY) {
    if (white <= full_ref - 0.5f) {
      empty_ref = std::round(white * 10.0f) / 10.0f;
      set_number_(this->empty_reference_number_, empty_ref);
      ESP_LOGI(TAG, "Calibrated EMPTY reference: white share %.1f %%", empty_ref);
    } else {
      this->report_error_("CALIBRATION_INVALID", "empty reference must be below the full reference");
    }
  }
  this->calibration_pending_ = Calibration::NONE;

  const float level = compute_level(white, empty_ref, full_ref);
  this->last_level_ = level;
  this->last_empty_reference_ = empty_ref;
  this->last_full_reference_ = full_ref;
  ESP_LOGI(TAG, "Frame %ux%u: white share %.2f %% (threshold %u), mean %.1f, level %.1f %%, JPEG %u bytes",
           (unsigned) result.width, (unsigned) result.height, white, (unsigned) this->threshold_(), result.stats.mean,
           level, (unsigned) result.jpg_len);

  if (this->white_share_sensor_ != nullptr) {
    this->white_share_sensor_->publish_state(white);
  }
  if (this->level_sensor_ != nullptr) {
    this->level_sensor_->publish_state(level);
  }
}

void SaltLevelComponent::publish_capture_(CaptureResult &result) {
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) {
    free(result.jpg);  // NOLINT(cppcoreguidelines-no-malloc)
    this->report_error_("MQTT_NOT_CONNECTED", "image dropped, broker not connected", result.jpg_len);
    return;
  }
  if (result.jpg_len > this->max_image_bytes_) {
    free(result.jpg);  // NOLINT(cppcoreguidelines-no-malloc)
    this->report_error_("IMAGE_TOO_LARGE", "JPEG exceeds max_image_bytes, not published", result.jpg_len);
    return;
  }

  // Raw binary JPEG, retained so the HA camera entity shows the last image after a restart.
  const uint32_t t0 = millis();
  const bool ok = client->publish(this->image_topic_.c_str(), reinterpret_cast<const char *>(result.jpg),
                                  result.jpg_len, 0, true);
  free(result.jpg);  // NOLINT(cppcoreguidelines-no-malloc)  - the MQTT client copies the payload
  ESP_LOGI(TAG, "Image publish: %u bytes, ok=%s, %u ms", (unsigned) result.jpg_len, YESNO(ok),
           (unsigned) (millis() - t0));
  if (!ok) {
    this->report_error_("MQTT_PUBLISH_FAILED", "publish() returned false", result.jpg_len);
    return;
  }
  this->publish_meta_("capture", nullptr, result.jpg_len, &result.stats, result.width, result.height);
}

void SaltLevelComponent::report_error_(const char *code, const char *detail, size_t bytes) {
  ESP_LOGE(TAG, "%s: %s", code, detail);
  this->set_error_text_(std::string(code) + ": " + detail);
  this->publish_meta_("error", code, bytes, nullptr, 0, 0);
}

void SaltLevelComponent::publish_meta_(const char *event, const char *error, size_t bytes, const GrayStats *stats,
                                       uint16_t width, uint16_t height) {
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) {
    return;
  }
  const uint32_t seq = ++this->sequence_;
  const uint32_t attempts = this->init_attempts_;
  const uint8_t threshold = this->threshold_();
  const uint16_t exposure = this->exposure_();
  const uint8_t gain = this->gain_();
  const uint8_t flash = this->flash_percent_();
  const float level = this->last_level_;
  const float empty_ref = this->last_empty_reference_;
  const float full_ref = this->last_full_reference_;
  const bool has_stats = stats != nullptr;
  const GrayStats st = has_stats ? *stats : GrayStats{};
  // `event` and `error` are always string literals, so capturing the pointers is safe.
  client->publish_json(
      this->meta_topic_.c_str(),
      [=](JsonObject root) {
        root["event"] = event;
        root["seq"] = seq;
        root["bytes"] = bytes;
        root["init_attempts"] = attempts;
        if (error != nullptr) {
          root["error"] = error;
        }
        if (has_stats) {
          root["width"] = width;
          root["height"] = height;
          root["threshold"] = threshold;
          root["exposure"] = exposure;
          root["gain"] = gain;
          root["flash"] = flash;
          root["white_pct"] = std::round(st.white_percent * 100.0f) / 100.0f;
          root["mean"] = std::round(st.mean * 10.0f) / 10.0f;
          if (!std::isnan(level)) {
            root["level_pct"] = std::round(level * 10.0f) / 10.0f;
          }
          root["empty_ref"] = empty_ref;
          root["full_ref"] = full_ref;
        }
        root["uptime_s"] = millis() / 1000;
        root["heap_internal_free"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        root["heap_internal_largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        root["psram_free"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        root["rssi"] = wifi::global_wifi_component->wifi_rssi();
      },
      0, false);
}

void SaltLevelComponent::publish_discovery_() {
  auto *client = mqtt::global_mqtt_client;
  const auto &info = client->get_discovery_info();
  if (info.prefix.empty()) {
    ESP_LOGD(TAG, "MQTT discovery disabled, skipping camera discovery");
    return;
  }

  const auto &node_name = App.get_name();
  const auto &friendly_name = App.get_friendly_name();
  const auto &node_friendly_name = friendly_name.empty() ? node_name : friendly_name;
  char mac[MAC_ADDRESS_BUFFER_SIZE];
  get_mac_address_into_buffer(mac);

  // Same device block as ESPHome's own entities (MAC identifier) so HA groups the camera under one device.
  const std::string topic = info.prefix + "/camera/" + node_name.c_str() + "/image/config";
  const std::string unique_id = std::string(mac) + "-camera-image";
  const auto &avail = client->get_availability();
  const std::string image_topic = this->image_topic_;

  const bool ok = client->publish_json(
      topic.c_str(),
      [&](JsonObject root) {
        root["name"] = "Kamera";
        root["unique_id"] = unique_id;
        root["topic"] = image_topic;
        if (!avail.topic.empty()) {
          root["availability_topic"] = avail.topic;
          if (avail.payload_available != "online") {
            root["payload_available"] = avail.payload_available;
          }
          if (avail.payload_not_available != "offline") {
            root["payload_not_available"] = avail.payload_not_available;
          }
        }
        JsonObject device = root["device"].to<JsonObject>();
        device["identifiers"] = mac;
        device["name"] = node_friendly_name;
        device["connections"][0][0] = "mac";
        device["connections"][0][1] = mac;
      },
      0, info.retain);
  ESP_LOGI(TAG, "Camera discovery published: %s", YESNO(ok));
}

}  // namespace esphome::salt_level
