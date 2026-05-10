/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppLive2DManager.hpp"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <Rendering/CubismRenderer.hpp>
#include <Rendering/PortableGL/CubismOffscreenManager_PortableGL.hpp>
#include "LAppAssetProvider.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppDelegate.hpp"
#include "LAppModel.hpp"
#include "LAppPerformanceMonitor.hpp"
#include "LAppView.hpp"
#include "LAppSprite.hpp"

using namespace Csm;
using namespace LAppDefine;

namespace {
    LAppLive2DManager* s_instance = NULL;

    void BeganMotion(ACubismMotion* self)
    {
        LAppPal::PrintLogLn("Motion began: %x", self);
    }

    void FinishedMotion(ACubismMotion* self)
    {
        LAppPal::PrintLogLn("Motion Finished: %x", self);
    }

    bool IsPriorityModelDirectory(const Csm::csmString& directoryName)
    {
        return strcmp(directoryName.GetRawString(), "A") == 0;
    }
}

LAppLive2DManager* LAppLive2DManager::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new LAppLive2DManager();
    }

    return s_instance;
}

void LAppLive2DManager::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

LAppLive2DManager::LAppLive2DManager()
    : _viewMatrix(NULL)
    , _sceneIndex(0)
{
    _viewMatrix = new CubismMatrix44();
    SetUpModel();

    ChangeScene(_sceneIndex);
}

LAppLive2DManager::~LAppLive2DManager()
{
    ReleaseAllModel();
    delete _viewMatrix;
    Csm::Rendering::CubismOffscreenManager_PortableGL::ReleaseInstance();
}

void LAppLive2DManager::ReleaseAllModel()
{
    for (csmUint32 i = 0; i < _models.GetSize(); i++)
    {
        delete _models[i];
    }

    _models.Clear();
}

void LAppLive2DManager::SetUpModel()
{
    _modelEntries.Clear();
    const std::vector<LAppAssetProvider::ModelEntry> entries = LAppAssetProvider::GetInstance().EnumerateModelEntries();
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        ModelEntry entry = {};
        entry.Directory = entries[i].Directory.c_str();
        entry.JsonFileName = entries[i].JsonFileName.c_str();
        _modelEntries.PushBack(entry);
    }

    std::sort(_modelEntries.GetPtr(), _modelEntries.GetPtr() + _modelEntries.GetSize(),
        [](const ModelEntry& lhs, const ModelEntry& rhs)
        {
            const bool lhsIsPriority = IsPriorityModelDirectory(lhs.Directory);
            const bool rhsIsPriority = IsPriorityModelDirectory(rhs.Directory);
            if (lhsIsPriority != rhsIsPriority)
            {
                return lhsIsPriority;
            }

            return strcmp(lhs.Directory.GetRawString(), rhs.Directory.GetRawString()) < 0;
        });
}

csmVector<csmString> LAppLive2DManager::GetModelDir() const
{
    csmVector<csmString> modelDirectories;
    modelDirectories.PrepareCapacity(_modelEntries.GetSize());

    for (csmUint32 i = 0; i < _modelEntries.GetSize(); ++i)
    {
        modelDirectories.PushBack(_modelEntries[i].Directory);
    }

    return modelDirectories;
}

csmInt32 LAppLive2DManager::GetModelDirSize() const
{
    return _modelEntries.GetSize();
}

LAppModel* LAppLive2DManager::GetModel(csmUint32 no) const
{
    if (no < _models.GetSize())
    {
        return _models[no];
    }

    return NULL;
}

void LAppLive2DManager::SetRenderTargetSize(csmUint32 width, csmUint32 height)
{
    for (csmUint32 i = 0; i < _models.GetSize(); i++)
    {
        LAppModel* model = GetModel(i);

        model->SetRenderTargetSize(width, height);
    }
}

void LAppLive2DManager::OnDrag(csmFloat32 x, csmFloat32 y) const
{
    for (csmUint32 i = 0; i < _models.GetSize(); i++)
    {
        LAppModel* model = GetModel(i);

        model->SetDragging(x, y);
    }
}

