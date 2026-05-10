#include <stdlib.h>
#include <string.h>

#include "display_priv.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/semphr.h"
#include "hal/lcd_types.h"

#include "display_ili9341.h"

#define DISPLAY_DEFAULT_QUEUE_DEPTH      10
#define DISPLAY_DEFAULT_FILL_BUFFER_LINES 40

static const char *TAG = "display";

static bool display_on_color_done(esp_lcd_panel_io_handle_t panel_io,
                                  esp_lcd_panel_io_event_data_t *edata,
                                  void *user_ctx);
static esp_err_t display_validate_config(const display_config_t *config);
static void display_clear_pending_callback(display_handle_t *display);
static size_t display_default_max_transfer_size(const display_config_t *config);
static size_t display_default_fill_lines(const display_config_t *config);

static bool display_on_color_done(esp_lcd_panel_io_handle_t panel_io,
                                  esp_lcd_panel_io_event_data_t *edata,
                                  void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    display_handle_t *display = user_ctx;
    BaseType_t task_woken = pdFALSE;
    display_flush_done_cb_t cb = NULL;
    void *cb_ctx = NULL;

    portENTER_CRITICAL_ISR(&display->lock);
    display->flush_in_flight = false;
    cb = display->pending_cb;
    cb_ctx = display->pending_cb_ctx;
    display->pending_cb = NULL;
    display->pending_cb_ctx = NULL;
    portEXIT_CRITICAL_ISR(&display->lock);

    xSemaphoreGiveFromISR(display->flush_done, &task_woken);
    if (cb && cb(display, cb_ctx)) {
        task_woken = pdTRUE;
    }
    return task_woken == pdTRUE;
}

static esp_err_t display_validate_config(const display_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(config->width > 0 && config->height > 0, ESP_ERR_INVALID_ARG, TAG,
                        "invalid resolution");
    ESP_RETURN_ON_FALSE(config->pin_cs >= 0 && config->pin_mosi >= 0 && config->pin_sclk >= 0 &&
                            config->pin_dc >= 0,
                        ESP_ERR_INVALID_ARG, TAG, "missing required SPI pins");
    ESP_RETURN_ON_FALSE(config->write_hz > 0, ESP_ERR_INVALID_ARG, TAG, "write_hz must be > 0");
    if (config->pin_touch_cs >= 0) {
        ESP_RETURN_ON_FALSE(config->pin_miso >= 0, ESP_ERR_INVALID_ARG, TAG,
                            "touch requires MISO to be connected");
    }
    return ESP_OK;
}

static void display_clear_pending_callback(display_handle_t *display)
{
    portENTER_CRITICAL(&display->lock);
    display->pending_cb = NULL;
    display->pending_cb_ctx = NULL;
    display->flush_in_flight = false;
    portEXIT_CRITICAL(&display->lock);
}

static size_t display_default_max_transfer_size(const display_config_t *config)
{
    return (size_t)config->width * (size_t)config->height * sizeof(uint16_t);
}

static size_t display_default_fill_lines(const display_config_t *config)
{
    size_t lines = DISPLAY_DEFAULT_FILL_BUFFER_LINES;
    if ((size_t)config->height < lines) {
        lines = config->height;
    }
    if (lines == 0) {
        lines = 1;
    }
    return lines;
}

