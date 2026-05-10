#include "live2d_pgl_app.h"

#include <cstring>
#include <esp_heap_caps.h>

#include "LAppDelegate.hpp"
#include "LAppPerformanceMonitor.hpp"
#include "live2d_pgl_host_internal.hpp"

namespace {
struct Live2DPglRuntimeState
{
    live2d_pgl_host_callbacks_t HostCallbacks;
    bool HostConfigured;
    bool Initialized;
    void* ReservedMainFramebufferStorage;
    std::size_t ReservedMainFramebufferStorageBytes;
};

Live2DPglRuntimeState& GetRuntimeState()
{
    static Live2DPglRuntimeState state = {};
    return state;
}

void ShutdownDelegate()
{
    Live2DPglRuntimeState& state = GetRuntimeState();
    if (!state.Initialized)
    {
        return;
    }

    LAppPerformanceMonitor::GetInstance().Flush();
    LAppDelegate* delegate = LAppDelegate::GetInstance();
    if (delegate != NULL)
    {
        delegate->Release();
    }
    LAppDelegate::ReleaseInstance();
    state.Initialized = false;
}
}

const live2d_pgl_host_callbacks_t* Live2DPglHost::GetCallbacks()
{
    const Live2DPglRuntimeState& state = GetRuntimeState();
    return state.HostConfigured ? &state.HostCallbacks : NULL;
}

bool Live2DPglHost::IsConfigured()
{
    return GetCallbacks() != NULL;
}

bool Live2DPglHost::GetSize(int& width, int& height)
{
    const live2d_pgl_host_callbacks_t* callbacks = GetCallbacks();
    return callbacks != NULL
        && callbacks->get_size != NULL
        && callbacks->get_size(callbacks->user_ctx, &width, &height);
}

bool Live2DPglHost::Present(int width, int height, const void* pixels, std::size_t strideBytes)
{
    const live2d_pgl_host_callbacks_t* callbacks = GetCallbacks();
    return callbacks != NULL
        && callbacks->present_rgba8888 != NULL
        && callbacks->present_rgba8888(callbacks->user_ctx, width, height, pixels, strideBytes);
}

bool Live2DPglHost::PollPointer(bool& pressed, float& x, float& y)
{
    const live2d_pgl_host_callbacks_t* callbacks = GetCallbacks();
    return callbacks != NULL
        && callbacks->poll_pointer != NULL
        && callbacks->poll_pointer(callbacks->user_ctx, &pressed, &x, &y);
}

bool Live2DPglHost::ReserveMainFramebufferStorage(std::size_t bytes)
{
    Live2DPglRuntimeState& state = GetRuntimeState();
    ReleaseReservedMainFramebufferStorage();
    if (bytes == 0)
    {
        return false;
    }

    void* ptr = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr == NULL)
    {
        return false;
    }

    state.ReservedMainFramebufferStorage = ptr;
    state.ReservedMainFramebufferStorageBytes = bytes;
    return true;
}

void Live2DPglHost::ReleaseReservedMainFramebufferStorage()
{
    Live2DPglRuntimeState& state = GetRuntimeState();
    if (state.ReservedMainFramebufferStorage != NULL)
    {
        heap_caps_free(state.ReservedMainFramebufferStorage);
        state.ReservedMainFramebufferStorage = NULL;
        state.ReservedMainFramebufferStorageBytes = 0;
    }
}

void* Live2DPglHost::TryTakeReservedMainFramebufferStorage(std::size_t bytes)
{
    Live2DPglRuntimeState& state = GetRuntimeState();
    if (state.ReservedMainFramebufferStorage != NULL &&
        state.ReservedMainFramebufferStorageBytes == bytes)
    {
        void* ptr = state.ReservedMainFramebufferStorage;
        state.ReservedMainFramebufferStorage = NULL;
        state.ReservedMainFramebufferStorageBytes = 0;
        return ptr;
    }

    return NULL;
}

extern "C" esp_err_t live2d_pgl_set_host_callbacks(const live2d_pgl_host_callbacks_t* callbacks)
{
    if (callbacks == NULL || callbacks->get_size == NULL || callbacks->present_rgba8888 == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    Live2DPglRuntimeState& state = GetRuntimeState();
    state.HostCallbacks = *callbacks;
    state.HostConfigured = true;
    return ESP_OK;
}

extern "C" void live2d_pgl_clear_host_callbacks(void)
{
    Live2DPglRuntimeState& state = GetRuntimeState();
    std::memset(&state.HostCallbacks, 0, sizeof(state.HostCallbacks));
    state.HostConfigured = false;
}

extern "C" bool live2d_pgl_initialize(void)
{
    Live2DPglRuntimeState& state = GetRuntimeState();
    if (state.Initialized)
    {
        return true;
    }

    if (!state.HostConfigured)
    {
        return false;
    }

    LAppDelegate* delegate = LAppDelegate::GetInstance();
    if (delegate == NULL || !delegate->Initialize())
    {
        LAppDelegate::ReleaseInstance();
        return false;
    }

    state.Initialized = true;
    return true;
}

extern "C" bool live2d_pgl_tick(void)
{
    Live2DPglRuntimeState& state = GetRuntimeState();
    if (!state.Initialized)
    {
        return false;
    }

    LAppDelegate* delegate = LAppDelegate::GetInstance();
    if (delegate == NULL || !delegate->Tick())
    {
        ShutdownDelegate();
        return false;
    }

    return true;
}

extern "C" void live2d_pgl_run(void)
{
    Live2DPglRuntimeState& state = GetRuntimeState();
    if (!state.Initialized)
    {
        return;
    }

    LAppDelegate* delegate = LAppDelegate::GetInstance();
    if (delegate == NULL)
    {
        ShutdownDelegate();
        return;
    }

    delegate->Run();
    state.Initialized = false;
}

extern "C" void live2d_pgl_shutdown(void)
{
    ShutdownDelegate();
}

extern "C" bool live2d_pgl_is_initialized(void)
{
    return GetRuntimeState().Initialized;
}

extern "C" bool live2d_pgl_reserve_main_framebuffer_storage(size_t bytes)
{
    return Live2DPglHost::ReserveMainFramebufferStorage(bytes);
}

extern "C" void live2d_pgl_release_main_framebuffer_storage(void)
{
    Live2DPglHost::ReleaseReservedMainFramebufferStorage();
}