void LAppLive2DManager::OnTap(csmFloat32 x, csmFloat32 y)
{
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("[APP]tap point: {x:%.2f y:%.2f}", x, y);
    }

    for (csmUint32 i = 0; i < _models.GetSize(); i++)
    {
        if (_models[i]->HitTest(HitAreaNameHead, x, y))
        {
            if (DebugLogEnable)
            {
                LAppPal::PrintLogLn("[APP]hit area: [%s]", HitAreaNameHead);
            }
            _models[i]->SetRandomExpression();
        }
        else if (_models[i]->HitTest(HitAreaNameBody, x, y))
        {
            if (DebugLogEnable)
            {
                LAppPal::PrintLogLn("[APP]hit area: [%s]", HitAreaNameBody);
            }
            _models[i]->StartRandomMotion(MotionGroupTapBody, PriorityNormal, FinishedMotion, BeganMotion);
        }
    }
}

void LAppLive2DManager::OnUpdate() const
{
    OnRender(true);
}

void LAppLive2DManager::OnRender(bool updateModels) const
{
    int width, height;
    LAppDelegate::GetClientSize(width, height);
    LAppPerformanceMonitor& performanceMonitor = LAppPerformanceMonitor::GetInstance();

    // モデルで使用するオフスクリーン管理の開始処理
    const LAppPerformanceMonitor::TimePoint frameBeginStart = LAppPerformanceMonitor::Now();
    Csm::Rendering::CubismOffscreenManager_PortableGL::GetInstance()->BeginFrameProcess();
    performanceMonitor.AddModelFrameBeginTime(LAppPerformanceMonitor::DurationMs(frameBeginStart));

    csmUint32 modelCount = _models.GetSize();
    for (csmUint32 i = 0; i < modelCount; ++i)
    {
        CubismMatrix44 projection;
        LAppModel* model = GetModel(i);

        if (model->GetModel() == NULL)
        {
            LAppPal::PrintLogLn("Failed to model->GetModel().");
            continue;
        }

        if (model->GetModel()->GetCanvasWidth() > 1.0f && width < height)
        {
            // 横に長いモデルを縦長ウィンドウに表示する際モデルの横サイズでscaleを算出する
            model->GetModelMatrix()->SetWidth(2.0f);
            projection.Scale(1.0f, static_cast<float>(width) / static_cast<float>(height));
        }
        else
        {
            projection.Scale(static_cast<float>(height) / static_cast<float>(width), 1.0f);
        }

        // 必要があればここで乗算
        if (_viewMatrix != NULL)
        {
            projection.MultiplyByMatrix(_viewMatrix);
        }

        if (updateModels)
        {
            const LAppPerformanceMonitor::TimePoint updateStart = LAppPerformanceMonitor::Now();
            model->Update();
            const double updateMs = LAppPerformanceMonitor::DurationMs(updateStart);
            performanceMonitor.AddModelUpdateTime(updateMs);
            performanceMonitor.AddModelUpdateTime(static_cast<int>(i), updateMs);
        }

        const LAppPerformanceMonitor::TimePoint drawStart = LAppPerformanceMonitor::Now();
        LAppView* view = LAppDelegate::GetInstance()->GetView();
        const LAppPerformanceMonitor::TimePoint preDrawStart = LAppPerformanceMonitor::Now();
        view->PreModelDraw(*model);
        performanceMonitor.AddModelPreDrawTime(LAppPerformanceMonitor::DurationMs(preDrawStart));

        model->Draw(projection);///< 参照渡しなのでprojectionは変質する

        const LAppPerformanceMonitor::TimePoint postDrawStart = LAppPerformanceMonitor::Now();
        view->PostModelDraw(*model);
        performanceMonitor.AddModelPostDrawTime(LAppPerformanceMonitor::DurationMs(postDrawStart));
        const double drawMs = LAppPerformanceMonitor::DurationMs(drawStart);
        performanceMonitor.AddModelDrawTime(drawMs);
        performanceMonitor.AddModelDrawTime(static_cast<int>(i), drawMs);
    }

    // モデルで使用するオフスクリーン管理の終了処理
    const LAppPerformanceMonitor::TimePoint frameEndStart = LAppPerformanceMonitor::Now();
    Csm::Rendering::CubismOffscreenManager_PortableGL::GetInstance()->EndFrameProcess();
    // もし余っているオフスクリーンのリソースを解放したい場合行う処理
    Csm::Rendering::CubismOffscreenManager_PortableGL::GetInstance()->ReleaseStaleRenderTextures();
    performanceMonitor.AddModelFrameEndTime(LAppPerformanceMonitor::DurationMs(frameEndStart));
}

