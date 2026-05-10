#include "LAppX11Window.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(CSM_TARGET_ESP_PGL)
#include <X11/Xatom.h>
#else
#include "sdkconfig.h"
#include <esp_heap_caps.h>
#include <soc/soc_caps.h>
#endif

#include "LAppDelegate.hpp"
#include "LAppPerformanceMonitor.hpp"
#if defined(CSM_TARGET_ESP_PGL)
#include "live2d_pgl_host_internal.hpp"
#endif

using namespace Live2D::Cubism::Framework::Rendering;

#if !defined(CSM_TARGET_ESP_PGL)
namespace {
constexpr long kEventMask =
    StructureNotifyMask |
    ButtonPressMask |
    ButtonReleaseMask |
    PointerMotionMask |
    FocusChangeMask |
    ExposureMask;
}
#else
namespace {
pix_t* AllocateBackBuffer(std::size_t bufferSize)
{
    if (void* reserved = Live2DPglHost::TryTakeReservedMainFramebufferStorage(bufferSize))
    {
        return static_cast<pix_t*>(reserved);
    }

    pix_t* buffer = static_cast<pix_t*>(heap_caps_aligned_alloc(16, bufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buffer != NULL)
    {
        return buffer;
    }

#if SOC_SPIRAM_SUPPORTED
    return static_cast<pix_t*>(heap_caps_aligned_alloc(16, bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    return NULL;
#endif
}
}
#endif

LAppX11Window::LAppX11Window()
#if !defined(CSM_TARGET_ESP_PGL)
    : _display(NULL)
    , _screen(0)
    , _window(0)
    , _graphicsContext(0)
    , _wmDeleteWindow(0)
    , _visualInfo()
    , _windowImage(NULL)
#else
    : _hostPointerPressed(false)
#endif
    , _context()
    , _backBuffer(NULL)
    , _contextInitialized(false)
    , _shouldClose(false)
    , _width(0)
    , _height(0)
#if !defined(CSM_TARGET_ESP_PGL)
    , _redMask()
    , _greenMask()
    , _blueMask()
#endif
{
    std::memset(&_context, 0, sizeof(_context));
}

LAppX11Window::~LAppX11Window()
{
    Shutdown();
}

bool LAppX11Window::Initialize(int width, int height, const char* title)
{
#if !defined(CSM_TARGET_ESP_PGL)
    _display = XOpenDisplay(NULL);
    if (_display == NULL)
    {
        return false;
    }

    _screen = DefaultScreen(_display);
    if (!XMatchVisualInfo(_display, _screen, 24, TrueColor, &_visualInfo))
    {
        return false;
    }

    XSetWindowAttributes attributes;
    attributes.background_pixel = 0;
    attributes.colormap = XCreateColormap(_display, RootWindow(_display, _screen), _visualInfo.visual, AllocNone);
    attributes.event_mask = kEventMask;

    _window = XCreateWindow(
        _display,
        RootWindow(_display, _screen),
        0,
        0,
        width,
        height,
        0,
        _visualInfo.depth,
        InputOutput,
        _visualInfo.visual,
        CWBackPixel | CWColormap | CWEventMask,
        &attributes
    );

    if (_window == 0)
    {
        return false;
    }

    _graphicsContext = DefaultGC(_display, _screen);
    _wmDeleteWindow = XInternAtom(_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(_display, _window, &_wmDeleteWindow, 1);
    XStoreName(_display, _window, title);
    XMapWindow(_display, _window);
    XFlush(_display);

    _redMask = BuildMaskInfo(_visualInfo.red_mask);
    _greenMask = BuildMaskInfo(_visualInfo.green_mask);
    _blueMask = BuildMaskInfo(_visualInfo.blue_mask);

    Resize(width, height);
#else
    (void)title;
    int hostWidth = width;
    int hostHeight = height;
    if (!Live2DPglHost::GetSize(hostWidth, hostHeight) || hostWidth <= 0 || hostHeight <= 0)
    {
        return false;
    }

    Resize(hostWidth, hostHeight);
#endif
    return !_shouldClose;
}

void LAppX11Window::Shutdown()
{
#if !defined(CSM_TARGET_ESP_PGL)
    if (_windowImage != NULL)
    {
        XDestroyImage(_windowImage);
        _windowImage = NULL;
    }
#endif

    if (_contextInitialized)
    {
        free_glContext(&_context);
        _contextInitialized = false;
    }

    if (_backBuffer != NULL)
    {
        std::free(_backBuffer);
        _backBuffer = NULL;
    }

#if !defined(CSM_TARGET_ESP_PGL)
    if (_window != 0 && _display != NULL)
    {
        XDestroyWindow(_display, _window);
        _window = 0;
    }

    if (_display != NULL)
    {
        XCloseDisplay(_display);
        _display = NULL;
    }
#endif

    _shouldClose = true;
}

void LAppX11Window::PollEvents(LAppDelegate* delegate)
{
#if !defined(CSM_TARGET_ESP_PGL)
    if (_display == NULL)
    {
        return;
    }

    XEvent event;
    while (XPending(_display) > 0)
    {
        XNextEvent(_display, &event);
        switch (event.type)
        {
        case ButtonPress:
            if (delegate != NULL && event.xbutton.button == Button1)
            {
                delegate->OnMouseMove(static_cast<float>(event.xbutton.x), static_cast<float>(event.xbutton.y));
                delegate->OnMouseButton(true);
            }
            break;
        case ButtonRelease:
            if (delegate != NULL && event.xbutton.button == Button1)
            {
                delegate->OnMouseMove(static_cast<float>(event.xbutton.x), static_cast<float>(event.xbutton.y));
                delegate->OnMouseButton(false);
            }
            break;
        case MotionNotify:
            if (delegate != NULL)
            {
                delegate->OnMouseMove(static_cast<float>(event.xmotion.x), static_cast<float>(event.xmotion.y));
            }
            break;
        case ConfigureNotify:
            if (event.xconfigure.width > 0 && event.xconfigure.height > 0
                && (event.xconfigure.width != _width || event.xconfigure.height != _height))
            {
                Resize(event.xconfigure.width, event.xconfigure.height);
            }
            break;
        case ClientMessage:
            if (static_cast<Atom>(event.xclient.data.l[0]) == _wmDeleteWindow)
            {
                _shouldClose = true;
            }
            break;
        case DestroyNotify:
            _shouldClose = true;
            break;
        default:
            break;
        }
    }
#else
    int hostWidth = _width;
    int hostHeight = _height;
    if (Live2DPglHost::GetSize(hostWidth, hostHeight)
        && hostWidth > 0
        && hostHeight > 0
        && (hostWidth != _width || hostHeight != _height))
    {
        Resize(hostWidth, hostHeight);
    }

    bool pressed = false;
    float x = 0.0f;
    float y = 0.0f;
    if (delegate != NULL && Live2DPglHost::PollPointer(pressed, x, y))
    {
        delegate->OnMouseMove(x, y);
        if (pressed != _hostPointerPressed)
        {
            delegate->OnMouseButton(pressed);
            _hostPointerPressed = pressed;
        }
    }
#endif
}

void LAppX11Window::Present()
{
#if !defined(CSM_TARGET_ESP_PGL)
    if (_windowImage == NULL || _backBuffer == NULL || _display == NULL)
    {
        return;
    }

    const LAppPerformanceMonitor::TimePoint presentStart = LAppPerformanceMonitor::Now();
    const LAppPerformanceMonitor::TimePoint copyStart = LAppPerformanceMonitor::Now();
    const int bytesPerPixel = _windowImage->bits_per_pixel / 8;
    for (int y = 0; y < _height; ++y)
    {
        const std::uint32_t* srcRow = reinterpret_cast<const std::uint32_t*>(_backBuffer) + static_cast<size_t>(y) * _width;
        unsigned char* dstRow = reinterpret_cast<unsigned char*>(_windowImage->data) + static_cast<size_t>(y) * _windowImage->bytes_per_line;

        for (int x = 0; x < _width; ++x)
        {
            const std::uint32_t pixel = srcRow[x];
            const std::uint8_t r = static_cast<std::uint8_t>(pixel & 0xFFu);
            const std::uint8_t g = static_cast<std::uint8_t>((pixel >> 8) & 0xFFu);
            const std::uint8_t b = static_cast<std::uint8_t>((pixel >> 16) & 0xFFu);
            const unsigned long packed = PackPixel(r, g, b);

            unsigned char* dstPixel = dstRow + static_cast<size_t>(x) * bytesPerPixel;
            for (int i = 0; i < bytesPerPixel; ++i)
            {
                dstPixel[i] = static_cast<unsigned char>((packed >> (i * 8)) & 0xFFu);
            }
        }
    }
    LAppPerformanceMonitor::GetInstance().AddPresentCopyTime(LAppPerformanceMonitor::DurationMs(copyStart));

    const LAppPerformanceMonitor::TimePoint flushStart = LAppPerformanceMonitor::Now();
    XPutImage(_display, _window, _graphicsContext, _windowImage, 0, 0, 0, 0, _width, _height);
    XFlush(_display);
    LAppPerformanceMonitor::GetInstance().AddPresentFlushTime(LAppPerformanceMonitor::DurationMs(flushStart));
    LAppPerformanceMonitor::GetInstance().AddPresentTime(LAppPerformanceMonitor::DurationMs(presentStart));

    static const bool debugPresent = (std::getenv("LAPP_PGL_DEBUG_PRESENT") != NULL);
    static int debugFrameCount = 0;
    if (debugPresent && debugFrameCount < 3)
    {
        const std::uint32_t* backBuffer = reinterpret_cast<const std::uint32_t*>(pglGetBackBuffer());
        if (backBuffer != NULL)
        {
            int nonBlackSamples = 0;
            const int stepY = (_height > 32) ? (_height / 32) : 1;
            const int stepX = (_width > 32) ? (_width / 32) : 1;

            for (int y = 0; y < _height; y += stepY)
            {
                for (int x = 0; x < _width; x += stepX)
                {
                    if (backBuffer[static_cast<size_t>(y) * _width + x] != 0)
                    {
                        ++nonBlackSamples;
                    }
                }
            }

            const std::uint32_t centerPixel = backBuffer[static_cast<size_t>(_height / 2) * _width + (_width / 2)];
            std::fprintf(stderr,
                "[PGL] frame=%d sampled_non_black=%d center=0x%08x size=%dx%d\n",
                debugFrameCount,
                nonBlackSamples,
                centerPixel,
                _width,
                _height);
        }

        ++debugFrameCount;
    }
#else
    if (_backBuffer == NULL || _width <= 0 || _height <= 0)
    {
        return;
    }

    const LAppPerformanceMonitor::TimePoint presentStart = LAppPerformanceMonitor::Now();
    LAppPerformanceMonitor::GetInstance().AddPresentCopyTime(0.0);

    const LAppPerformanceMonitor::TimePoint flushStart = LAppPerformanceMonitor::Now();
    const bool flushResult = Live2DPglHost::Present(
        _width,
        _height,
        _backBuffer,
        static_cast<size_t>(_width) * sizeof(pix_t));
    LAppPerformanceMonitor::GetInstance().AddPresentFlushTime(LAppPerformanceMonitor::DurationMs(flushStart));
    LAppPerformanceMonitor::GetInstance().AddPresentTime(LAppPerformanceMonitor::DurationMs(presentStart));

    if (!flushResult)
    {
        std::fprintf(stdout, "[APP]host present_rgba8888 failed.\n");
        std::fflush(stdout);
        _shouldClose = true;
    }
#endif
}

void LAppX11Window::GetSize(int& width, int& height) const
{
    width = _width;
    height = _height;
}

std::size_t LAppX11Window::GetBackBufferBytes() const
{
    return _backBuffer == NULL ? 0 :
        static_cast<std::size_t>(_width) * static_cast<std::size_t>(_height) * sizeof(pix_t);
}

std::size_t LAppX11Window::GetPresentBufferBytes() const
{
#if !defined(CSM_TARGET_ESP_PGL)
    return _windowImage == NULL ? 0 :
        static_cast<std::size_t>(_windowImage->bytes_per_line) * static_cast<std::size_t>(_windowImage->height);
#else
    return 0;
#endif
}

#if !defined(CSM_TARGET_ESP_PGL)
LAppX11Window::MaskInfo LAppX11Window::BuildMaskInfo(unsigned long mask)
{
    MaskInfo info = {mask, 0, 0};
    if (mask == 0)
    {
        return info;
    }

    while (((mask >> info.Shift) & 1UL) == 0UL)
    {
        ++info.Shift;
    }

    unsigned long shiftedMask = mask >> info.Shift;
    while ((shiftedMask & 1UL) != 0UL)
    {
        ++info.Bits;
        shiftedMask >>= 1;
    }

    return info;
}
#endif

bool LAppX11Window::ResizeBackBuffer(int width, int height)
{
    const size_t bufferSize = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(pix_t);

    if (!_contextInitialized)
    {
#if defined(CSM_TARGET_ESP_PGL)
        _backBuffer = AllocateBackBuffer(bufferSize);
#else
        _backBuffer = static_cast<pix_t*>(std::malloc(bufferSize));
#endif
        if (_backBuffer == NULL)
        {
            return false;
        }

        pix_t* backBuffer = _backBuffer;
        if (!init_glContext(&_context, &backBuffer, width, height))
        {
            return false;
        }

        _backBuffer = backBuffer;
        _contextInitialized = true;
    }
    else
    {
#if defined(CSM_TARGET_ESP_PGL)
        pix_t* resized = AllocateBackBuffer(bufferSize);
        if (resized == NULL)
        {
            return false;
        }

        std::free(_backBuffer);
        _backBuffer = resized;
        CubismPortableGLSetMainFramebuffer(_backBuffer, width, height, GL_TRUE);
        if (!pglResizeFramebuffer(width, height))
        {
            return false;
        }
#else
        pix_t* resized = static_cast<pix_t*>(std::realloc(_backBuffer, bufferSize));
        if (resized == NULL)
        {
            return false;
        }

        _backBuffer = resized;
        if (!pglResizeFramebuffer(width, height))
        {
            return false;
        }
#endif
    }

    CubismPortableGLSetMainFramebuffer(_backBuffer, width, height, GL_TRUE);
    glViewport(0, 0, width, height);
    return true;
}

#if !defined(CSM_TARGET_ESP_PGL)
bool LAppX11Window::RecreateWindowImage(int width, int height)
{
    if (_windowImage != NULL)
    {
        XDestroyImage(_windowImage);
        _windowImage = NULL;
    }

    char* pixels = static_cast<char*>(std::malloc(static_cast<size_t>(width) * height * 4));
    if (pixels == NULL)
    {
        return false;
    }

    _windowImage = XCreateImage(
        _display,
        _visualInfo.visual,
        _visualInfo.depth,
        ZPixmap,
        0,
        pixels,
        width,
        height,
        32,
        0
    );

    if (_windowImage == NULL)
    {
        std::free(pixels);
        return false;
    }

    return true;
}
#endif

void LAppX11Window::Resize(int width, int height)
{
#if !defined(CSM_TARGET_ESP_PGL)
    if (!ResizeBackBuffer(width, height) || !RecreateWindowImage(width, height))
#else
    if (!ResizeBackBuffer(width, height))
#endif
    {
        _shouldClose = true;
        return;
    }

    _width = width;
    _height = height;
}

#if !defined(CSM_TARGET_ESP_PGL)
unsigned long LAppX11Window::PackComponent(std::uint8_t value, const MaskInfo& mask) const
{
    if (mask.Mask == 0 || mask.Bits <= 0)
    {
        return 0;
    }

    const unsigned long maxValue = (1UL << mask.Bits) - 1UL;
    const unsigned long scaled = (static_cast<unsigned long>(value) * maxValue + 127UL) / 255UL;
    return (scaled << mask.Shift) & mask.Mask;
}

unsigned long LAppX11Window::PackPixel(std::uint8_t r, std::uint8_t g, std::uint8_t b) const
{
    return PackComponent(r, _redMask)
         | PackComponent(g, _greenMask)
         | PackComponent(b, _blueMask);
}
#endif