esp_err_t display_new(const display_config_t *config, display_handle_t **out_display)
{
    esp_err_t ret = ESP_OK;
    display_handle_t *display = NULL;

    ESP_RETURN_ON_FALSE(out_display, ESP_ERR_INVALID_ARG, TAG, "out_display is null");
    ESP_RETURN_ON_ERROR(display_validate_config(config), TAG, "invalid display config");

    display = calloc(1, sizeof(*display));
    ESP_RETURN_ON_FALSE(display, ESP_ERR_NO_MEM, TAG, "no mem for display");

    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    display->config = *config;
    display->flush_done = xSemaphoreCreateBinary();
    display->lock = lock;
    display->current_width = config->width;
    display->current_height = config->height;
    display->touch_calibration.z_threshold = 350;
    display->touch_calibration.raw_error = 20;
    ESP_GOTO_ON_FALSE(display->flush_done, ESP_ERR_NO_MEM, err, TAG, "create semaphore failed");

    const size_t max_transfer_sz = config->max_transfer_sz ? config->max_transfer_sz :
                                                             display_default_max_transfer_size(config);
    const size_t fill_lines = config->fill_buffer_lines ? config->fill_buffer_lines :
                                                         display_default_fill_lines(config);
    const int max_dimension = (config->width > config->height) ? config->width : config->height;
    display->fill_buffer_pixels = (size_t)max_dimension * fill_lines;

    spi_bus_config_t bus_config = {
        .sclk_io_num = config->pin_sclk,
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = config->pin_miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)max_transfer_sz,
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(config->spi_host, &bus_config, SPI_DMA_CH_AUTO), err, TAG,
                      "spi_bus_initialize failed");
    display->bus_initialized = true;

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = config->pin_cs,
        .dc_gpio_num = config->pin_dc,
        .spi_mode = 0,
        .pclk_hz = config->write_hz,
        .trans_queue_depth = config->trans_queue_depth ? config->trans_queue_depth :
                                                         DISPLAY_DEFAULT_QUEUE_DEPTH,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi(config->spi_host, &io_config, &display->io), err, TAG,
                      "esp_lcd_new_panel_io_spi failed");

    esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = display_on_color_done,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(display->io, &callbacks, display),
                      err, TAG, "register panel io callback failed");

    if (config->pin_touch_cs >= 0) {
        spi_device_interface_config_t touch_config = {
            .clock_speed_hz = config->touch_hz ? (int)config->touch_hz : 2500000,
            .mode = 0,
            .spics_io_num = config->pin_touch_cs,
            .queue_size = 1,
        };
        ESP_GOTO_ON_ERROR(spi_bus_add_device(config->spi_host, &touch_config, &display->touch_spi),
                          err, TAG, "add touch device failed");
        display->touch_initialized = true;

        if (config->pin_touch_irq >= 0) {
            gpio_config_t irq_conf = {
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pin_bit_mask = 1ULL << config->pin_touch_irq,
            };
            ESP_GOTO_ON_ERROR(gpio_config(&irq_conf), err, TAG, "touch irq gpio config failed");
        }
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .rgb_ele_order = (config->rgb_order == DISPLAY_RGB_ORDER_BGR) ?
                             LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .reset_gpio_num = config->pin_rst,
        .flags.reset_active_high = config->reset_active_high,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_ili9341(display->io, &panel_config, &display->panel), err,
                      TAG, "create ILI9341 panel failed");

    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(display->panel), err, TAG, "panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(display->panel), err, TAG, "panel init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_set_gap(display->panel, config->x_gap, config->y_gap), err, TAG,
                      "set panel gap failed");
    if (config->invert_color) {
        ESP_GOTO_ON_ERROR(esp_lcd_panel_invert_color(display->panel, true), err, TAG,
                          "invert color failed");
    }
    ESP_GOTO_ON_ERROR(display_set_rotation(display, config->rotation), err, TAG,
                      "set panel rotation failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), err, TAG,
                      "display on failed");

    if (config->pin_bl >= 0) {
        gpio_config_t bl_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << config->pin_bl,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&bl_conf), err, TAG, "backlight gpio config failed");
        gpio_set_level(config->pin_bl, config->bl_on_level);
        display->backlight_initialized = true;
    }

    display->fill_buffer = display_alloc_dma_buffer(display,
                                                    display->fill_buffer_pixels * sizeof(uint16_t));
    ESP_GOTO_ON_FALSE(display->fill_buffer, ESP_ERR_NO_MEM, err, TAG, "alloc fill buffer failed");
    display->rgba_buffer = display_alloc_dma_buffer(display,
                                                    display->fill_buffer_pixels * sizeof(uint16_t));
    ESP_GOTO_ON_FALSE(display->rgba_buffer, ESP_ERR_NO_MEM, err, TAG, "alloc rgba buffer failed");

    *out_display = display;
    return ESP_OK;

err:
    if (display) {
        (void)display_delete(display);
    }
    return ret;
}

esp_err_t display_delete(display_handle_t *display)
{
    if (!display) {
        return ESP_OK;
    }

    if (display->flush_in_flight) {
        (void)display_flush_wait(display, pdMS_TO_TICKS(1000));
    }

    if (display->fill_buffer) {
        display_free_dma_buffer(display->fill_buffer);
    }
    if (display->rgba_buffer) {
        display_free_dma_buffer(display->rgba_buffer);
    }

    if (display->backlight_initialized) {
        gpio_set_level(display->config.pin_bl, !display->config.bl_on_level);
        gpio_reset_pin(display->config.pin_bl);
    }

    if (display->touch_initialized) {
        spi_bus_remove_device(display->touch_spi);
    }
    if (display->config.pin_touch_irq >= 0) {
        gpio_reset_pin(display->config.pin_touch_irq);
    }

    if (display->panel) {
        esp_lcd_panel_del(display->panel);
    }
    if (display->io) {
        esp_lcd_panel_io_del(display->io);
    }
    if (display->bus_initialized) {
        spi_bus_free(display->config.spi_host);
    }
    if (display->flush_done) {
        vSemaphoreDelete(display->flush_done);
    }
    free(display);
    return ESP_OK;
}

