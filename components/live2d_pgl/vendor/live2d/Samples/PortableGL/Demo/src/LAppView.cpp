/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppView.hpp"

#include <algorithm>
#include <math.h>
#include <string>
#include "LAppPal.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppTextureManager.hpp"
#include "LAppDefine.hpp"
#include "TouchManager_Common.hpp"
#include "LAppSprite.hpp"
#include "LAppSpriteShader.hpp"
#include "LAppModel.hpp"
#include "LAppPerformanceMonitor.hpp"

#include <Rendering/PortableGL/CubismRenderer_PortableGL.hpp>

using namespace std;
using namespace LAppDefine;

namespace {
constexpr float InvisibleSwitchInset = 8.0f;
constexpr float InvisibleSwitchMinSize = 64.0f;
constexpr float InvisibleSwitchMaxSize = 128.0f;

float GetInvisibleSwitchSize(int width, int height)
{
    const float base = static_cast<float>(std::min(width, height)) * 0.1f;
    return std::max(InvisibleSwitchMinSize, std::min(InvisibleSwitchMaxSize, base));
}
}

LAppView::LAppView() :
    LAppView_Common(),
    _back(NULL),
    _gear(NULL),
    _power(NULL),
    _switchButtonRect{0.0f, 0.0f, 0.0f, 0.0f},
    _renderSprite(NULL),
    _renderTarget(SelectTarget_None),
    _spriteShader(NULL)
{
    _clearColor[0] = 1.0f;
    _clearColor[1] = 1.0f;
    _clearColor[2] = 1.0f;
    _clearColor[3] = 0.0f;

    // タッチ関係のイベント管理
    _touchManager = new TouchManager_Common();
}

LAppView::~LAppView()
{
    _renderBuffer.DestroyRenderTarget();
   if (_renderSprite)
    {
        delete _renderSprite;
    }
    if (_spriteShader)
    {
        delete _spriteShader;
    }
    if (_touchManager)
    {
        delete _touchManager;
    }
    if (_back)
    {
        delete _back;
    }
    if (_gear)
    {
        delete _gear;
    }
    if (_power)
    {
        delete _power;
    }
}

void LAppView::Initialize(int width, int height)
{
    LAppView_Common::Initialize(width, height);

    // シェーダー作成
    if(_spriteShader == NULL)
    {
        _spriteShader = new LAppSpriteShader();
    }
}

void LAppView::Render(bool updateModels)
{
    LAppPerformanceMonitor& performanceMonitor = LAppPerformanceMonitor::GetInstance();
    performanceMonitor.AddBackgroundTime(0.0);
    performanceMonitor.AddUiTime(0.0);

    LAppLive2DManager* Live2DManager = LAppLive2DManager::GetInstance();

    Live2DManager->SetViewMatrix(_viewMatrix);

    // Cubism更新・描画
    const LAppPerformanceMonitor::TimePoint modelStart = LAppPerformanceMonitor::Now();
    Live2DManager->OnRender(updateModels);

    // 各モデルが持つ描画ターゲットをテクスチャとする場合
    if (_renderTarget == SelectTarget_ModelFrameBuffer && _renderSprite)
    {
        const GLfloat uvVertex[] =
        {
            1.0f, 1.0f,
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
        };

        for(csmUint32 i=0; i<Live2DManager->GetModelNum(); i++)
        {
            LAppModel* model = Live2DManager->GetModel(i);
            float alpha = i < 1 ? 1.0f : model->GetOpacity(); // 片方のみ不透明度を取得できるようにする
            _renderSprite->SetColor(1.0f * alpha, 1.0f * alpha, 1.0f * alpha, alpha);

            if (model)
            {
                _renderSprite->RenderImmidiate(model->GetRenderBuffer().GetColorBuffer(), uvVertex);
            }
        }
    }

    performanceMonitor.AddModelTotalTime(LAppPerformanceMonitor::DurationMs(modelStart));
}

void LAppView::InitializeSprite()
{
    GLuint programId = _spriteShader->GetShaderId();

    int width, height;
    LAppDelegate::GetClientSize(width, height);
    float x = 0.0f;
    float y = 0.0f;
    UpdateSwitchButtonRect(width, height);

    // 画面全体を覆うサイズ
    x = width * 0.5f;
    y = height * 0.5f;
    _renderSprite = new LAppSprite(x, y, static_cast<float>(width), static_cast<float>(height), 0, programId);

}

void LAppView::OnTouchesBegan(float px, float py) const
{
    _touchManager->TouchesBegan(px, py);
}

void LAppView::OnTouchesMoved(float px, float py) const
{
    float viewX = this->TransformViewX(_touchManager->GetX());
    float viewY = this->TransformViewY(_touchManager->GetY());

    _touchManager->TouchesMoved(px, py);

    LAppLive2DManager* Live2DManager = LAppLive2DManager::GetInstance();
    Live2DManager->OnDrag(viewX, viewY);
}

void LAppView::OnTouchesEnded(float px, float py) const
{
    // タッチ終了
    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    live2DManager->OnDrag(0.0f, 0.0f);
    {

        // シングルタップ
        float x = _deviceToScreen->TransformX(_touchManager->GetX()); // 論理座標変換した座標を取得。
        float y = _deviceToScreen->TransformY(_touchManager->GetY()); // 論理座標変換した座標を取得。
        if (DebugTouchLogEnable)
        {
            LAppPal::PrintLogLn("[APP]touchesEnded x:%.2f y:%.2f", x, y);
        }
        live2DManager->OnTap(x, y);

        // 歯車にタップしたか
        if (IsSwitchButtonHit(px, py))
        {
            live2DManager->NextScene();
        }
    }
}

