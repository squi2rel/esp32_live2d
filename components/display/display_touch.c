#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display_priv.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#define DISPLAY_TOUCH_DEFAULT_THRESHOLD    350
#define DISPLAY_TOUCH_MIN_THRESHOLD        20
#define DISPLAY_TOUCH_DEFAULT_RAW_ERROR    20
#define DISPLAY_TOUCH_HOLD_US              50000
#define DISPLAY_TOUCH_VALID_SAMPLES        5
#define DISPLAY_TOUCH_AXIS_SAMPLES         4
#define DISPLAY_TOUCH_SETTLE_RETRIES       8
#define DISPLAY_TOUCH_DEFAULT_CAL_INSET    24

#define XPT2046_CMD_Y_POSITION 0xD0
#define XPT2046_CMD_X_POSITION 0x90
#define XPT2046_CMD_Z1         0xB0
#define XPT2046_CMD_Z2         0xC0

static const char *TAG = "display_touch";

static inline double display_touch_absd(double value)
{
    return (value < 0.0) ? -value : value;
}

static inline uint16_t display_touch_threshold(const display_handle_t *display)
{
    return display->touch_calibration.z_threshold ? display->touch_calibration.z_threshold :
                                                    DISPLAY_TOUCH_DEFAULT_THRESHOLD;
}

static inline uint16_t display_touch_raw_error(const display_handle_t *display)
{
    return display->touch_calibration.raw_error ? display->touch_calibration.raw_error :
                                                  DISPLAY_TOUCH_DEFAULT_RAW_ERROR;
}

static void display_touch_logical_to_base(const display_handle_t *display, int logical_x, int logical_y,
                                          int *base_x, int *base_y)
{
    const int width = display->config.width;
    const int height = display->config.height;

    switch (display->config.rotation) {
    case DISPLAY_ROTATION_0:
        *base_x = logical_x;
        *base_y = logical_y;
        break;
    case DISPLAY_ROTATION_90:
        *base_x = width - 1 - logical_y;
        *base_y = logical_x;
        break;
    case DISPLAY_ROTATION_180:
        *base_x = width - 1 - logical_x;
        *base_y = height - 1 - logical_y;
        break;
    case DISPLAY_ROTATION_270:
        *base_x = logical_y;
        *base_y = height - 1 - logical_x;
        break;
    default:
        *base_x = logical_x;
        *base_y = logical_y;
        break;
    }
}

static void display_touch_base_to_logical(const display_handle_t *display, int base_x, int base_y,
                                          int *logical_x, int *logical_y)
{
    const int width = display->config.width;
    const int height = display->config.height;

    switch (display->config.rotation) {
    case DISPLAY_ROTATION_0:
        *logical_x = base_x;
        *logical_y = base_y;
        break;
    case DISPLAY_ROTATION_90:
        *logical_x = base_y;
        *logical_y = width - 1 - base_x;
        break;
    case DISPLAY_ROTATION_180:
        *logical_x = width - 1 - base_x;
        *logical_y = height - 1 - base_y;
        break;
    case DISPLAY_ROTATION_270:
        *logical_x = height - 1 - base_y;
        *logical_y = base_x;
        break;
    default:
        *logical_x = base_x;
        *logical_y = base_y;
        break;
    }
}

static esp_err_t display_touch_transfer24_locked(display_handle_t *display, uint8_t command,
                                                 uint16_t *out_value)
{
    spi_transaction_t trans = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 24,
    };
    trans.tx_data[0] = command;
    trans.tx_data[1] = 0;
    trans.tx_data[2] = 0;

    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(display->touch_spi, &trans), TAG,
                        "touch transfer failed");
    *out_value = (uint16_t)((((uint16_t)trans.rx_data[1] << 8) | trans.rx_data[2]) >> 3);
    return ESP_OK;
}

