#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <esp_log.h>
#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "sdkconfig.h"
#include "display.h"
#include "LAppDelegate.hpp"
#include "LAppView.hpp"
#include "live2d_pgl_app.h"

namespace {
const char* const Tag = "live2d_app";

struct Live2DHostContext
{
    display_handle_t* Display;
    int RenderWidth;
    int RenderHeight;
    int RenderOffsetX;
    int RenderOffsetY;
};

static void LogRenderStartMemorySnapshot(const char* tag)
{
    const std::size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const std::size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const std::size_t totalInternal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if SOC_SPIRAM_SUPPORTED
    const std::size_t freeSpiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const std::size_t largestSpiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const std::size_t totalSpiram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const std::size_t freeSpiram = 0;
    const std::size_t largestSpiram = 0;
    const std::size_t totalSpiram = 0;
#endif
    ESP_LOGI(tag,
             "Render start memory: internal used=%u free=%u largest=%u ; psram used=%u free=%u largest=%u",
             static_cast<unsigned>(totalInternal - freeInternal),
             static_cast<unsigned>(freeInternal),
             static_cast<unsigned>(largestInternal),
             static_cast<unsigned>(totalSpiram - freeSpiram),
             static_cast<unsigned>(freeSpiram),
             static_cast<unsigned>(largestSpiram));
}

constexpr int DisplayPresentQueueDepth = 2;
constexpr uint32_t DisplayPresentWorkerStackWords = 6144;

struct DisplayPresentFrameSlot
{
    uint32_t* Pixels;
    size_t CapacityBytes;
};

struct DisplayPresentMessage
{
    int SlotIndex;
    int Width;
    int Height;
    size_t StrideBytes;
};

struct DisplayPresentPipeline
{
    display_handle_t* Display;
    QueueHandle_t FreeSlots;
    QueueHandle_t ReadyFrames;
    TaskHandle_t WorkerTask;
    TaskHandle_t OwnerTask;
    DisplayPresentFrameSlot Slots[DisplayPresentQueueDepth];
    int FrameWidth;
    int FrameHeight;
    bool Initialized;
};

static void* AllocatePresentFrameStorage(size_t size)
{
#if SOC_SPIRAM_SUPPORTED
    void* ptr = heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL)
    {
        return ptr;
    }
#endif
    return heap_caps_aligned_alloc(16, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void FreePresentFrameStorage(void* ptr)
{
    if (ptr != NULL)
    {
        heap_caps_free(ptr);
    }
}

static void DisplayPresentWorker(void* arg)
{
    DisplayPresentPipeline* const pipeline = static_cast<DisplayPresentPipeline*>(arg);
    DisplayPresentMessage msg = {};

    while (xQueueReceive(pipeline->ReadyFrames, &msg, portMAX_DELAY) == pdTRUE)
    {
        if (msg.SlotIndex < 0)
        {
            break;
        }

        display_area_t area = {};
        area.x1 = 0;
        area.y1 = 0;
        area.x2 = pipeline->FrameWidth;
        area.y2 = pipeline->FrameHeight;

        (void)display_flush_rgba8888(
            pipeline->Display,
            &area,
            pipeline->Slots[msg.SlotIndex].Pixels,
            msg.StrideBytes);

        const int recycledSlot = msg.SlotIndex;
        (void)xQueueSend(pipeline->FreeSlots, &recycledSlot, portMAX_DELAY);
    }

    if (pipeline->OwnerTask != NULL)
    {
        xTaskNotifyGive(pipeline->OwnerTask);
    }

    vTaskDelete(NULL);
}

static bool InitializeDisplayPresentPipeline(DisplayPresentPipeline* pipeline, display_handle_t* display,
                                             int width, int height)
{
    if (pipeline == NULL || display == NULL || width <= 0 || height <= 0)
    {
        return false;
    }

    std::memset(pipeline, 0, sizeof(*pipeline));
    pipeline->Display = display;
    pipeline->FrameWidth = width;
    pipeline->FrameHeight = height;
    pipeline->OwnerTask = xTaskGetCurrentTaskHandle();

    const size_t frameBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint32_t);
    pipeline->FreeSlots = xQueueCreate(DisplayPresentQueueDepth, sizeof(int));
    pipeline->ReadyFrames = xQueueCreate(DisplayPresentQueueDepth, sizeof(DisplayPresentMessage));
    if (pipeline->FreeSlots == NULL || pipeline->ReadyFrames == NULL)
    {
        return false;
    }

    for (int i = 0; i < DisplayPresentQueueDepth; ++i)
    {
        pipeline->Slots[i].Pixels = static_cast<uint32_t*>(AllocatePresentFrameStorage(frameBytes));
        pipeline->Slots[i].CapacityBytes = pipeline->Slots[i].Pixels != NULL ? frameBytes : 0;
        if (pipeline->Slots[i].Pixels == NULL)
        {
            return false;
        }

        const int slotIndex = i;
        if (xQueueSend(pipeline->FreeSlots, &slotIndex, 0) != pdTRUE)
        {
            return false;
        }
    }

#if CONFIG_FREERTOS_UNICORE
    const BaseType_t core = tskNO_AFFINITY;
#else
    const BaseType_t core = 1;
#endif
    if (xTaskCreatePinnedToCore(DisplayPresentWorker,
                                "pgl_flush",
                                DisplayPresentWorkerStackWords,
                                pipeline,
                                tskIDLE_PRIORITY + 2,
                                &pipeline->WorkerTask,
                                core) != pdPASS)
    {
        return false;
    }

    pipeline->Initialized = true;
    return true;
}

static void ShutdownDisplayPresentPipeline(DisplayPresentPipeline* pipeline)
{
    if (pipeline == NULL)
    {
        return;
    }

    if (pipeline->WorkerTask != NULL)
    {
        DisplayPresentMessage stop = {};
        stop.SlotIndex = -1;
        (void)xQueueSend(pipeline->ReadyFrames, &stop, pdMS_TO_TICKS(1000));
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
        pipeline->WorkerTask = NULL;
    }

    if (pipeline->ReadyFrames != NULL)
    {
        vQueueDelete(pipeline->ReadyFrames);
        pipeline->ReadyFrames = NULL;
    }
    if (pipeline->FreeSlots != NULL)
    {
        vQueueDelete(pipeline->FreeSlots);
        pipeline->FreeSlots = NULL;
    }

    for (int i = 0; i < DisplayPresentQueueDepth; ++i)
    {
        FreePresentFrameStorage(pipeline->Slots[i].Pixels);
        pipeline->Slots[i].Pixels = NULL;
        pipeline->Slots[i].CapacityBytes = 0;
    }

    pipeline->Initialized = false;
}

DisplayPresentPipeline& GetDisplayPresentPipeline()
{
    static DisplayPresentPipeline pipeline = {};
    return pipeline;
}

constexpr float DefaultScale = 2.0f;//4.5f;
constexpr float DefaultTranslateX = 0.0f;
constexpr float DefaultTranslateY = -0.3f;//-0.6f;
constexpr int GlRenderWidth = 200;
constexpr int GlRenderHeight = 280;

display_config_t BuildDefaultDisplayConfig(int width, int height)
{
    display_config_t config = {};
    config.spi_host = SPI2_HOST;
    config.pin_cs = GPIO_NUM_10;
    config.pin_mosi = GPIO_NUM_11;
    config.pin_miso = GPIO_NUM_13;
    config.pin_sclk = GPIO_NUM_12;
    config.pin_dc = GPIO_NUM_7;
    config.pin_rst = GPIO_NUM_6;
    config.pin_bl = GPIO_NUM_NC;
    config.pin_touch_cs = GPIO_NUM_NC;
    config.pin_touch_irq = GPIO_NUM_NC;
    config.width = width;
    config.height = height;
    config.write_hz = 40000000;
    config.read_hz = 6000000;
    config.touch_hz = 0;
    config.max_transfer_sz = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint16_t);
    config.trans_queue_depth = 10;
    config.fill_buffer_lines = 40;
    config.rotation = DISPLAY_ROTATION_180;
    config.rgb_order = DISPLAY_RGB_ORDER_BGR;
    return config;
}

