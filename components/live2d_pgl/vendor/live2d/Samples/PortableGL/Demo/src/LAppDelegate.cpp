/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"

#if !defined(CSM_TARGET_ESP_PGL)
#include <libgen.h>
#include <unistd.h>
#else
#include "sdkconfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "LAppAssetProvider.hpp"
#include "LAppDefine.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppPal.hpp"
#include "LAppPerformanceMonitor.hpp"
#include "LAppTextureManager.hpp"
#include "LAppView.hpp"
#include "LAppX11Window.hpp"

using namespace Csm;
using namespace LAppDefine;

namespace {
    LAppDelegate* s_instance = NULL;
}

LAppDelegate* LAppDelegate::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new LAppDelegate();
    }

    return s_instance;
}

void LAppDelegate::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

bool LAppDelegate::Initialize()
{
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("START");
    }

    _window = new LAppX11Window();
    if (_window == NULL || !_window->Initialize(RenderTargetWidth, RenderTargetHeight, "SAMPLE"))
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLogLn("Can't create X11 window.");
        }
        delete _window;
        _window = NULL;
        return false;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    int width = 0;
    int height = 0;
    _window->GetSize(width, height);
    _windowWidth = width;
    _windowHeight = height;
    glViewport(0, 0, _windowWidth, _windowHeight);

    InitializeCubism();
    SetExecuteAbsolutePath();

    LAppLive2DManager::GetInstance();

    _view->Initialize(width, height);
    _view->InitializeSprite();

    return true;
}

void LAppDelegate::Release()
{
    if (_window != NULL)
    {
        _window->Shutdown();
        delete _window;
        _window = NULL;
    }

    delete _textureManager;
    _textureManager = NULL;

    delete _view;
    _view = NULL;

    LAppLive2DManager::ReleaseInstance();
    CubismFramework::Dispose();
}

void LAppDelegate::Run()
{
    while (Tick())
    {
#if defined(CSM_TARGET_ESP_PGL)
        vTaskDelay(1);
#endif
    }

    LAppPerformanceMonitor::GetInstance().Flush();
    Release();
    LAppDelegate::ReleaseInstance();
}

bool LAppDelegate::Tick()
{
    if (_window == NULL || _window->ShouldClose() || _isEnd)
    {
        return false;
    }

    LAppPerformanceMonitor& performanceMonitor = LAppPerformanceMonitor::GetInstance();
    _window->PollEvents(this);

    int width = 0;
    int height = 0;
    _window->GetSize(width, height);
    if ((_windowWidth != width || _windowHeight != height) && width > 0 && height > 0)
    {
        _view->Initialize(width, height);
        _view->ResizeSprite();
        _view->DestroySpriteRenderTarget();
        LAppLive2DManager::GetInstance()->SetRenderTargetSize(width, height);
        _windowWidth = width;
        _windowHeight = height;
    }

    if (_windowWidth <= 0 || _windowHeight <= 0)
    {
        return _window != NULL && !_window->ShouldClose() && !_isEnd;
    }

    performanceMonitor.BeginFrame(_windowWidth, _windowHeight);
    glViewport(0, 0, _windowWidth, _windowHeight);

    LAppPal::UpdateTime();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    const LAppPerformanceMonitor::TimePoint clearStart = LAppPerformanceMonitor::Now();
    glClear(GL_COLOR_BUFFER_BIT);
    performanceMonitor.AddClearTime(LAppPerformanceMonitor::DurationMs(clearStart));
    _view->Render();

    _window->Present();
    performanceMonitor.EndFrame();

    return _window != NULL && !_window->ShouldClose() && !_isEnd;
}

LAppDelegate::LAppDelegate()
    : _cubismOption()
    , _window(NULL)
    , _view(NULL)
    , _captured(false)
    , _mouseX(0.0f)
    , _mouseY(0.0f)
    , _isEnd(false)
    , _textureManager(NULL)
    , _executeAbsolutePath("")
    , _windowWidth(0)
    , _windowHeight(0)
{
    _view = new LAppView();
    _textureManager = new LAppTextureManager();
}

LAppDelegate::~LAppDelegate()
{
}

void LAppDelegate::InitializeCubism()
{
    _cubismOption.LogFunction = LAppPal::PrintMessage;
    _cubismOption.LoggingLevel = LAppDefine::CubismLoggingLevel;
    _cubismOption.LoadFileFunction = LAppPal::LoadFileAsBytes;
    _cubismOption.ReleaseBytesFunction = LAppPal::ReleaseBytes;
    CubismFramework::StartUp(&_cubismAllocator, &_cubismOption);
    CubismFramework::Initialize();
    LAppPal::UpdateTime();
}

void LAppDelegate::OnMouseButton(bool pressed)
{
    if (_view == NULL)
    {
        return;
    }

    if (pressed)
    {
        _captured = true;
        _view->OnTouchesBegan(_mouseX, _mouseY);
    }
    else if (_captured)
    {
        _captured = false;
        _view->OnTouchesEnded(_mouseX, _mouseY);
    }
}

void LAppDelegate::OnMouseMove(float x, float y)
{
    _mouseX = x;
    _mouseY = y;

    if (!_captured || _view == NULL)
    {
        return;
    }

    _view->OnTouchesMoved(_mouseX, _mouseY);
}

void LAppDelegate::GetClientSize(int& rWidth, int& rHeight)
{
    if (LAppDelegate::GetInstance()->GetWindow() == NULL)
    {
        rWidth = 0;
        rHeight = 0;
        return;
    }

    LAppDelegate::GetInstance()->GetWindow()->GetSize(rWidth, rHeight);
}

void LAppDelegate::SetExecuteAbsolutePath()
{
#if defined(CSM_TARGET_ESP_PGL)
    _executeAbsolutePath.clear();
#else
    char path[1024];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1)
    {
        path[len] = '\0';
    }

    _executeAbsolutePath = dirname(path);
    _executeAbsolutePath += "/";
#endif
    LAppAssetProvider::GetInstance().SetBaseDirectory(_executeAbsolutePath);
}
