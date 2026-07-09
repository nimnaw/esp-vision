/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "camera.h"

#include <inttypes.h>
#include <string.h>

#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"

#include "boardconfig.h"
#include "debug.h"

/*
 * esp32-camera uses OpenMV-style PIXFORMAT_* names that conflict with imlib.
 * Keep those names local to this include and expose them with an ESP32_CAMERA_
 * prefix inside this board backend.
 */
#define pixformat_t         esp32_camera_pixformat_t
#define PIXFORMAT_RGB565    ESP32_CAMERA_PIXFORMAT_RGB565
#define PIXFORMAT_YUV422    ESP32_CAMERA_PIXFORMAT_YUV422
#define PIXFORMAT_YUV420    ESP32_CAMERA_PIXFORMAT_YUV420
#define PIXFORMAT_GRAYSCALE ESP32_CAMERA_PIXFORMAT_GRAYSCALE
#define PIXFORMAT_JPEG      ESP32_CAMERA_PIXFORMAT_JPEG
#define PIXFORMAT_RGB888    ESP32_CAMERA_PIXFORMAT_RGB888
#define PIXFORMAT_RAW       ESP32_CAMERA_PIXFORMAT_RAW
#define PIXFORMAT_RGB444    ESP32_CAMERA_PIXFORMAT_RGB444
#define PIXFORMAT_RGB555    ESP32_CAMERA_PIXFORMAT_RGB555
#define PIXFORMAT_RAW8      ESP32_CAMERA_PIXFORMAT_RAW8
#include "esp_camera.h"
#undef pixformat_t
#undef PIXFORMAT_RGB565
#undef PIXFORMAT_YUV422
#undef PIXFORMAT_YUV420
#undef PIXFORMAT_GRAYSCALE
#undef PIXFORMAT_JPEG
#undef PIXFORMAT_RGB888
#undef PIXFORMAT_RAW
#undef PIXFORMAT_RGB444
#undef PIXFORMAT_RGB555
#undef PIXFORMAT_RAW8

#ifndef CMSIS_MCU_H
#define CMSIS_MCU_H "cmsis_compiler.h"
#endif

#ifndef NO_QSTR
#include "imlib.h"
#endif

#define ESP_VISION_CAMERA_CAPTURE_RETRY_COUNT 3

typedef struct {
    bool initialized;
    bool hmirror;
    bool vflip;
    uint32_t raw_input_width;
    uint32_t raw_input_height;
    uint32_t active_input_width;
    uint32_t active_input_height;
    uint32_t active_input_offset_x;
    uint32_t active_input_offset_y;
    uint32_t width;
    uint32_t height;
    pixformat_t output_pixfmt;
} esp_vision_camera_context_t;

static const char *TAG = "esp_vision_camera";
static esp_vision_camera_context_t s_camera = {
    .width = ESP_VISION_CAMERA_OUTPUT_QVGA_WIDTH,
    .height = ESP_VISION_CAMERA_OUTPUT_QVGA_HEIGHT,
    .output_pixfmt = PIXFORMAT_RGB565,
};

static void esp_vision_camera_set_dimensions(uint32_t width, uint32_t height)
{
    s_camera.raw_input_width = width;
    s_camera.raw_input_height = height;
    s_camera.active_input_width = width;
    s_camera.active_input_height = height;
    s_camera.active_input_offset_x = 0;
    s_camera.active_input_offset_y = 0;
    s_camera.width = width;
    s_camera.height = height;
}

static void esp_vision_camera_set_defaults(void)
{
    esp_vision_camera_set_dimensions(ESP_VISION_CAMERA_OUTPUT_QVGA_WIDTH,
                                     ESP_VISION_CAMERA_OUTPUT_QVGA_HEIGHT);
    s_camera.output_pixfmt = PIXFORMAT_RGB565;
}

void esp_vision_camera_init0(void)
{
    esp_vision_camera_deinit();
    memset(&s_camera, 0, sizeof(s_camera));
    esp_vision_camera_set_defaults();
}

static size_t esp_vision_camera_bpp(uint32_t pixfmt)
{
    switch (pixfmt) {
    case PIXFORMAT_GRAYSCALE:
        return sizeof(uint8_t);
    case PIXFORMAT_RGB565:
        return sizeof(uint16_t);
    default:
        return 0;
    }
}

