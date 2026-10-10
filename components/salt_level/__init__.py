import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import number, sensor, text_sensor
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["esp32", "mqtt", "wifi", "psram"]
AUTO_LOAD = ["json"]

# esp32-camera driver, same version ESPHome's esp32_camera pins in 2026.9.0.
ESP32_CAMERA_VERSION = "2.1.7"

CONF_CAMERA = "camera"
CONF_DATA_PINS = "data_pins"
CONF_XCLK_PIN = "xclk_pin"
CONF_XCLK_FREQUENCY = "xclk_frequency"
CONF_PCLK_PIN = "pclk_pin"
CONF_VSYNC_PIN = "vsync_pin"
CONF_HREF_PIN = "href_pin"
CONF_SCCB_SDA_PIN = "sccb_sda_pin"
CONF_SCCB_SCL_PIN = "sccb_scl_pin"
CONF_POWER_DOWN_PIN = "power_down_pin"
CONF_RESET_PIN = "reset_pin"
CONF_FLASH_PIN = "flash_pin"
CONF_SCCB_DRIVER = "sccb_driver"
CONF_RESOLUTION = "resolution"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_VERTICAL_FLIP = "vertical_flip"
CONF_HORIZONTAL_MIRROR = "horizontal_mirror"
CONF_AUTO_EXPOSURE = "auto_exposure"
CONF_EXPOSURE_VALUE = "exposure_value"
CONF_AUTO_GAIN = "auto_gain"
CONF_GAIN_VALUE = "gain_value"
CONF_BRIGHTNESS = "brightness"
CONF_CONTRAST = "contrast"

CONF_IMAGE_TOPIC = "image_topic"
CONF_META_TOPIC = "meta_topic"
CONF_MAX_IMAGE_BYTES = "max_image_bytes"
CONF_MIN_CAPTURE_INTERVAL = "min_capture_interval"
CONF_CAMERA_INIT_DELAY = "camera_init_delay"
CONF_CAPTURE_ON_START = "capture_on_start"
CONF_WHITE_THRESHOLD = "white_threshold"

# Optional entity references: numbers are read at use time, sensors are published after a capture.
NUMBER_ENTITIES = {
    "threshold_number": "set_threshold_number",
    "interval_number": "set_interval_number",
    "exposure_number": "set_exposure_number",
    "gain_number": "set_gain_number",
    "flash_number": "set_flash_number",
    "full_reference_number": "set_full_reference_number",
    "empty_reference_number": "set_empty_reference_number",
}
SENSOR_ENTITIES = {
    "level_sensor": "set_level_sensor",
    "white_share_sensor": "set_white_share_sensor",
}
CONF_ERROR_TEXT_SENSOR = "error_text_sensor"

salt_level_ns = cg.esphome_ns.namespace("salt_level")
SaltLevelComponent = salt_level_ns.class_("SaltLevelComponent", cg.Component)

FRAME_SIZES = {
    "160x120": "FRAMESIZE_QQVGA",
    "320x240": "FRAMESIZE_QVGA",
    "640x480": "FRAMESIZE_VGA",
    "800x600": "FRAMESIZE_SVGA",
}

