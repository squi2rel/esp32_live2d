#pragma once

#include <cstddef>

#include "live2d_pgl_app.h"

namespace Live2DPglHost
{
const live2d_pgl_host_callbacks_t* GetCallbacks();
bool IsConfigured();
bool GetSize(int& width, int& height);
bool Present(int width, int height, const void* pixels, std::size_t strideBytes);
bool PollPointer(bool& pressed, float& x, float& y);
bool ReserveMainFramebufferStorage(std::size_t bytes);
void ReleaseReservedMainFramebufferStorage();
void* TryTakeReservedMainFramebufferStorage(std::size_t bytes);
}