bool HostGetSize(void* userCtx, int* width, int* height)
{
    if (userCtx == NULL || width == NULL || height == NULL)
    {
        return false;
    }

    const Live2DHostContext* const host = static_cast<const Live2DHostContext*>(userCtx);
    if (host->Display == NULL)
    {
        return false;
    }

    *width = host->RenderWidth;
    *height = host->RenderHeight;
    return *width > 0 && *height > 0;
}

bool HostPresent(void* userCtx, int width, int height, const void* pixels, size_t strideBytes)
{
    if (userCtx == NULL || pixels == NULL || width <= 0 || height <= 0)
    {
        return false;
    }

    const Live2DHostContext* const host = static_cast<const Live2DHostContext*>(userCtx);
    if (host->Display == NULL)
    {
        return false;
    }

    DisplayPresentPipeline& pipeline = GetDisplayPresentPipeline();
    if (pipeline.Initialized)
    {
        int slotIndex = -1;
        if (xQueueReceive(pipeline.FreeSlots, &slotIndex, portMAX_DELAY) == pdTRUE
            && slotIndex >= 0
            && slotIndex < DisplayPresentQueueDepth)
        {
            uint8_t* const dstBytes = reinterpret_cast<uint8_t*>(pipeline.Slots[slotIndex].Pixels);
            const uint8_t* const srcBytes = static_cast<const uint8_t*>(pixels);
            const size_t rowBytes = static_cast<size_t>(width) * sizeof(uint32_t);
            const size_t fullRowBytes = static_cast<size_t>(pipeline.FrameWidth) * sizeof(uint32_t);
            std::memset(dstBytes, 0, pipeline.Slots[slotIndex].CapacityBytes);
            for (int y = 0; y < height; ++y)
            {
                std::memcpy(dstBytes + (static_cast<size_t>(host->RenderOffsetY + y) * fullRowBytes)
                                      + (static_cast<size_t>(host->RenderOffsetX) * sizeof(uint32_t)),
                            srcBytes + (static_cast<size_t>(y) * strideBytes),
                            rowBytes);
            }

            DisplayPresentMessage msg = {};
            msg.SlotIndex = slotIndex;
            msg.Width = width;
            msg.Height = height;
            msg.StrideBytes = fullRowBytes;
            if (xQueueSend(pipeline.ReadyFrames, &msg, portMAX_DELAY) == pdTRUE)
            {
                return true;
            }

            (void)xQueueSend(pipeline.FreeSlots, &slotIndex, portMAX_DELAY);
        }
    }

    const int fullWidth = display_get_width(host->Display);
    const int fullHeight = display_get_height(host->Display);
    const size_t fullRowBytes = static_cast<size_t>(fullWidth) * sizeof(uint32_t);
    const size_t fullBytes = fullRowBytes * static_cast<size_t>(fullHeight);
    uint32_t* composed = static_cast<uint32_t*>(heap_caps_aligned_alloc(16, fullBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#if SOC_SPIRAM_SUPPORTED
    if (composed == NULL)
    {
        composed = static_cast<uint32_t*>(heap_caps_aligned_alloc(16, fullBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
#endif
    if (composed == NULL)
    {
        return false;
    }

    std::memset(composed, 0, fullBytes);
    const uint8_t* const srcBytes = static_cast<const uint8_t*>(pixels);
    uint8_t* const dstBytes = reinterpret_cast<uint8_t*>(composed);
    const size_t rowBytes = static_cast<size_t>(width) * sizeof(uint32_t);
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(dstBytes + (static_cast<size_t>(host->RenderOffsetY + y) * fullRowBytes)
                              + (static_cast<size_t>(host->RenderOffsetX) * sizeof(uint32_t)),
                    srcBytes + (static_cast<size_t>(y) * strideBytes),
                    rowBytes);
    }

    display_area_t area = {};
    area.x1 = 0;
    area.y1 = 0;
    area.x2 = fullWidth;
    area.y2 = fullHeight;
    const bool ok = display_flush_rgba8888(host->Display, &area, composed, fullRowBytes) == ESP_OK;
    heap_caps_free(composed);
    return ok;
}

bool HostPollPointer(void* userCtx, bool* pressed, float* x, float* y)
{
    if (userCtx == NULL || pressed == NULL || x == NULL || y == NULL)
    {
        return false;
    }

    Live2DHostContext* const host = static_cast<Live2DHostContext*>(userCtx);
    if (host->Display == NULL || !display_touch_is_available(host->Display))
    {
        return false;
    }

    display_touch_data_t touch = {};
    if (display_touch_read(host->Display, &touch) != ESP_OK)
    {
        return false;
    }

    *pressed = touch.touched;
    if (!touch.touched)
    {
        *x = 0.0f;
        *y = 0.0f;
        return true;
    }

    const int localX = static_cast<int>(touch.x) - host->RenderOffsetX;
    const int localY = static_cast<int>(touch.y) - host->RenderOffsetY;
    if (localX < 0 || localY < 0 || localX >= host->RenderWidth || localY >= host->RenderHeight)
    {
        *pressed = false;
        *x = 0.0f;
        *y = 0.0f;
        return true;
    }

    *x = static_cast<float>(localX);
    *y = static_cast<float>(localY);
    return true;
}

}

extern "C" void app_main(void)
{
    LogRenderStartMemorySnapshot("boot_mem");
    const std::size_t mainFramebufferReserveBytes =
        static_cast<std::size_t>(GlRenderWidth) * static_cast<std::size_t>(GlRenderHeight) * sizeof(uint32_t);
    const bool mainFramebufferReserved = live2d_pgl_reserve_main_framebuffer_storage(mainFramebufferReserveBytes);
    ESP_LOGI(Tag,
             "Main framebuffer reserve: bytes=%u result=%s",
             static_cast<unsigned>(mainFramebufferReserveBytes),
             mainFramebufferReserved ? "ok" : "failed");

    Live2DHostContext host = {};
    display_config_t config = BuildDefaultDisplayConfig(240, 320);
    if (display_new(&config, &host.Display) != ESP_OK || host.Display == NULL)
    {
        ESP_LOGE(Tag, "Display initialization failed");
        live2d_pgl_release_main_framebuffer_storage();
        vTaskDelete(NULL);
        return;
    }

    host.RenderWidth = GlRenderWidth;
    host.RenderHeight = GlRenderHeight;
    host.RenderOffsetX = display_get_width(host.Display) - host.RenderWidth;
    host.RenderOffsetY = display_get_height(host.Display) - host.RenderHeight;

    if (host.RenderOffsetX < 0 || host.RenderOffsetY < 0)
    {
        ESP_LOGE(Tag, "Render surface %dx%d exceeds display %dx%d",
                 host.RenderWidth,
                 host.RenderHeight,
                 display_get_width(host.Display),
                 display_get_height(host.Display));
        display_delete(host.Display);
        live2d_pgl_release_main_framebuffer_storage();
        vTaskDelete(NULL);
        return;
    }

    DisplayPresentPipeline& presentPipeline = GetDisplayPresentPipeline();
    if (InitializeDisplayPresentPipeline(&presentPipeline,
                                         host.Display,
                                         display_get_width(host.Display),
                                         display_get_height(host.Display)))
    {
        ESP_LOGI(Tag, "Dual-core display present enabled: render=CPU0 flush=CPU1");
    }
    else
    {
        ESP_LOGW(Tag, "Dual-core display present unavailable; falling back to synchronous flush");
        ShutdownDisplayPresentPipeline(&presentPipeline);
    }

    live2d_pgl_host_callbacks_t callbacks = {};
    callbacks.user_ctx = &host;
    callbacks.get_size = HostGetSize;
    callbacks.present_rgba8888 = HostPresent;
    callbacks.poll_pointer = HostPollPointer;
    if (live2d_pgl_set_host_callbacks(&callbacks) != ESP_OK)
    {
        ESP_LOGE(Tag, "Live2D host callback registration failed");
        display_delete(host.Display);
        live2d_pgl_release_main_framebuffer_storage();
        vTaskDelete(NULL);
        return;
    }

    if (!live2d_pgl_initialize())
    {
        ESP_LOGE(Tag, "Live2D initialization failed");
        live2d_pgl_clear_host_callbacks();
        display_delete(host.Display);
        live2d_pgl_release_main_framebuffer_storage();
        vTaskDelete(NULL);
        return;
    }

    if (LAppDelegate::GetInstance() != NULL && LAppDelegate::GetInstance()->GetView() != NULL)
    {
        LAppDelegate::GetInstance()->GetView()->ApplyDebugViewTransform(
            DefaultScale,
            DefaultTranslateX,
            DefaultTranslateY);
    }

    LogRenderStartMemorySnapshot(Tag);
    ESP_LOGI(Tag, "Live2D PortableGL render loop started");
    while (live2d_pgl_tick())
    {
        vTaskDelay(1);
    }

    live2d_pgl_shutdown();
    live2d_pgl_clear_host_callbacks();
    ShutdownDisplayPresentPipeline(&GetDisplayPresentPipeline());
    display_delete(host.Display);
    live2d_pgl_release_main_framebuffer_storage();
    vTaskDelete(NULL);
}
