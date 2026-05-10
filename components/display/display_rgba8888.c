#include <stddef.h>
#include <stdint.h>

#include "display_priv.h"

#include "esp_check.h"
#include "soc/soc_caps.h"

#define DISPLAY_RGBA_FLUSH_TIMEOUT_MS 2000

static const char *TAG = "display_rgba";

#if SOC_SIMD_INSTRUCTION_SUPPORTED
extern void display_rgba8888_to_rgb565_be_s3(uint16_t *dst, const uint32_t *src,
                                             size_t pixel_count, const uint32_t *masks);

static const uint32_t s_s3_masks[4] = {
    0x000000F8u,
    0x0000E000u,
    0x00001C00u,
    0x00F80000u,
};
#endif

static inline uint16_t display_rgba8888_pixel_to_bus(const uint8_t *pixel)
{
    const uint8_t r = pixel[0];
    const uint8_t g = pixel[1];
    const uint8_t b = pixel[2];
    const uint16_t hi = (uint16_t)((r & 0xF8u) | (g >> 5));
    const uint16_t lo = (uint16_t)(((g & 0x1Cu) << 3) | (b >> 3));
    return (uint16_t)(hi | (lo << 8));
}

static void display_rgba8888_to_rgb565_be_scalar(uint16_t *dst, const uint8_t *src, size_t pixel_count)
{
    for (size_t i = 0; i < pixel_count; ++i) {
        dst[i] = display_rgba8888_pixel_to_bus(src + (i * 4));
    }
}

static void display_rgba8888_to_rgb565_be_line(uint16_t *dst, const uint8_t *src, size_t pixel_count)
{
    if (pixel_count == 0) {
        return;
    }

    if (((uintptr_t)src & 0x3u) != 0u) {
        display_rgba8888_to_rgb565_be_scalar(dst, src, pixel_count);
        return;
    }

#if SOC_SIMD_INSTRUCTION_SUPPORTED
    const size_t align_bytes = ((size_t)16 - ((uintptr_t)src & 0xFu)) & 0xFu;
    size_t align_pixels = align_bytes >> 2;
    if (align_pixels > pixel_count) {
        align_pixels = pixel_count;
    }
    if (align_pixels > 0) {
        display_rgba8888_to_rgb565_be_scalar(dst, src, align_pixels);
        dst += align_pixels;
        src += align_pixels * 4;
        pixel_count -= align_pixels;
    }

    const size_t simd_pixels = pixel_count & ~((size_t)3);
    if (simd_pixels > 0) {
        display_rgba8888_to_rgb565_be_s3(dst, (const uint32_t *)src, simd_pixels, s_s3_masks);
        dst += simd_pixels;
        src += simd_pixels * 4;
        pixel_count -= simd_pixels;
    }
#endif

    if (pixel_count > 0) {
        display_rgba8888_to_rgb565_be_scalar(dst, src, pixel_count);
    }
}

static void display_convert_rgba8888_chunk(uint16_t *dst, const uint8_t *src_rgba8888,
                                           size_t src_stride_bytes, const display_area_t *area,
                                           int start_y, int chunk_lines)
{
    const int width = area->x2 - area->x1;
    for (int row = 0; row < chunk_lines; ++row) {
        const size_t src_offset = ((size_t)(start_y + row) * src_stride_bytes) + ((size_t)area->x1 * 4);
        const uint8_t *src_row = src_rgba8888 + src_offset;
        uint16_t *dst_row = dst + ((size_t)row * (size_t)width);
        display_rgba8888_to_rgb565_be_line(dst_row, src_row, (size_t)width);
    }
}

void display_convert_rgba8888_to_rgb565_be(void *dst_rgb565_be, const void *src_rgba8888,
                                           size_t pixel_count)
{
    if (dst_rgb565_be == NULL || src_rgba8888 == NULL || pixel_count == 0) {
        return;
    }

    display_rgba8888_to_rgb565_be_line((uint16_t *)dst_rgb565_be, (const uint8_t *)src_rgba8888,
                                       pixel_count);
}

esp_err_t display_flush_rgba8888(display_handle_t *display, const display_area_t *area,
                                 const void *src_rgba8888, size_t src_stride_bytes)
{
    ESP_RETURN_ON_FALSE(display && area && src_rgba8888, ESP_ERR_INVALID_ARG, TAG,
                        "display, area or src is null");
    ESP_RETURN_ON_FALSE(display->fill_buffer && display->rgba_buffer, ESP_ERR_INVALID_STATE, TAG,
                        "rgba staging buffers are not initialized");
    ESP_RETURN_ON_FALSE(area->x1 >= 0 && area->y1 >= 0 && area->x2 > area->x1 && area->y2 > area->y1,
                        ESP_ERR_INVALID_ARG, TAG, "invalid flush area");
    ESP_RETURN_ON_FALSE(area->x2 <= display->current_width && area->y2 <= display->current_height,
                        ESP_ERR_INVALID_ARG, TAG, "flush area exceeds current bounds");

    const size_t min_stride_bytes = (size_t)display->current_width * sizeof(uint32_t);
    ESP_RETURN_ON_FALSE(src_stride_bytes >= min_stride_bytes, ESP_ERR_INVALID_ARG, TAG,
                        "src stride is smaller than current framebuffer width");

    const uint8_t *src_bytes = src_rgba8888;
    const int width = area->x2 - area->x1;
    const int height = area->y2 - area->y1;
    const int max_chunk_lines = (int)(display->fill_buffer_pixels / (size_t)width);
    ESP_RETURN_ON_FALSE(max_chunk_lines > 0, ESP_ERR_INVALID_STATE, TAG,
                        "staging buffer is too small for the requested area");

    uint16_t *buffers[2] = {display->fill_buffer, display->rgba_buffer};
    int buffer_index = 0;
    int current_y = area->y1;
    int remaining_lines = height;
    int chunk_lines = (remaining_lines > max_chunk_lines) ? max_chunk_lines : remaining_lines;

    display_convert_rgba8888_chunk(buffers[buffer_index], src_bytes, src_stride_bytes, area, current_y,
                                   chunk_lines);

    display_area_t chunk_area = {
        .x1 = area->x1,
        .y1 = current_y,
        .x2 = area->x2,
        .y2 = current_y + chunk_lines,
    };
    ESP_RETURN_ON_ERROR(display_flush_async(display, &chunk_area, buffers[buffer_index], NULL, NULL), TAG,
                        "initial rgba flush failed");

    current_y += chunk_lines;
    remaining_lines -= chunk_lines;
    buffer_index ^= 1;

    while (remaining_lines > 0) {
        chunk_lines = (remaining_lines > max_chunk_lines) ? max_chunk_lines : remaining_lines;
        display_convert_rgba8888_chunk(buffers[buffer_index], src_bytes, src_stride_bytes, area, current_y,
                                       chunk_lines);

        ESP_RETURN_ON_ERROR(display_flush_wait(display, pdMS_TO_TICKS(DISPLAY_RGBA_FLUSH_TIMEOUT_MS)), TAG,
                            "wait for previous rgba flush failed");

        chunk_area.y1 = current_y;
        chunk_area.y2 = current_y + chunk_lines;
        ESP_RETURN_ON_ERROR(display_flush_async(display, &chunk_area, buffers[buffer_index], NULL, NULL),
                            TAG, "chunk rgba flush failed");

        current_y += chunk_lines;
        remaining_lines -= chunk_lines;
        buffer_index ^= 1;
    }

    return display_flush_wait(display, pdMS_TO_TICKS(DISPLAY_RGBA_FLUSH_TIMEOUT_MS));
}