void LAppView::PreModelDraw(LAppModel &refModel)
{
    // 別のレンダリングターゲットへ向けて描画する場合の使用するフレームバッファ
    Csm::Rendering::CubismRenderTarget_PortableGL* useTarget = NULL;

    if (_renderTarget != SelectTarget_None)
    {// 別のレンダリングターゲットへ向けて描画する場合

        // 透過設定
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        // 使用するターゲット
        useTarget = (_renderTarget == SelectTarget_ViewFrameBuffer) ? &_renderBuffer : &refModel.GetRenderBuffer();

        if (!useTarget->IsValid())
        {// 描画ターゲット内部未作成の場合はここで作成
            int bufWidth, bufHeight;
            LAppDelegate::GetClientSize(bufWidth, bufHeight);

            if(bufWidth!=0 && bufHeight!=0)
            {
                // モデル描画キャンバス
                useTarget->CreateRenderTarget(static_cast<csmUint32>(bufWidth), static_cast<csmUint32>(bufHeight));
            }
        }

        // レンダリング開始
        useTarget->BeginDraw();
        useTarget->Clear(_clearColor[0], _clearColor[1], _clearColor[2], _clearColor[3]); // 背景クリアカラー
    }
}

void LAppView::PostModelDraw(LAppModel &refModel)
{
    // 別のレンダリングターゲットへ向けて描画する場合の使用するフレームバッファ
    Csm::Rendering::CubismRenderTarget_PortableGL* useTarget = NULL;

    if (_renderTarget != SelectTarget_None)
    {// 別のレンダリングターゲットへ向けて描画する場合

        // 使用するターゲット
        useTarget = (_renderTarget == SelectTarget_ViewFrameBuffer) ? &_renderBuffer : &refModel.GetRenderBuffer();

        // レンダリング終了
        useTarget->EndDraw();

        // LAppViewの持つフレームバッファを使うなら、スプライトへの描画はここ
        if (_renderTarget == SelectTarget_ViewFrameBuffer && _renderSprite)
        {
            const GLfloat uvVertex[] =
            {
                1.0f, 1.0f,
                0.0f, 1.0f,
                0.0f, 0.0f,
                1.0f, 0.0f,
            };

            _renderSprite->SetColor(1.0f * GetSpriteAlpha(0), 1.0f * GetSpriteAlpha(0), 1.0f * GetSpriteAlpha(0), GetSpriteAlpha(0));
            _renderSprite->RenderImmidiate(useTarget->GetColorBuffer(), uvVertex);
        }
    }
}

void LAppView::SwitchRenderingTarget(SelectTarget targetType)
{
    _renderTarget = targetType;
}

void LAppView::SetRenderTargetClearColor(float r, float g, float b)
{
    _clearColor[0] = r;
    _clearColor[1] = g;
    _clearColor[2] = b;
}


float LAppView::GetSpriteAlpha(int assign) const
{
    // assignの数値に応じて適当に決定
    float alpha = 0.4f + static_cast<float>(assign) * 0.5f; // サンプルとしてαに適当な差をつける
    if (alpha > 1.0f)
    {
        alpha = 1.0f;
    }
    if (alpha < 0.1f)
    {
        alpha = 0.1f;
    }

    return alpha;
}

void LAppView::ResizeSprite()
{
    // 描画領域サイズ
    int width, height;
    LAppDelegate::GetClientSize(width, height);

    float x = 0.0f;
    float y = 0.0f;
    UpdateSwitchButtonRect(width, height);

    if (_renderSprite)
    {
        x = width * 0.5f;
        y = height * 0.5f;
        _renderSprite->ResetRect(x, y, static_cast<float>(width), static_cast<float>(height));
    }
}

void LAppView::DestroySpriteRenderTarget()
{
    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    if (_renderTarget == SelectTarget_ViewFrameBuffer)
    {
        _renderBuffer.DestroyRenderTarget();
    }
    else if (_renderTarget == SelectTarget_ModelFrameBuffer)
    {
        for (csmUint32 i = 0; i < live2DManager->GetModelNum(); i++)
        {
            LAppModel* model = live2DManager->GetModel(i);
            if (model)
            {
                model->GetRenderBuffer().DestroyRenderTarget();
            }
        }
    }
}

bool LAppView::IsSwitchButtonHit(float pointX, float pointY) const
{
    int maxWidth = 0;
    int maxHeight = 0;
    LAppDelegate::GetClientSize(maxWidth, maxHeight);
    if (maxWidth == 0 || maxHeight == 0)
    {
        return false;
    }

    const float flippedY = static_cast<float>(maxHeight) - pointY;
    return pointX >= _switchButtonRect.Left
        && pointX <= _switchButtonRect.Right
        && flippedY <= _switchButtonRect.Up
        && flippedY >= _switchButtonRect.Down;
}

void LAppView::UpdateSwitchButtonRect(int width, int height)
{
    const float switchSize = GetInvisibleSwitchSize(width, height);
    const float centerX = static_cast<float>(width) - switchSize * 0.5f - InvisibleSwitchInset;
    const float centerY = static_cast<float>(height) - switchSize * 0.5f - InvisibleSwitchInset;

    _switchButtonRect.Left = centerX - switchSize * 0.5f;
    _switchButtonRect.Right = centerX + switchSize * 0.5f;
    _switchButtonRect.Up = centerY + switchSize * 0.5f;
    _switchButtonRect.Down = centerY - switchSize * 0.5f;
}