static size_t esp_vision_camera_output_size(uint32_t width, uint32_t height, uint32_t pixfmt)
{
    return (size_t)width * (size_t)height * esp_vision_camera_bpp(pixfmt);
}

static esp_err_t esp_vision_camera_to_esp32_framesize(uint32_t width,
                                                      uint32_t height,
                                                      framesize_t *framesize)
{
    if (framesize == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((width == ESP_VISION_CAMERA_OUTPUT_QQVGA_WIDTH) &&
            (height == ESP_VISION_CAMERA_OUTPUT_QQVGA_HEIGHT)) {
        *framesize = FRAMESIZE_QQVGA;
        return ESP_OK;
    }

    if ((width == ESP_VISION_CAMERA_OUTPUT_QVGA_WIDTH) &&
            (height == ESP_VISION_CAMERA_OUTPUT_QVGA_HEIGHT)) {
        *framesize = FRAMESIZE_QVGA;
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

static void esp_vision_camera_log_frame(const char *reason, const camera_fb_t *fb)
{
    if ((fb == NULL) || (fb->buf == NULL) || (fb->len == 0)) {
        esp_vision_debug_printf("[esp-vision] camera frame %s: no frame\r\n", reason);
        return;
    }

    const uint8_t *data = fb->buf;
    size_t size = fb->len;
    uint8_t h0 = (size > 0) ? data[0] : 0;
    uint8_t h1 = (size > 1) ? data[1] : 0;
    uint8_t h2 = (size > 2) ? data[2] : 0;
    uint8_t h3 = (size > 3) ? data[3] : 0;
    uint8_t t0 = (size > 1) ? data[size - 2] : 0;
    uint8_t t1 = (size > 0) ? data[size - 1] : 0;

    esp_vision_debug_printf("[esp-vision] camera frame %s: bytes=%u frame=%ux%u fmt=%u"
                            " head=%02x %02x %02x %02x tail=%02x %02x\r\n",
                            reason,
                            (unsigned int)fb->len,
                            (unsigned int)fb->width,
                            (unsigned int)fb->height,
                            (unsigned int)fb->format,
                            h0,
                            h1,
                            h2,
                            h3,
                            t0,
                            t1);
}

/*
 * The esp32-camera driver delivers RGB565 with the high byte (R[4:0]G[5:3])
 * first. Grayscale conversion scales each channel to 8 bits (r<<3, g<<2,
 * b<<3) and applies BT.601 weights (77, 150, 29) over 256, i.e.
 * (r*616 + g*600 + b*232) >> 8. The per-channel products are precomputed so
 * each pixel is three table loads and two adds instead of shift/multiply
 * math. The tables stay as plain flash rodata (not IRAM_ATTR): on ESP32-S3
 * const data must not be placed in the IRAM window, where data-bus reads
 * fault with a LoadStoreError.
 */
#define ESP_VISION_CAMERA_LUMA_R_WEIGHT (616)
#define ESP_VISION_CAMERA_LUMA_G_WEIGHT (600)
#define ESP_VISION_CAMERA_LUMA_B_WEIGHT (232)

static const uint16_t s_luma_r[32] = {
    0 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 1 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    2 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 3 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    4 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 5 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    6 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 7 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    8 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 9 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    10 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 11 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    12 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 13 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    14 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 15 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    16 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 17 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    18 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 19 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    20 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 21 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    22 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 23 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    24 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 25 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    26 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 27 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    28 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 29 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
    30 * ESP_VISION_CAMERA_LUMA_R_WEIGHT, 31 * ESP_VISION_CAMERA_LUMA_R_WEIGHT,
};

static const uint16_t s_luma_g[64] = {
    0 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 1 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    2 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 3 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    4 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 5 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    6 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 7 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    8 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 9 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    10 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 11 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    12 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 13 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    14 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 15 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    16 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 17 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    18 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 19 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    20 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 21 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    22 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 23 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    24 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 25 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    26 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 27 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    28 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 29 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    30 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 31 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    32 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 33 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    34 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 35 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    36 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 37 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    38 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 39 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    40 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 41 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    42 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 43 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    44 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 45 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    46 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 47 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    48 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 49 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    50 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 51 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    52 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 53 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    54 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 55 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    56 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 57 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    58 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 59 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    60 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 61 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
    62 * ESP_VISION_CAMERA_LUMA_G_WEIGHT, 63 * ESP_VISION_CAMERA_LUMA_G_WEIGHT,
};

static const uint16_t s_luma_b[32] = {
    0 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 1 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    2 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 3 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    4 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 5 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    6 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 7 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    8 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 9 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    10 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 11 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    12 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 13 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    14 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 15 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    16 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 17 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    18 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 19 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    20 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 21 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    22 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 23 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    24 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 25 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    26 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 27 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    28 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 29 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
    30 * ESP_VISION_CAMERA_LUMA_B_WEIGHT, 31 * ESP_VISION_CAMERA_LUMA_B_WEIGHT,
};

/* Byte-swap each RGB565 pixel into imlib big-endian order, two pixels per
 * 32-bit word: reversing the word byte order and rotating the halves is the
 * per-pixel 16-bit byte swap applied to both pixels at once. */
static void esp_vision_camera_rgb565_swap(const uint8_t *src, uint8_t *dst, size_t bytes)
{
    const uint32_t *src_w = (const uint32_t *)src;
    uint32_t *dst_w = (uint32_t *)dst;
    size_t words = bytes / sizeof(uint32_t);

    for (size_t i = 0; i < words; i++) {
        uint32_t w = __builtin_bswap32(src_w[i]);
        dst_w[i] = (w >> 16) | (w << 16);
    }
}

/* Convert big-endian RGB565 frames to 8-bit luma. Channels are extracted
 * directly from the delivered byte order (R in bits 7:3, G split across
 * bits 15:13 and 7:5, B in bits 12:8 of the native read), so no byte swap
 * is needed. */
static void esp_vision_camera_rgb565_to_grayscale(const uint8_t *src, uint8_t *dst, size_t bytes)
{
    const uint16_t *src_px = (const uint16_t *)src;
    size_t count = bytes / sizeof(uint16_t);

    for (size_t i = 0; i < count; i++) {
        uint16_t v = src_px[i];
        uint8_t r = (uint8_t)((v >> 3) & 0x1F);
        uint8_t g = (uint8_t)(((v >> 5) & 0x38) | ((v >> 13) & 0x07));
        uint8_t b = (uint8_t)((v >> 8) & 0x1F);
        dst[i] = (uint8_t)((s_luma_r[r] + s_luma_g[g] + s_luma_b[b]) >> 8);
    }
}

esp_err_t esp_vision_camera_get_framesize_dimensions(esp_vision_camera_framesize_t framesize,
                                                     uint32_t *width,
                                                     uint32_t *height)
{
    if ((width == NULL) || (height == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (framesize) {
    case ESP_VISION_CAMERA_FRAMESIZE_QQVGA:
        *width = ESP_VISION_CAMERA_OUTPUT_QQVGA_WIDTH;
        *height = ESP_VISION_CAMERA_OUTPUT_QQVGA_HEIGHT;
        return ESP_OK;
    case ESP_VISION_CAMERA_FRAMESIZE_QVGA:
        *width = ESP_VISION_CAMERA_OUTPUT_QVGA_WIDTH;
        *height = ESP_VISION_CAMERA_OUTPUT_QVGA_HEIGHT;
        return ESP_OK;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t esp_vision_camera_init(void)
{
    if (s_camera.initialized) {
        return ESP_OK;
    }

    if ((s_camera.width == 0) || (s_camera.height == 0) || (s_camera.output_pixfmt == PIXFORMAT_INVALID)) {
        esp_vision_camera_set_defaults();
    }

    framesize_t frame_size = FRAMESIZE_QVGA;
    esp_err_t ret = esp_vision_camera_to_esp32_framesize(s_camera.width, s_camera.height, &frame_size);
    if (ret != ESP_OK) {
        return ret;
    }

    const camera_config_t config = {
        .pin_pwdn = ESP_VISION_CAMERA_SENSOR_PWDN_PIN,
        .pin_reset = ESP_VISION_CAMERA_SENSOR_RESET_PIN,
        .pin_xclk = ESP_VISION_CAMERA_XCLK_PIN,
        .pin_sccb_sda = ESP_VISION_CAMERA_SCCB_I2C_SDA_PIN,
        .pin_sccb_scl = ESP_VISION_CAMERA_SCCB_I2C_SCL_PIN,
        .pin_d7 = ESP_VISION_CAMERA_DVP_D7_PIN,
        .pin_d6 = ESP_VISION_CAMERA_DVP_D6_PIN,
        .pin_d5 = ESP_VISION_CAMERA_DVP_D5_PIN,
        .pin_d4 = ESP_VISION_CAMERA_DVP_D4_PIN,
        .pin_d3 = ESP_VISION_CAMERA_DVP_D3_PIN,
        .pin_d2 = ESP_VISION_CAMERA_DVP_D2_PIN,
        .pin_d1 = ESP_VISION_CAMERA_DVP_D1_PIN,
        .pin_d0 = ESP_VISION_CAMERA_DVP_D0_PIN,
        .pin_vsync = ESP_VISION_CAMERA_DVP_VSYNC_PIN,
        .pin_href = ESP_VISION_CAMERA_DVP_HSYNC_PIN,
        .pin_pclk = ESP_VISION_CAMERA_DVP_PCLK_PIN,
        .xclk_freq_hz = ESP_VISION_CAMERA_XCLK_FREQ,
        .ledc_timer = (ledc_timer_t)ESP_VISION_CAMERA_XCLK_LEDC_TIMER,
        .ledc_channel = (ledc_channel_t)ESP_VISION_CAMERA_XCLK_LEDC_CHANNEL,
        .pixel_format = ESP32_CAMERA_PIXFORMAT_RGB565,
        .frame_size = frame_size,
        .fb_count = ESP_VISION_CAMERA_BUFFER_COUNT,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = ESP_VISION_CAMERA_SCCB_I2C_PORT,
    };

    ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize esp32-camera: %s", esp_err_to_name(ret));
        esp_vision_debug_printf("[esp-vision] camera start failed: %s\r\n", esp_err_to_name(ret));
        return ret;
    }

    s_camera.vflip = true;
    s_camera.hmirror = true;
    s_camera.initialized = true;

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        sensor->set_hmirror(sensor, s_camera.hmirror ? 1 : 0);
        sensor->set_vflip(sensor, s_camera.vflip ? 1 : 0);
    }

    esp_vision_debug_printf("[esp-vision] camera started: sensor=0x%04" PRIx32
                            " raw=%" PRIu32 "x%" PRIu32
                            " active=%" PRIu32 "x%" PRIu32 "+%" PRIu32 "+%" PRIu32
                            " output=%" PRIu32 "x%" PRIu32
                            " driver=esp32-camera pixfmt=%" PRIu32 "\r\n",
                            ESP_VISION_CAMERA_SENSOR_ID,
                            s_camera.raw_input_width,
                            s_camera.raw_input_height,
                            s_camera.active_input_width,
                            s_camera.active_input_height,
                            s_camera.active_input_offset_x,
                            s_camera.active_input_offset_y,
                            s_camera.width,
                            s_camera.height,
                            (uint32_t)s_camera.output_pixfmt);
    return ESP_OK;
}

void esp_vision_camera_deinit(void)
{
    if (s_camera.initialized) {
        esp_camera_deinit();
        s_camera.initialized = false;
    }
}

bool esp_vision_camera_is_ready(void)
{
    return s_camera.initialized &&
           (s_camera.width != 0) &&
           (s_camera.height != 0);
}

esp_err_t esp_vision_camera_set_pixformat(uint32_t pixfmt)
{
    if ((pixfmt != PIXFORMAT_RGB565) && (pixfmt != PIXFORMAT_GRAYSCALE)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_camera.output_pixfmt = (pixformat_t)pixfmt;
    return ESP_OK;
}

uint32_t esp_vision_camera_get_pixformat(void)
{
    return s_camera.output_pixfmt;
}

esp_err_t esp_vision_camera_set_framesize(esp_vision_camera_framesize_t framesize)
{
    uint32_t width = 0;
    uint32_t height = 0;
    esp_err_t ret = esp_vision_camera_get_framesize_dimensions(framesize, &width, &height);
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_camera.initialized && s_camera.width == width && s_camera.height == height) {
        return ESP_OK;
    }

    if (s_camera.initialized) {
        esp_vision_camera_deinit();
    }

    esp_vision_camera_set_dimensions(width, height);
    return esp_vision_camera_init();
}

uint32_t esp_vision_camera_get_width(void)
{
    return s_camera.width;
}

uint32_t esp_vision_camera_get_height(void)
{
    return s_camera.height;
}

esp_err_t esp_vision_camera_set_hmirror(bool enable)
{
    s_camera.hmirror = enable;

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        sensor->set_hmirror(sensor, enable ? 1 : 0);
    }
    return ESP_OK;
}

bool esp_vision_camera_get_hmirror(void)
{
    return s_camera.hmirror;
}

esp_err_t esp_vision_camera_set_vflip(bool enable)
{
    s_camera.vflip = enable;

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        sensor->set_vflip(sensor, enable ? 1 : 0);
    }
    return ESP_OK;
}

bool esp_vision_camera_get_vflip(void)
{
    return s_camera.vflip;
}

uint32_t esp_vision_camera_get_sensor_id(void)
{
    return ESP_VISION_CAMERA_SENSOR_ID;
}

size_t esp_vision_camera_frame_size(void)
{
    return esp_vision_camera_output_size(s_camera.width, s_camera.height, s_camera.output_pixfmt);
}

esp_err_t esp_vision_camera_capture(uint8_t *pixels, size_t pixels_size)
{
    if ((pixels == NULL) || !esp_vision_camera_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t expected_size = esp_vision_camera_frame_size();
    if ((expected_size == 0) || (pixels_size < expected_size)) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (int attempt = 0; attempt < ESP_VISION_CAMERA_CAPTURE_RETRY_COUNT; attempt++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            return ESP_FAIL;
        }

        esp_err_t ret = ESP_OK;
        if ((fb->format != ESP32_CAMERA_PIXFORMAT_RGB565) ||
                ((uint32_t)fb->width != s_camera.width) ||
                ((uint32_t)fb->height != s_camera.height) ||
                (fb->len != (size_t)fb->width * (size_t)fb->height * sizeof(uint16_t))) {
            esp_vision_camera_log_frame("rgb565 size mismatch", fb);
            ret = ESP_ERR_INVALID_RESPONSE;
        } else if (s_camera.output_pixfmt == PIXFORMAT_GRAYSCALE) {
            esp_vision_camera_rgb565_to_grayscale(fb->buf, pixels, fb->len);
        } else {
            esp_vision_camera_rgb565_swap(fb->buf, pixels, fb->len);
        }

        esp_camera_fb_return(fb);
        if (ret != ESP_ERR_INVALID_RESPONSE) {
            return ret;
        }
    }

    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t esp_vision_camera_snapshot(image_t *img, uint8_t *pixels, size_t pixels_size)
{
    if (img == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_vision_camera_capture(pixels, pixels_size);
    if (ret != ESP_OK) {
        return ret;
    }

    img->w = s_camera.width;
    img->h = s_camera.height;
    img->pixfmt = s_camera.output_pixfmt;
    img->size = 0;
    img->_raw = NULL;
    img->pixels = pixels;
    return ESP_OK;
}

esp_err_t esp_vision_camera_get_status(esp_vision_camera_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    status->ready = esp_vision_camera_is_ready();
    status->sensor_id = ESP_VISION_CAMERA_SENSOR_ID;
    status->raw_input_width = s_camera.raw_input_width;
    status->raw_input_height = s_camera.raw_input_height;
    status->active_input_width = s_camera.active_input_width;
    status->active_input_height = s_camera.active_input_height;
    status->active_input_offset_x = s_camera.active_input_offset_x;
    status->active_input_offset_y = s_camera.active_input_offset_y;
    status->width = s_camera.width;
    status->height = s_camera.height;
    status->pixfmt = s_camera.output_pixfmt;
    status->hmirror = s_camera.hmirror;
    status->vflip = s_camera.vflip;
    return ESP_OK;
}