void LAppLive2DManager::NextScene()
{
    if (GetModelDirSize() <= 0)
    {
        return;
    }

    csmInt32 no = (_sceneIndex + 1) % GetModelDirSize();
    ChangeScene(no);
}

void LAppLive2DManager::ChangeScene(Csm::csmInt32 index)
{
    if (_modelEntries.GetSize() == 0)
    {
        LAppPal::PrintLogLn("[APP]No model entries found.");
        ReleaseAllModel();
        return;
    }

    const csmInt32 requestedIndex = (index % GetModelDirSize() + GetModelDirSize()) % GetModelDirSize();
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("[APP]model index request: %d", requestedIndex);
    }

    ReleaseAllModel();

    for (csmInt32 attempt = 0; attempt < GetModelDirSize(); ++attempt)
    {
        const csmInt32 candidateIndex = (requestedIndex + attempt) % GetModelDirSize();
        const ModelEntry& modelEntry = _modelEntries[candidateIndex];
        const csmString& model = modelEntry.Directory;

        if (DebugLogEnable)
        {
            LAppPal::PrintLogLn("[APP]model index: %d", candidateIndex);
        }
        LAppPal::PrintLogLn("[APP]_modelDir: %s", model.GetRawString());

        std::string modelPath = LAppAssetProvider::GetInstance().BuildResourcePath(model.GetRawString());
        if (!modelPath.empty() && modelPath[modelPath.size() - 1] != '/')
        {
            modelPath += "/";
        }

        ReleaseAllModel();
        _models.PushBack(new LAppModel());
        _models[0]->LoadAssets(modelPath.c_str(), modelEntry.JsonFileName.GetRawString());

        if (_models[0]->GetModel() != NULL)
        {
            _sceneIndex = candidateIndex;
            break;
        }

        LAppPal::PrintLogLn(
            "[APP]Failed to load model '%s/%s'. Trying next model.",
            modelEntry.Directory.GetRawString(),
            modelEntry.JsonFileName.GetRawString());
    }

    if (_models.GetSize() == 0 || _models[0]->GetModel() == NULL)
    {
        LAppPal::PrintLogLn("[APP]No loadable model found.");
        ReleaseAllModel();
        return;
    }

    /*
     * モデル半透明表示を行うサンプルを提示する。
     * ここでUSE_RENDER_TARGET、USE_MODEL_RENDER_TARGETが定義されている場合
     * 別のレンダリングターゲットにモデルを描画し、描画結果をテクスチャとして別のスプライトに張り付ける。
     */
    {
#if defined(USE_RENDER_TARGET)
        // LAppViewの持つターゲットに描画を行う場合、こちらを選択
        LAppView::SelectTarget useRenderTarget = LAppView::SelectTarget_ViewFrameBuffer;
#elif defined(USE_MODEL_RENDER_TARGET)
        // 各LAppModelの持つターゲットに描画を行う場合、こちらを選択
        LAppView::SelectTarget useRenderTarget = LAppView::SelectTarget_ModelFrameBuffer;
#else
        // デフォルトのメインフレームバッファへレンダリングする(通常)
        LAppView::SelectTarget useRenderTarget = LAppView::SelectTarget_None;
#endif

#if defined(USE_RENDER_TARGET) || defined(USE_MODEL_RENDER_TARGET)
        // モデル個別にαを付けるサンプルとして、もう1体モデルを作成し、少し位置をずらす
        _models.PushBack(new LAppModel());
        _models[1]->LoadAssets(modelPath.GetRawString(), modelJsonName.GetRawString());
        _models[1]->GetModelMatrix()->TranslateX(0.2f);
#endif

        float clearColor[3] = { 0.0f, 0.0f, 0.0f };

        LAppDelegate::GetInstance()->GetView()->SwitchRenderingTarget(useRenderTarget);

        if(useRenderTarget)
        {
            LAppDelegate::GetInstance()->GetView()->SwitchRenderingTarget(useRenderTarget);
            // 背景クリア色
            LAppDelegate::GetInstance()->GetView()->SetRenderTargetClearColor(clearColor[0], clearColor[1], clearColor[2]);
        }
    }
}

csmUint32 LAppLive2DManager::GetModelNum() const
{
    return _models.GetSize();
}

void LAppLive2DManager::SetViewMatrix(CubismMatrix44* m)
{
    for (int i = 0; i < 16; i++) {
        _viewMatrix->GetArray()[i] = m->GetArray()[i];
    }
}