CAMERA_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_DATA_PINS): cv.All(
            [pins.internal_gpio_input_pin_number], cv.Length(min=8, max=8)
        ),
        cv.Required(CONF_XCLK_PIN): pins.internal_gpio_input_pin_number,
        cv.Optional(CONF_XCLK_FREQUENCY, default="20MHz"): cv.All(
            cv.frequency, cv.Range(min=8e6, max=20e6)
        ),
        cv.Required(CONF_PCLK_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_VSYNC_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_HREF_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_SCCB_SDA_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_SCCB_SCL_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_POWER_DOWN_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_RESET_PIN): pins.internal_gpio_output_pin_number,
        # White flash LED, switched on (PWM) only while a frame is taken.
        cv.Optional(CONF_FLASH_PIN): pins.internal_gpio_output_pin_number,
        # LEGACY is the SCCB path that detected the OV3660 reliably on this board;
        # ESPHome's own esp32_camera forces NEW.
        cv.Optional(CONF_SCCB_DRIVER, default="legacy"): cv.one_of(
            "legacy", "new", lower=True
        ),
        cv.Optional(CONF_RESOLUTION, default="320x240"): cv.one_of(
            *FRAME_SIZES, lower=True
        ),
        # frame2jpg quality: 1 (worst) .. 100 (best)
        cv.Optional(CONF_JPEG_QUALITY, default=50): cv.int_range(min=1, max=100),
        cv.Optional(CONF_VERTICAL_FLIP, default=False): cv.boolean,
        cv.Optional(CONF_HORIZONTAL_MIRROR, default=False): cv.boolean,
        cv.Optional(CONF_AUTO_EXPOSURE, default=True): cv.boolean,
        cv.Optional(CONF_EXPOSURE_VALUE, default=300): cv.int_range(min=0, max=1200),
        cv.Optional(CONF_AUTO_GAIN, default=True): cv.boolean,
        cv.Optional(CONF_GAIN_VALUE, default=0): cv.int_range(min=0, max=30),
        cv.Optional(CONF_BRIGHTNESS, default=0): cv.int_range(min=-2, max=2),
        cv.Optional(CONF_CONTRAST, default=0): cv.int_range(min=-2, max=2),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SaltLevelComponent),
        cv.Required(CONF_CAMERA): CAMERA_SCHEMA,
        cv.Required(CONF_IMAGE_TOPIC): cv.publish_topic,
        cv.Required(CONF_META_TOPIC): cv.publish_topic,
        cv.Optional(CONF_MAX_IMAGE_BYTES, default=100000): cv.int_range(
            min=1024, max=500000
        ),
        cv.Optional(
            CONF_MIN_CAPTURE_INTERVAL, default="5s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_CAMERA_INIT_DELAY, default="2s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_CAPTURE_ON_START, default=True): cv.boolean,
        # Fallback when no threshold_number entity is configured.
        cv.Optional(CONF_WHITE_THRESHOLD, default=115): cv.int_range(min=0, max=255),
        **{cv.Optional(key): cv.use_id(number.Number) for key in NUMBER_ENTITIES},
        **{cv.Optional(key): cv.use_id(sensor.Sensor) for key in SENSOR_ENTITIES},
        cv.Optional(CONF_ERROR_TEXT_SENSOR): cv.use_id(text_sensor.TextSensor),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cam = config[CONF_CAMERA]
    cg.add(var.set_data_pins(cam[CONF_DATA_PINS]))
    cg.add(var.set_xclk_pin(cam[CONF_XCLK_PIN]))
    cg.add(var.set_xclk_frequency(int(cam[CONF_XCLK_FREQUENCY])))
    cg.add(var.set_pclk_pin(cam[CONF_PCLK_PIN]))
    cg.add(var.set_vsync_pin(cam[CONF_VSYNC_PIN]))
    cg.add(var.set_href_pin(cam[CONF_HREF_PIN]))
    cg.add(var.set_sccb_sda_pin(cam[CONF_SCCB_SDA_PIN]))
    cg.add(var.set_sccb_scl_pin(cam[CONF_SCCB_SCL_PIN]))
    if CONF_POWER_DOWN_PIN in cam:
        cg.add(var.set_power_down_pin(cam[CONF_POWER_DOWN_PIN]))
    if CONF_RESET_PIN in cam:
        cg.add(var.set_reset_pin(cam[CONF_RESET_PIN]))
    if CONF_FLASH_PIN in cam:
        cg.add(var.set_flash_pin(cam[CONF_FLASH_PIN]))
    cg.add(var.set_frame_size(cg.RawExpression(FRAME_SIZES[cam[CONF_RESOLUTION]])))
    cg.add(var.set_jpeg_quality(cam[CONF_JPEG_QUALITY]))
    cg.add(var.set_vertical_flip(cam[CONF_VERTICAL_FLIP]))
    cg.add(var.set_horizontal_mirror(cam[CONF_HORIZONTAL_MIRROR]))
    cg.add(var.set_auto_exposure(cam[CONF_AUTO_EXPOSURE]))
    cg.add(var.set_exposure_value(cam[CONF_EXPOSURE_VALUE]))
    cg.add(var.set_auto_gain(cam[CONF_AUTO_GAIN]))
    cg.add(var.set_gain_value(cam[CONF_GAIN_VALUE]))
    cg.add(var.set_brightness(cam[CONF_BRIGHTNESS]))
    cg.add(var.set_contrast(cam[CONF_CONTRAST]))

    cg.add(var.set_image_topic(config[CONF_IMAGE_TOPIC]))
    cg.add(var.set_meta_topic(config[CONF_META_TOPIC]))
    cg.add(var.set_max_image_bytes(config[CONF_MAX_IMAGE_BYTES]))
    cg.add(var.set_min_capture_interval_ms(config[CONF_MIN_CAPTURE_INTERVAL]))
    cg.add(var.set_camera_init_delay_ms(config[CONF_CAMERA_INIT_DELAY]))
    cg.add(var.set_capture_on_start(config[CONF_CAPTURE_ON_START]))
    cg.add(var.set_white_threshold(config[CONF_WHITE_THRESHOLD]))

    for key, setter in NUMBER_ENTITIES.items():
        if key in config:
            cg.add(getattr(var, setter)(await cg.get_variable(config[key])))
    for key, setter in SENSOR_ENTITIES.items():
        if key in config:
            cg.add(getattr(var, setter)(await cg.get_variable(config[key])))
    if CONF_ERROR_TEXT_SENSOR in config:
        cg.add(
            var.set_error_text_sensor(
                await cg.get_variable(config[CONF_ERROR_TEXT_SENSOR])
            )
        )

    add_idf_component(name="espressif/esp32-camera", ref=ESP32_CAMERA_VERSION)
    legacy = cam[CONF_SCCB_DRIVER] == "legacy"
    add_idf_sdkconfig_option("CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY", legacy)
    add_idf_sdkconfig_option("CONFIG_SCCB_HARDWARE_I2C_DRIVER_NEW", not legacy)