static esp_err_t display_touch_read_axis_locked(display_handle_t *display, uint8_t command,
                                                uint16_t *out_value)
{
    uint16_t sample = 0;
    for (int i = 0; i < DISPLAY_TOUCH_AXIS_SAMPLES; ++i) {
        ESP_RETURN_ON_ERROR(display_touch_transfer24_locked(display, command, &sample), TAG,
                            "axis sample failed");
    }
    *out_value = sample;
    return ESP_OK;
}

static esp_err_t display_touch_read_raw_xy_once(display_handle_t *display, uint16_t *x, uint16_t *y)
{
    esp_err_t ret = spi_device_acquire_bus(display->touch_spi, portMAX_DELAY);
    ESP_RETURN_ON_ERROR(ret, TAG, "acquire touch bus failed");

    ret = display_touch_read_axis_locked(display, XPT2046_CMD_Y_POSITION, x);
    if (ret == ESP_OK) {
        ret = display_touch_read_axis_locked(display, XPT2046_CMD_X_POSITION, y);
    }

    spi_device_release_bus(display->touch_spi);
    return ret;
}

static esp_err_t display_touch_read_raw_z_once(display_handle_t *display, uint16_t *z)
{
    uint16_t z1 = 0;
    uint16_t z2 = 0;
    esp_err_t ret = spi_device_acquire_bus(display->touch_spi, portMAX_DELAY);
    ESP_RETURN_ON_ERROR(ret, TAG, "acquire touch bus failed");

    ret = display_touch_transfer24_locked(display, XPT2046_CMD_Z1, &z1);
    if (ret == ESP_OK) {
        ret = display_touch_transfer24_locked(display, XPT2046_CMD_Z2, &z2);
    }

    spi_device_release_bus(display->touch_spi);
    ESP_RETURN_ON_ERROR(ret, TAG, "z sample failed");

    int32_t pressure = 4095 + (int32_t)z1 - (int32_t)z2;
    if (pressure == 4095) {
        pressure = 0;
    }
    if (pressure < 0) {
        pressure = 0;
    }
    if (pressure > 4095) {
        pressure = 4095;
    }
    *z = (uint16_t)pressure;
    return ESP_OK;
}

static esp_err_t display_touch_valid_sample(display_handle_t *display, uint16_t threshold,
                                            uint16_t raw_error, uint16_t *x, uint16_t *y,
                                            uint16_t *z, bool *touched)
{
    uint16_t x0 = 0;
    uint16_t y0 = 0;
    uint16_t x1 = 0;
    uint16_t y1 = 0;
    uint16_t z_prev = 0;
    uint16_t z_cur = 1;

    *touched = false;

    for (int i = 0; i < DISPLAY_TOUCH_SETTLE_RETRIES && (z_cur > z_prev); ++i) {
        z_prev = z_cur;
        ESP_RETURN_ON_ERROR(display_touch_read_raw_z_once(display, &z_cur), TAG,
                            "read touch pressure failed");
        esp_rom_delay_us(1000);
    }

    if (z_cur <= threshold) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(display_touch_read_raw_xy_once(display, &x0, &y0), TAG,
                        "read raw touch failed");

    esp_rom_delay_us(1000);
    ESP_RETURN_ON_ERROR(display_touch_read_raw_z_once(display, &z_prev), TAG,
                        "re-read touch pressure failed");
    if (z_prev <= threshold) {
        return ESP_OK;
    }

    esp_rom_delay_us(2000);
    ESP_RETURN_ON_ERROR(display_touch_read_raw_xy_once(display, &x1, &y1), TAG,
                        "read raw touch confirmation failed");

    if ((x0 > x1 ? (x0 - x1) : (x1 - x0)) > raw_error) {
        return ESP_OK;
    }
    if ((y0 > y1 ? (y0 - y1) : (y1 - y0)) > raw_error) {
        return ESP_OK;
    }

    *x = x0;
    *y = y0;
    *z = z_cur;
    *touched = true;
    return ESP_OK;
}

