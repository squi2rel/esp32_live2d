#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*live2d_pgl_host_get_size_fn)(void *user_ctx, int *width, int *height);
typedef bool (*live2d_pgl_host_present_rgba8888_fn)(
    void *user_ctx,
    int width,
    int height,
    const void *pixels,
    size_t stride_bytes);
typedef bool (*live2d_pgl_host_poll_pointer_fn)(
    void *user_ctx,
    bool *pressed,
    float *x,
    float *y);

typedef struct live2d_pgl_host_callbacks_t
{
    void *user_ctx;
    live2d_pgl_host_get_size_fn get_size;
    live2d_pgl_host_present_rgba8888_fn present_rgba8888;
    live2d_pgl_host_poll_pointer_fn poll_pointer;
} live2d_pgl_host_callbacks_t;

esp_err_t live2d_pgl_set_host_callbacks(const live2d_pgl_host_callbacks_t *callbacks);
void live2d_pgl_clear_host_callbacks(void);

bool live2d_pgl_initialize(void);
bool live2d_pgl_tick(void);
void live2d_pgl_run(void);
void live2d_pgl_shutdown(void);
bool live2d_pgl_is_initialized(void);
bool live2d_pgl_reserve_main_framebuffer_storage(size_t bytes);
void live2d_pgl_release_main_framebuffer_storage(void);

#ifdef __cplusplus
}
#endif
