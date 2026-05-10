#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct display_handle_t display_handle_t;

typedef enum {
    DISPLAY_ROTATION_0 = 0,
    DISPLAY_ROTATION_90,
    DISPLAY_ROTATION_180,
    DISPLAY_ROTATION_270,
} display_rotation_t;

typedef enum {
    DISPLAY_RGB_ORDER_RGB = 0,
    DISPLAY_RGB_ORDER_BGR,
} display_rgb_order_t;

typedef struct {
    int x;
    int y;
} display_point_t;

typedef struct {
    int x1;
    int y1;
    int x2; // exclusive
    int y2; // exclusive
} display_area_t;

#define DISPLAY_TOUCH_CALIBRATION_POINT_COUNT 5

typedef struct {
    bool touched;
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint8_t quality;
} display_touch_data_t;

typedef struct {
    int32_t x_a_q16;
    int32_t x_b_q16;
    int32_t x_c_q16;
    int32_t y_a_q16;
    int32_t y_b_q16;
    int32_t y_c_q16;
    uint16_t z_threshold;
    uint16_t raw_error;
    bool valid;
} display_touch_calibration_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t pin_cs;
    gpio_num_t pin_mosi;
    gpio_num_t pin_miso;
    gpio_num_t pin_sclk;
    gpio_num_t pin_dc;
    gpio_num_t pin_rst;
    gpio_num_t pin_bl;
    gpio_num_t pin_touch_cs;
    gpio_num_t pin_touch_irq;
    int width;
    int height;
    int x_gap;
    int y_gap;
    unsigned int write_hz;
    unsigned int read_hz;   // Reserved for future panel read support.
    unsigned int touch_hz;  // Reserved for future touch support.
    size_t max_transfer_sz;
    size_t trans_queue_depth;
    size_t fill_buffer_lines;
    display_rotation_t rotation;
    display_rgb_order_t rgb_order;
    bool invert_color;
    bool reset_active_high;
    bool bl_on_level;
} display_config_t;

// Called from the SPI post-transaction callback context. Keep it ISR-safe.
typedef bool (*display_flush_done_cb_t)(display_handle_t *display, void *user_ctx);

esp_err_t display_new(const display_config_t *config, display_handle_t **out_display);
esp_err_t display_delete(display_handle_t *display);

// pixel_data must be RGB565 in LCD byte order (big-endian on the wire).
esp_err_t display_flush_async(display_handle_t *display, const display_area_t *area,
                              const void *pixel_data, display_flush_done_cb_t done_cb,
                              void *user_ctx);
esp_err_t display_flush_rgba8888(display_handle_t *display, const display_area_t *area,
                                 const void *src_rgba8888, size_t src_stride_bytes);
void display_convert_rgba8888_to_rgb565_be(void *dst_rgb565_be, const void *src_rgba8888,
                                           size_t pixel_count);
esp_err_t display_flush_wait(display_handle_t *display, TickType_t timeout);
esp_err_t display_fill_color(display_handle_t *display, uint16_t rgb565);
esp_err_t display_set_rotation(display_handle_t *display, display_rotation_t rotation);

bool display_touch_is_available(const display_handle_t *display);
esp_err_t display_touch_read_raw(display_handle_t *display, display_touch_data_t *out_data);
esp_err_t display_touch_read(display_handle_t *display, display_touch_data_t *out_data);
esp_err_t display_touch_get_calibration_points(const display_handle_t *display, int inset,
                                               display_point_t points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT]);
esp_err_t display_touch_calibrate(display_handle_t *display,
                                  const display_point_t raw_points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT],
                                  const display_point_t logical_points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT],
                                  display_touch_calibration_t *out_calibration);
esp_err_t display_touch_set_calibration(display_handle_t *display,
                                        const display_touch_calibration_t *calibration);
esp_err_t display_touch_get_calibration(const display_handle_t *display,
                                        display_touch_calibration_t *out_calibration);

void *display_alloc_dma_buffer(display_handle_t *display, size_t size_bytes);
void display_free_dma_buffer(void *buffer);
bool display_is_dma_buffer(const void *buffer);

int display_get_width(const display_handle_t *display);
int display_get_height(const display_handle_t *display);

static inline uint16_t display_rgb565_to_bus(uint16_t rgb565)
{
    return __builtin_bswap16(rgb565);
}

static inline uint16_t display_rgb888_to_bus(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t rgb565 = (uint16_t)(((r & 0xF8) << 8) |
                                 ((g & 0xFC) << 3) |
                                 (b >> 3));
    return display_rgb565_to_bus(rgb565);
}

#ifdef __cplusplus
}
#endif
