#pragma once

#if !defined(CSM_TARGET_ESP_PGL)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <cstddef>
#include <cstdint>

#include <Rendering/PortableGL/CubismPortableGL.hpp>

class LAppDelegate;

class LAppX11Window
{
public:
    LAppX11Window();
    ~LAppX11Window();

    bool Initialize(int width, int height, const char* title);
    void Shutdown();
    void PollEvents(LAppDelegate* delegate);
    void Present();

    bool ShouldClose() const { return _shouldClose; }
    void GetSize(int& width, int& height) const;
    std::size_t GetBackBufferBytes() const;
    std::size_t GetPresentBufferBytes() const;

private:
#if !defined(CSM_TARGET_ESP_PGL)
    struct MaskInfo
    {
        unsigned long Mask;
        int Shift;
        int Bits;
    };

    static MaskInfo BuildMaskInfo(unsigned long mask);
#endif

    bool ResizeBackBuffer(int width, int height);
#if !defined(CSM_TARGET_ESP_PGL)
    bool RecreateWindowImage(int width, int height);
#endif
    void Resize(int width, int height);

#if !defined(CSM_TARGET_ESP_PGL)
    unsigned long PackComponent(std::uint8_t value, const MaskInfo& mask) const;
    unsigned long PackPixel(std::uint8_t r, std::uint8_t g, std::uint8_t b) const;

    Display* _display;
    int _screen;
    Window _window;
    GC _graphicsContext;
    Atom _wmDeleteWindow;
    XVisualInfo _visualInfo;
    XImage* _windowImage;

    MaskInfo _redMask;
    MaskInfo _greenMask;
    MaskInfo _blueMask;
#else
    bool _hostPointerPressed;
#endif

    glContext _context;
    pix_t* _backBuffer;
    bool _contextInitialized;
    bool _shouldClose;
    int _width;
    int _height;
};