static bool display_touch_solve_3x3(double matrix[3][4], double out[3])
{
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 3; ++row) {
            if (display_touch_absd(matrix[row][col]) > display_touch_absd(matrix[pivot][col])) {
                pivot = row;
            }
        }

        if (display_touch_absd(matrix[pivot][col]) < 1e-9) {
            return false;
        }

        if (pivot != col) {
            for (int k = col; k < 4; ++k) {
                double tmp = matrix[col][k];
                matrix[col][k] = matrix[pivot][k];
                matrix[pivot][k] = tmp;
            }
        }

        const double pivot_value = matrix[col][col];
        for (int k = col; k < 4; ++k) {
            matrix[col][k] /= pivot_value;
        }

        for (int row = 0; row < 3; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = matrix[row][col];
            for (int k = col; k < 4; ++k) {
                matrix[row][k] -= factor * matrix[col][k];
            }
        }
    }

    out[0] = matrix[0][3];
    out[1] = matrix[1][3];
    out[2] = matrix[2][3];
    return true;
}

static esp_err_t display_touch_fit_affine(const display_point_t raw_points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT],
                                          const display_point_t base_points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT],
                                          display_touch_calibration_t *out_calibration)
{
    double sum_xx = 0.0;
    double sum_xy = 0.0;
    double sum_yy = 0.0;
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_tx = 0.0;
    double sum_ty = 0.0;
    double sum_xtx = 0.0;
    double sum_ytx = 0.0;
    double sum_xty = 0.0;
    double sum_yty = 0.0;
    double coeff_x[3];
    double coeff_y[3];

    for (int i = 0; i < DISPLAY_TOUCH_CALIBRATION_POINT_COUNT; ++i) {
        const double xr = raw_points[i].x;
        const double yr = raw_points[i].y;
        const double tx = base_points[i].x;
        const double ty = base_points[i].y;

        sum_xx += xr * xr;
        sum_xy += xr * yr;
        sum_yy += yr * yr;
        sum_x += xr;
        sum_y += yr;
        sum_tx += tx;
        sum_ty += ty;
        sum_xtx += xr * tx;
        sum_ytx += yr * tx;
        sum_xty += xr * ty;
        sum_yty += yr * ty;
    }

    double matrix_x[3][4] = {
        {sum_xx, sum_xy, sum_x, sum_xtx},
        {sum_xy, sum_yy, sum_y, sum_ytx},
        {sum_x, sum_y, (double)DISPLAY_TOUCH_CALIBRATION_POINT_COUNT, sum_tx},
    };
    double matrix_y[3][4] = {
        {sum_xx, sum_xy, sum_x, sum_xty},
        {sum_xy, sum_yy, sum_y, sum_yty},
        {sum_x, sum_y, (double)DISPLAY_TOUCH_CALIBRATION_POINT_COUNT, sum_ty},
    };

    ESP_RETURN_ON_FALSE(display_touch_solve_3x3(matrix_x, coeff_x), ESP_ERR_INVALID_STATE, TAG,
                        "solve x affine failed");
    ESP_RETURN_ON_FALSE(display_touch_solve_3x3(matrix_y, coeff_y), ESP_ERR_INVALID_STATE, TAG,
                        "solve y affine failed");

    const double scale = 65536.0;
    out_calibration->x_a_q16 = (int32_t)((coeff_x[0] * scale) + ((coeff_x[0] >= 0.0) ? 0.5 : -0.5));
    out_calibration->x_b_q16 = (int32_t)((coeff_x[1] * scale) + ((coeff_x[1] >= 0.0) ? 0.5 : -0.5));
    out_calibration->x_c_q16 = (int32_t)((coeff_x[2] * scale) + ((coeff_x[2] >= 0.0) ? 0.5 : -0.5));
    out_calibration->y_a_q16 = (int32_t)((coeff_y[0] * scale) + ((coeff_y[0] >= 0.0) ? 0.5 : -0.5));
    out_calibration->y_b_q16 = (int32_t)((coeff_y[1] * scale) + ((coeff_y[1] >= 0.0) ? 0.5 : -0.5));
    out_calibration->y_c_q16 = (int32_t)((coeff_y[2] * scale) + ((coeff_y[2] >= 0.0) ? 0.5 : -0.5));
    out_calibration->z_threshold = DISPLAY_TOUCH_DEFAULT_THRESHOLD;
    out_calibration->raw_error = DISPLAY_TOUCH_DEFAULT_RAW_ERROR;
    out_calibration->valid = true;
    return ESP_OK;
}

