/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <string>

#include "CubismFramework.hpp"
#include "LAppAllocator_Common.hpp"

class LAppTextureManager;
class LAppView;
class LAppX11Window;

class LAppDelegate
{
public:
    static LAppDelegate* GetInstance();
    static void ReleaseInstance();

    bool Initialize();
    void Release();
    void Run();
    bool Tick();

    void OnMouseButton(bool pressed);
    void OnMouseMove(float x, float y);

    static void GetClientSize(int& rWidth, int& rHeight);

    LAppX11Window* GetWindow() { return _window; }
    LAppView* GetView() { return _view; }
    bool GetIsEnd() { return _isEnd; }
    void AppEnd() { _isEnd = true; }

    void SetExecuteAbsolutePath();
    std::string GetExecuteAbsolutePath() { return _executeAbsolutePath; }
    LAppTextureManager* GetTextureManager() { return _textureManager; }
    int GetWindowWidth() { return _windowWidth; }
    int GetWindowHeight() { return _windowHeight; }

private:
    LAppDelegate();
    ~LAppDelegate();

    void InitializeCubism();

    LAppAllocator_Common _cubismAllocator;
    Csm::CubismFramework::Option _cubismOption;
    LAppX11Window* _window;
    LAppView* _view;
    bool _captured;
    float _mouseX;
    float _mouseY;
    bool _isEnd;
    LAppTextureManager* _textureManager;
    std::string _executeAbsolutePath;
    int _windowWidth;
    int _windowHeight;
};
