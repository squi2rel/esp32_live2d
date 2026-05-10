#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "display.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/semphr.h"

struct display_handle_t {
    display_config_t config;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t flush_done;
    portMUX_TYPE lock;
    display_flush_done_cb_t pending_cb;
    void *pending_cb_ctx;
    spi_device_handle_t touch_spi;
    uint16_t *fill_buffer;
    uint16_t *rgba_buffer;
    size_t fill_buffer_pixels;
    int current_width;
    int current_height;
    display_touch_calibration_t touch_calibration;
    int64_t touch_press_deadline_us;
    volatile bool flush_in_flight;
    bool bus_initialized;
    bool backlight_initialized;
    bool touch_initialized;
};