static void display_touch_apply_calibration_to_base(const display_touch_calibration_t *calibration,
                                                    uint16_t raw_x, uint16_t raw_y, int *base_x,
                                                    int *base_y)
{
    const int64_t x_q16 = ((int64_t)calibration->x_a_q16 * raw_x) +
                          ((int64_t)calibration->x_b_q16 * raw_y) +
                          calibration->x_c_q16;
    const int64_t y_q16 = ((int64_t)calibration->y_a_q16 * raw_x) +
                          ((int64_t)calibration->y_b_q16 * raw_y) +
                          calibration->y_c_q16;

    *base_x = (int)((x_q16 + ((x_q16 >= 0) ? 0x8000 : -0x8000)) >> 16);
    *base_y = (int)((y_q16 + ((y_q16 >= 0) ? 0x8000 : -0x8000)) >> 16);
}

bool display_touch_is_available(const display_handle_t *display)
{
    return display && display->touch_initialized;
}

esp_err_t display_touch_read_raw(display_handle_t *display, display_touch_data_t *out_data)
{
    bool touched = false;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t z = 0;
    uint16_t threshold = 0;

    ESP_RETURN_ON_FALSE(display && out_data, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(display->touch_initialized, ESP_ERR_NOT_SUPPORTED, TAG,
                        "touch is not configured");

    memset(out_data, 0, sizeof(*out_data));

    threshold = display_touch_threshold(display);
    if (display->touch_press_deadline_us > esp_timer_get_time()) {
        threshold = DISPLAY_TOUCH_MIN_THRESHOLD;
    }

    ESP_RETURN_ON_ERROR(display_touch_valid_sample(display, threshold, display_touch_raw_error(display),
                                                   &x, &y, &z, &touched),
                        TAG, "raw touch read failed");

    out_data->touched = touched;
    out_data->x = x;
    out_data->y = y;
    out_data->z = z;
    out_data->quality = touched ? 1 : 0;
    return ESP_OK;
}

esp_err_t display_touch_read(display_handle_t *display, display_touch_data_t *out_data)
{
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    uint16_t raw_z = 0;
    uint16_t threshold = 0;
    uint8_t valid = 0;
    int base_x = 0;
    int base_y = 0;
    int logical_x = 0;
    int logical_y = 0;

    ESP_RETURN_ON_FALSE(display && out_data, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(display->touch_initialized, ESP_ERR_NOT_SUPPORTED, TAG,
                        "touch is not configured");
    ESP_RETURN_ON_FALSE(display->touch_calibration.valid, ESP_ERR_INVALID_STATE, TAG,
                        "touch calibration is not set");

    memset(out_data, 0, sizeof(*out_data));

    threshold = display_touch_threshold(display);
    if (display->touch_press_deadline_us > esp_timer_get_time()) {
        threshold = DISPLAY_TOUCH_MIN_THRESHOLD;
    }

    for (int i = 0; i < DISPLAY_TOUCH_VALID_SAMPLES; ++i) {
        bool touched = false;
        uint16_t sample_x = 0;
        uint16_t sample_y = 0;
        uint16_t sample_z = 0;

        ESP_RETURN_ON_ERROR(display_touch_valid_sample(display, threshold,
                                                       display_touch_raw_error(display),
                                                       &sample_x, &sample_y, &sample_z, &touched),
                            TAG, "touch validation failed");
        if (touched) {
            raw_x = sample_x;
            raw_y = sample_y;
            raw_z = sample_z;
            ++valid;
        }
    }

    if (valid == 0) {
        display->touch_press_deadline_us = 0;
        return ESP_OK;
    }

    display_touch_apply_calibration_to_base(&display->touch_calibration, raw_x, raw_y, &base_x,
                                            &base_y);

    if (base_x < 0 || base_y < 0 || base_x >= display->config.width ||
        base_y >= display->config.height) {
        display->touch_press_deadline_us = 0;
        return ESP_OK;
    }

    display_touch_base_to_logical(display, base_x, base_y, &logical_x, &logical_y);
    if (logical_x < 0 || logical_y < 0 || logical_x >= display->current_width ||
        logical_y >= display->current_height) {
        display->touch_press_deadline_us = 0;
        return ESP_OK;
    }

    out_data->touched = true;
    out_data->x = (uint16_t)logical_x;
    out_data->y = (uint16_t)logical_y;
    out_data->z = raw_z;
    out_data->quality = valid;
    display->touch_press_deadline_us = esp_timer_get_time() + DISPLAY_TOUCH_HOLD_US;
    return ESP_OK;
}

esp_err_t display_touch_get_calibration_points(const display_handle_t *display, int inset,
                                               display_point_t points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT])
{
    int margin = inset;

    ESP_RETURN_ON_FALSE(display && points, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    if (margin <= 0) {
        margin = DISPLAY_TOUCH_DEFAULT_CAL_INSET;
    }
    if (margin > (display->current_width / 3)) {
        margin = display->current_width / 3;
    }
    if (margin > (display->current_height / 3)) {
        margin = display->current_height / 3;
    }
    if (margin < 8) {
        margin = 8;
    }

    points[0] = (display_point_t){.x = margin, .y = margin};
    points[1] = (display_point_t){.x = display->current_width - 1 - margin, .y = margin};
    points[2] = (display_point_t){.x = display->current_width - 1 - margin,
                                  .y = display->current_height - 1 - margin};
    points[3] = (display_point_t){.x = margin, .y = display->current_height - 1 - margin};
    points[4] = (display_point_t){.x = display->current_width / 2, .y = display->current_height / 2};
    return ESP_OK;
}

esp_err_t display_touch_calibrate(display_handle_t *display,
                                  const display_point_t raw_points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT],
                                  const display_point_t logical_points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT],
                                  display_touch_calibration_t *out_calibration)
{
    display_point_t base_points[DISPLAY_TOUCH_CALIBRATION_POINT_COUNT];

    ESP_RETURN_ON_FALSE(display && raw_points && logical_points && out_calibration,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    for (int i = 0; i < DISPLAY_TOUCH_CALIBRATION_POINT_COUNT; ++i) {
        display_touch_logical_to_base(display, logical_points[i].x, logical_points[i].y,
                                      &base_points[i].x, &base_points[i].y);
    }

    ESP_RETURN_ON_ERROR(display_touch_fit_affine(raw_points, base_points, out_calibration), TAG,
                        "fit touch calibration failed");
    return ESP_OK;
}

esp_err_t display_touch_set_calibration(display_handle_t *display,
                                        const display_touch_calibration_t *calibration)
{
    ESP_RETURN_ON_FALSE(display && calibration, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    display->touch_calibration = *calibration;
    if (display->touch_calibration.z_threshold == 0) {
        display->touch_calibration.z_threshold = DISPLAY_TOUCH_DEFAULT_THRESHOLD;
    }
    if (display->touch_calibration.raw_error == 0) {
        display->touch_calibration.raw_error = DISPLAY_TOUCH_DEFAULT_RAW_ERROR;
    }
    return ESP_OK;
}

esp_err_t display_touch_get_calibration(const display_handle_t *display,
                                        display_touch_calibration_t *out_calibration)
{
    ESP_RETURN_ON_FALSE(display && out_calibration, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    *out_calibration = display->touch_calibration;
    return ESP_OK;
}