esp_err_t display_flush_async(display_handle_t *display, const display_area_t *area,
                              const void *pixel_data, display_flush_done_cb_t done_cb,
                              void *user_ctx)
{
    ESP_RETURN_ON_FALSE(display && area && pixel_data, ESP_ERR_INVALID_ARG, TAG,
                        "display, area or pixel_data is null");
    ESP_RETURN_ON_FALSE(area->x1 >= 0 && area->y1 >= 0 && area->x2 > area->x1 && area->y2 > area->y1,
                        ESP_ERR_INVALID_ARG, TAG, "invalid flush area");
    ESP_RETURN_ON_FALSE(area->x2 <= display->current_width && area->y2 <= display->current_height,
                        ESP_ERR_INVALID_ARG, TAG, "flush area exceeds current bounds");
    ESP_RETURN_ON_FALSE(display_is_dma_buffer(pixel_data), ESP_ERR_INVALID_ARG, TAG,
                        "pixel buffer must be DMA-capable");
    ESP_RETURN_ON_FALSE(esp_ptr_word_aligned(pixel_data), ESP_ERR_INVALID_ARG, TAG,
                        "pixel buffer must be 32-bit aligned");

    portENTER_CRITICAL(&display->lock);
    bool busy = display->flush_in_flight;
    if (!busy) {
        display->flush_in_flight = true;
        display->pending_cb = done_cb;
        display->pending_cb_ctx = user_ctx;
    }
    portEXIT_CRITICAL(&display->lock);

    ESP_RETURN_ON_FALSE(!busy, ESP_ERR_INVALID_STATE, TAG, "flush already in progress");

    xSemaphoreTake(display->flush_done, 0);
    esp_err_t ret = esp_lcd_panel_draw_bitmap(display->panel, area->x1, area->y1, area->x2,
                                              area->y2, pixel_data);
    if (ret != ESP_OK) {
        display_clear_pending_callback(display);
        return ret;
    }

    return ESP_OK;
}

esp_err_t display_flush_wait(display_handle_t *display, TickType_t timeout)
{
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, TAG, "display is null");
    if (!display->flush_in_flight) {
        return ESP_OK;
    }

    if (xSemaphoreTake(display->flush_done, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t display_fill_color(display_handle_t *display, uint16_t rgb565)
{
    ESP_RETURN_ON_FALSE(display && display->fill_buffer, ESP_ERR_INVALID_ARG, TAG,
                        "display or fill buffer is null");

    const uint16_t bus_color = display_rgb565_to_bus(rgb565);
    for (size_t i = 0; i < display->fill_buffer_pixels; ++i) {
        display->fill_buffer[i] = bus_color;
    }

    const int width = display->current_width;
    const int height = display->current_height;
    const int lines_per_chunk = (int)(display->fill_buffer_pixels / (size_t)width);
    for (int y = 0; y < height; y += lines_per_chunk) {
        const int chunk_lines = ((y + lines_per_chunk) > height) ? (height - y) : lines_per_chunk;
        display_area_t area = {
            .x1 = 0,
            .y1 = y,
            .x2 = width,
            .y2 = y + chunk_lines,
        };
        ESP_RETURN_ON_ERROR(display_flush_async(display, &area, display->fill_buffer, NULL, NULL), TAG,
                            "fill chunk flush failed");
        ESP_RETURN_ON_ERROR(display_flush_wait(display, pdMS_TO_TICKS(1000)), TAG,
                            "fill chunk wait failed");
    }

    return ESP_OK;
}

esp_err_t display_set_rotation(display_handle_t *display, display_rotation_t rotation)
{
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, TAG, "display is null");

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;

    switch (rotation) {
    case DISPLAY_ROTATION_0:
        swap_xy = false;
        mirror_x = true;
        mirror_y = false;
        display->current_width = display->config.width;
        display->current_height = display->config.height;
        break;
    case DISPLAY_ROTATION_90:
        swap_xy = true;
        mirror_x = false;
        mirror_y = false;
        display->current_width = display->config.height;
        display->current_height = display->config.width;
        break;
    case DISPLAY_ROTATION_180:
        swap_xy = false;
        mirror_x = false;
        mirror_y = true;
        display->current_width = display->config.width;
        display->current_height = display->config.height;
        break;
    case DISPLAY_ROTATION_270:
        swap_xy = true;
        mirror_x = true;
        mirror_y = true;
        display->current_width = display->config.height;
        display->current_height = display->config.width;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(display->panel, swap_xy), TAG, "swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(display->panel, mirror_x, mirror_y), TAG,
                        "mirror failed");
    display->config.rotation = rotation;
    return ESP_OK;
}

void *display_alloc_dma_buffer(display_handle_t *display, size_t size_bytes)
{
    if (!display) {
        ESP_LOGE(TAG, "display is null");
        return NULL;
    }
    return spi_bus_dma_memory_alloc(display->config.spi_host, size_bytes, MALLOC_CAP_INTERNAL);
}

void display_free_dma_buffer(void *buffer)
{
    free(buffer);
}

bool display_is_dma_buffer(const void *buffer)
{
    if (!buffer) {
        return false;
    }
    return esp_ptr_dma_capable(buffer) || esp_ptr_dma_ext_capable(buffer);
}

int display_get_width(const display_handle_t *display)
{
    return display ? display->current_width : 0;
}

int display_get_height(const display_handle_t *display)
{
    return display ? display->current_height : 0;
}
