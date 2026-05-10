/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppModel.hpp"
#include <fstream>
#include <vector>
#include <CubismModelSettingJson.hpp>
#include <Motion/CubismMotion.hpp>
#include <Physics/CubismPhysics.hpp>
#include <CubismDefaultParameterId.hpp>
#include <Rendering/PortableGL/CubismRenderer_PortableGL.hpp>
#include <Utils/CubismString.hpp>
#include <Id/CubismIdManager.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "LAppPerformanceMonitor.hpp"
#include "LAppTextureManager.hpp"
#include "LAppDelegate.hpp"
#include "Motion/CubismBreathUpdater.hpp"
#include "Motion/CubismLookUpdater.hpp"
#include "Motion/CubismExpressionUpdater.hpp"
#include "Motion/CubismEyeBlinkUpdater.hpp"
#include "Motion/CubismLipSyncUpdater.hpp"
#include "Motion/CubismPhysicsUpdater.hpp"
#include "Motion/CubismPoseUpdater.hpp"

using namespace Live2D::Cubism::Framework;
using namespace Live2D::Cubism::Framework::DefaultParameterId;
using namespace LAppDefine;

namespace {
double ToMiB(const Live2D::Cubism::Core::csmUint64 bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

#ifdef CSM_TARGET_ESP_PGL
constexpr Csm::csmFloat32 kPortableGLMaskBufferSize = 64.0f;

void ConfigurePortableGLMaskBuffers(Csm::CubismUserModel& model)
{
    Rendering::CubismRenderer_PortableGL* renderer = model.GetRenderer<Rendering::CubismRenderer_PortableGL>();
    if (renderer == NULL)
    {
        return;
    }

    renderer->SetDrawableClippingMaskBufferSize(kPortableGLMaskBufferSize, kPortableGLMaskBufferSize);
    renderer->SetOffscreenClippingMaskBufferSize(kPortableGLMaskBufferSize, kPortableGLMaskBufferSize);
}
#endif

void LogCoreMemoryBreakdown(const Live2D::Cubism::Framework::CubismModel* model)
{
    using namespace Live2D::Cubism::Core;

    if (model == NULL)
    {
        return;
    }

    csmDebugMocMemoryInfo mocInfo = {};
    csmDebugModelMemoryInfo modelInfo = {};
    const csmModel* rawModel = model->GetModel();

    if (!csmDebugGetModelMocMemoryInfo(rawModel, &mocInfo) || !csmDebugGetModelMemoryInfo(rawModel, &modelInfo))
    {
        return;
    }

    LAppPal::PrintLogLn(
        "[COREMEM] arena_req_mib=%.2f arena_req_clamped_mib=%.3f arena_used_mib=%.3f heap_total_mib=%.3f",
        ToMiB(modelInfo.arenaBytesRequested),
        ToMiB(mocInfo.modelArenaBytesReturnedClamped),
        ToMiB(modelInfo.arenaBytesUsed),
        ToMiB(modelInfo.heapTotalBytes));

    LAppPal::PrintLogLn(
        "[COREMEM] heap_mib: sort=%.3f param_eval=%.3f eval=%.3f group=%.3f rotation=%.3f drawable=%.3f deformer=%.3f late=%.3f pair=%.3f debug=%.3f other=%.3f",
        ToMiB(modelInfo.heapSortBytes),
        ToMiB(modelInfo.heapParamEvalBytes),
        ToMiB(modelInfo.heapEvalBytes),
        ToMiB(modelInfo.heapGroupBytes),
        ToMiB(modelInfo.heapRotationBytes),
        ToMiB(modelInfo.heapDrawableBytes),
        ToMiB(modelInfo.heapDeformerBytes),
        ToMiB(modelInfo.heapLateBytes),
        ToMiB(modelInfo.heapPairBlendBytes),
        ToMiB(modelInfo.heapDebugBytes),
        ToMiB(modelInfo.heapOtherBytes));

    LAppPal::PrintLogLn(
        "[COREMEM] moc_layout: parts=%d drawables=%d params=%d key_entries_readable=%u key_entries_overlap=%u key_values_raw=%llu key_values_clamped=%llu overlap=%d",
        mocInfo.partCount,
        mocInfo.drawableCount,
        mocInfo.parameterCount,
        static_cast<unsigned>(mocInfo.parameterKeyCountReadableEntries),
        static_cast<unsigned>(mocInfo.parameterKeyCountOverlapEntries),
        static_cast<unsigned long long>(mocInfo.parameterKeyValueCountRaw),
        static_cast<unsigned long long>(mocInfo.parameterKeyValueCountClamped),
        mocInfo.hasParameterKeyCountOverlap ? 1 : 0);
}
}

LAppModel::LAppModel()
    : LAppModel_Common()
    , _modelSetting(NULL)
    , _userTimeSeconds(0.0f)
    , _motionUpdated(false)
{
    if (DebugLogEnable)
    {
        _debugMode = true;
    }

    _idParamAngleX = CubismFramework::GetIdManager()->GetId(ParamAngleX);
    _idParamAngleY = CubismFramework::GetIdManager()->GetId(ParamAngleY);
    _idParamAngleZ = CubismFramework::GetIdManager()->GetId(ParamAngleZ);
    _idParamBodyAngleX = CubismFramework::GetIdManager()->GetId(ParamBodyAngleX);
    _idParamEyeBallX = CubismFramework::GetIdManager()->GetId(ParamEyeBallX);
    _idParamEyeBallY = CubismFramework::GetIdManager()->GetId(ParamEyeBallY);
}

LAppModel::~LAppModel()
{
    _renderBuffer.DestroyRenderTarget();
    ReleaseOwnedTextures();

    ReleaseMotions();
    ReleaseExpressions();

    if (_modelSetting != NULL)
    {
        for (csmInt32 i = 0; i < _modelSetting->GetMotionGroupCount(); i++)
        {
            const csmChar* group = _modelSetting->GetMotionGroupName(i);
            ReleaseMotionGroup(group);
        }
        delete(_modelSetting);
        _modelSetting = NULL;
    }
}

void LAppModel::LoadAssets(const csmChar* dir, const csmChar* fileName)
{
    _modelHomeDir = dir;

    if (_debugMode)
    {
        LAppPal::PrintLogLn("[APP]load model setting: %s", fileName);
    }

    csmSizeInt size;
    const csmString path = csmString(dir) + fileName;

    csmByte* buffer = CreateBuffer(path.GetRawString(), &size);
    ICubismModelSetting* setting = new CubismModelSettingJson(buffer, size);
    DeleteBuffer(buffer, path.GetRawString());

    SetupModel(setting);

    if (_model == NULL)
    {
        LAppPal::PrintLogLn("Failed to LoadAssets().");
        return;
    }

    CreateRenderer(LAppDelegate::GetInstance()->GetWindowWidth(), LAppDelegate::GetInstance()->GetWindowHeight());
#ifdef CSM_TARGET_ESP_PGL
    ConfigurePortableGLMaskBuffers(*this);
#endif

    SetupTextures();
}


void LAppModel::SetupModel(ICubismModelSetting* setting)
{
    _updating = true;
    _initialized = false;

    _modelSetting = setting;

    csmByte* buffer;
    csmSizeInt size;

    //Cubism Model
    if (strcmp(_modelSetting->GetModelFileName(), "") != 0)
    {
        csmString path = _modelSetting->GetModelFileName();
        path = _modelHomeDir + path;

        if (_debugMode)
        {
            LAppPal::PrintLogLn("[APP]create model: %s", setting->GetModelFileName());
        }

        buffer = CreateBuffer(path.GetRawString(), &size);
        LoadModel(buffer, size);
        LogCoreMemoryBreakdown(_model);
        DeleteBuffer(buffer, path.GetRawString());
    }

    //Expression
    if (_modelSetting->GetExpressionCount() > 0)
    {
        const csmInt32 count = _modelSetting->GetExpressionCount();
        for (csmInt32 i = 0; i < count; i++)
        {
            csmString name = _modelSetting->GetExpressionName(i);
            csmString path = _modelSetting->GetExpressionFileName(i);
            path = _modelHomeDir + path;

            buffer = CreateBuffer(path.GetRawString(), &size);
            ACubismMotion* motion = LoadExpression(buffer, size, name.GetRawString());

            if (motion)
            {
                if (_expressions[name] != NULL)
                {
                    ACubismMotion::Delete(_expressions[name]);
                    _expressions[name] = NULL;
                }
                _expressions[name] = motion;
            }

            DeleteBuffer(buffer, path.GetRawString());
        }

        CubismExpressionUpdater* expression = CSM_NEW CubismExpressionUpdater(*_expressionManager);
        _updateScheduler.AddUpdatableList(expression);
    }

    //Physics
    if (strcmp(_modelSetting->GetPhysicsFileName(), "") != 0)
    {
        csmString path = _modelSetting->GetPhysicsFileName();
        path = _modelHomeDir + path;

        buffer = CreateBuffer(path.GetRawString(), &size);
        LoadPhysics(buffer, size);
        DeleteBuffer(buffer, path.GetRawString());

        if (_physics != nullptr)
        {
            CubismPhysicsUpdater* physics = CSM_NEW CubismPhysicsUpdater(*_physics);
            _updateScheduler.AddUpdatableList(physics);
        }
    }

    //Pose
    if (strcmp(_modelSetting->GetPoseFileName(), "") != 0)
    {
        csmString path = _modelSetting->GetPoseFileName();
        path = _modelHomeDir + path;

        buffer = CreateBuffer(path.GetRawString(), &size);
        LoadPose(buffer, size);
        DeleteBuffer(buffer, path.GetRawString());

        if (_pose != nullptr)
        {
            CubismPoseUpdater* pose = CSM_NEW CubismPoseUpdater(*_pose);
            _updateScheduler.AddUpdatableList(pose);
        }
    }

    //EyeBlink
    {
        if (_modelSetting->GetEyeBlinkParameterCount() > 0)
        {
            _eyeBlink = CubismEyeBlink::Create(_modelSetting);

            CubismEyeBlinkUpdater* eyeBlink = CSM_NEW CubismEyeBlinkUpdater(_motionUpdated, *_eyeBlink);
            _updateScheduler.AddUpdatableList(eyeBlink);
        }
    }

    //Breath
    {
        _breath = CubismBreath::Create();

        csmVector<CubismBreath::BreathParameterData> breathParameters;

        breathParameters.PushBack(CubismBreath::BreathParameterData(_idParamAngleX, 0.0f, 15.0f, 6.5345f, 0.5f));
        breathParameters.PushBack(CubismBreath::BreathParameterData(_idParamAngleY, 0.0f, 8.0f, 3.5345f, 0.5f));
        breathParameters.PushBack(CubismBreath::BreathParameterData(_idParamAngleZ, 0.0f, 10.0f, 5.5345f, 0.5f));
        breathParameters.PushBack(CubismBreath::BreathParameterData(_idParamBodyAngleX, 0.0f, 4.0f, 15.5345f, 0.5f));
        breathParameters.PushBack(CubismBreath::BreathParameterData(CubismFramework::GetIdManager()->GetId(ParamBreath), 0.5f, 0.5f, 3.2345f, 0.5f));

        _breath->SetParameters(breathParameters);

        CubismBreathUpdater* breath = CSM_NEW CubismBreathUpdater(*_breath);
        _updateScheduler.AddUpdatableList(breath);
    }

    //UserData
    if (strcmp(_modelSetting->GetUserDataFile(), "") != 0)
    {
        csmString path = _modelSetting->GetUserDataFile();
        path = _modelHomeDir + path;
        buffer = CreateBuffer(path.GetRawString(), &size);
        LoadUserData(buffer, size);
        DeleteBuffer(buffer, path.GetRawString());
    }

    // EyeBlinkIds
    {
        csmInt32 eyeBlinkIdCount = _modelSetting->GetEyeBlinkParameterCount();
        for (csmInt32 i = 0; i < eyeBlinkIdCount; ++i)
        {
            _eyeBlinkIds.PushBack(_modelSetting->GetEyeBlinkParameterId(i));
        }
    }

    // LipSyncIds
    {
        csmInt32 lipSyncIdCount = _modelSetting->GetLipSyncParameterCount();
        for (csmInt32 i = 0; i < lipSyncIdCount; ++i)
        {
            _lipSyncIds.PushBack(_modelSetting->GetLipSyncParameterId(i));
        }

        CubismLipSyncUpdater* lipSync = CSM_NEW CubismLipSyncUpdater(_lipSyncIds, _wavFileHandler);
        _updateScheduler.AddUpdatableList(lipSync);
    }

    // Look
    {
        _look = CubismLook::Create();

        csmVector<CubismLook::LookParameterData> lookParameters;

        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleX, 30.0f));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleY, 0.0f, 30.0f));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleZ, 0.0f, 0.0f, -30.0f));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamBodyAngleX, 10.0f));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamEyeBallX, 1.0f));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamEyeBallY, 0.0f, 1.0f));

        _look->SetParameters(lookParameters);

        CubismLookUpdater* look = CSM_NEW CubismLookUpdater(*_look, *_dragManager);
        _updateScheduler.AddUpdatableList(look);
    }

    _updateScheduler.SortUpdatableList();

    if (_modelSetting == NULL || _modelMatrix == NULL)
    {
        LAppPal::PrintLogLn("Failed to SetupModel().");
        return;
    }

    //Layout
    csmMap<csmString, csmFloat32> layout;
    _modelSetting->GetLayoutMap(layout);
    _modelMatrix->SetupFromLayout(layout);

    _model->SaveParameters();

    for (csmInt32 i = 0; i < _modelSetting->GetMotionGroupCount(); i++)
    {
        const csmChar* group = _modelSetting->GetMotionGroupName(i);
        PreloadMotionGroup(group);
    }

    _motionManager->StopAllMotions();

    _updating = false;
    _initialized = true;
}

void LAppModel::PreloadMotionGroup(const csmChar* group)
{
    const csmInt32 count = _modelSetting->GetMotionCount(group);

    for (csmInt32 i = 0; i < count; i++)
    {
        //ex) idle_0
        csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, i);
        csmString path = _modelSetting->GetMotionFileName(group, i);
        path = _modelHomeDir + path;

        if (_debugMode)
        {
            LAppPal::PrintLogLn("[APP]load motion: %s => [%s_%d] ", path.GetRawString(), group, i);
        }

        csmByte* buffer;
        csmSizeInt size;
        buffer = CreateBuffer(path.GetRawString(), &size);
        CubismMotion* tmpMotion = static_cast<CubismMotion*>(LoadMotion(buffer, size, name.GetRawString(), NULL, NULL, _modelSetting, group, i));

        if (tmpMotion)
        {
            tmpMotion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);

            if (_motions[name] != NULL)
            {
                ACubismMotion::Delete(_motions[name]);
            }
            _motions[name] = tmpMotion;
        }

        DeleteBuffer(buffer, path.GetRawString());
    }
}

void LAppModel::ReleaseMotionGroup(const csmChar* group) const
{
    const csmInt32 count = _modelSetting->GetMotionCount(group);
    for (csmInt32 i = 0; i < count; i++)
    {
        csmString voice = _modelSetting->GetMotionSoundFileName(group, i);
        if (strcmp(voice.GetRawString(), "") != 0)
        {
            csmString path = voice;
            path = _modelHomeDir + path;
        }
    }
}

/**
* @brief すべてのモーションデータの解放
*
* すべてのモーションデータを解放する。
*/
void LAppModel::ReleaseMotions()
{
    for (csmMap<csmString, ACubismMotion*>::const_iterator iter = _motions.Begin(); iter != _motions.End(); ++iter)
    {
        ACubismMotion::Delete(iter->Second);
    }

    _motions.Clear();
}

/**
* @brief すべての表情データの解放
*
* すべての表情データを解放する。
*/
void LAppModel::ReleaseExpressions()
{
    for (csmMap<csmString, ACubismMotion*>::const_iterator iter = _expressions.Begin(); iter != _expressions.End(); ++iter)
    {
        ACubismMotion::Delete(iter->Second);
    }

    _expressions.Clear();
}

void LAppModel::Update()
{
    LAppPerformanceMonitor& performanceMonitor = LAppPerformanceMonitor::GetInstance();
    const csmFloat32 deltaTimeSeconds = LAppPal::GetDeltaTime();
    _userTimeSeconds += deltaTimeSeconds;

    // モーションによるパラメータ更新の有無
    _motionUpdated = false;

    //-----------------------------------------------------------------
    LAppPerformanceMonitor::TimePoint stageStart = LAppPerformanceMonitor::Now();
    _model->LoadParameters(); // 前回セーブされた状態をロード
    performanceMonitor.AddModelLoadParametersTime(LAppPerformanceMonitor::DurationMs(stageStart));

    stageStart = LAppPerformanceMonitor::Now();
    if (_motionManager->IsFinished())
    {
        // モーションの再生がない場合、待機モーションの中からランダムで再生する
        StartRandomMotion(MotionGroupIdle, PriorityIdle);
        performanceMonitor.AddModelIdleMotionStartTime(LAppPerformanceMonitor::DurationMs(stageStart));
    }
    else
    {
        _motionUpdated = _motionManager->UpdateMotion(_model, deltaTimeSeconds); // モーションを更新
        performanceMonitor.AddModelMotionEvaluateTime(LAppPerformanceMonitor::DurationMs(stageStart));
    }

    stageStart = LAppPerformanceMonitor::Now();
    _model->SaveParameters(); // 状態を保存
    performanceMonitor.AddModelSaveParametersTime(LAppPerformanceMonitor::DurationMs(stageStart));
    //-----------------------------------------------------------------

    // 不透明度
    _opacity = _model->GetModelOpacity();

    stageStart = LAppPerformanceMonitor::Now();
    _updateScheduler.OnLateUpdate(_model, deltaTimeSeconds);
    performanceMonitor.AddModelLateUpdateTime(LAppPerformanceMonitor::DurationMs(stageStart));

    stageStart = LAppPerformanceMonitor::Now();
    _model->Update();
    performanceMonitor.AddModelCoreUpdateTime(LAppPerformanceMonitor::DurationMs(stageStart));

}

CubismMotionQueueEntryHandle LAppModel::StartMotion(const csmChar* group, csmInt32 no, csmInt32 priority, ACubismMotion::FinishedMotionCallback onFinishedMotionHandler, ACubismMotion::BeganMotionCallback onBeganMotionHandler)
{
    if (priority == PriorityForce)
    {
        _motionManager->SetReservePriority(priority);
    }
    else if (!_motionManager->ReserveMotion(priority))
    {
        if (_debugMode)
        {
            LAppPal::PrintLogLn("[APP]can't start motion.");
        }
        return InvalidMotionQueueEntryHandleValue;
    }

    const csmString fileName = _modelSetting->GetMotionFileName(group, no);

    //ex) idle_0
    csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, no);
    CubismMotion* motion = static_cast<CubismMotion*>(_motions[name.GetRawString()]);
    csmBool autoDelete = false;

    if (motion == NULL)
    {
        csmString path = fileName;
        path = _modelHomeDir + path;

        csmByte* buffer;
        csmSizeInt size;
        buffer = CreateBuffer(path.GetRawString(), &size);
        motion = static_cast<CubismMotion*>(LoadMotion(buffer, size, NULL, onFinishedMotionHandler, onBeganMotionHandler, _modelSetting, group, no));

        if (motion)
        {
            motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);
            autoDelete = true; // 終了時にメモリから削除
        }

        DeleteBuffer(buffer, path.GetRawString());
    }
    else
    {
        motion->SetBeganMotionHandler(onBeganMotionHandler);
        motion->SetFinishedMotionHandler(onFinishedMotionHandler);
    }

    if (_debugMode)
    {
        LAppPal::PrintLogLn("[APP]start motion: [%s_%d]", group, no);
    }
    return  _motionManager->StartMotionPriority(motion, autoDelete, priority);
}

CubismMotionQueueEntryHandle LAppModel::StartRandomMotion(const csmChar* group, csmInt32 priority, ACubismMotion::FinishedMotionCallback onFinishedMotionHandler, ACubismMotion::BeganMotionCallback onBeganMotionHandler)
{
    if (_modelSetting->GetMotionCount(group) == 0)
    {
        return InvalidMotionQueueEntryHandleValue;
    }

    csmInt32 no = rand() % _modelSetting->GetMotionCount(group);

    return StartMotion(group, no, priority, onFinishedMotionHandler, onBeganMotionHandler);
}

void LAppModel::DoDraw()
{
    if (_model == NULL)
    {
        return;
    }

    GetRenderer<Rendering::CubismRenderer_PortableGL>()->DrawModel();
}

void LAppModel::Draw(CubismMatrix44& matrix)
{
    if (_model == NULL)
    {
        return;
    }

    LAppPerformanceMonitor& performanceMonitor = LAppPerformanceMonitor::GetInstance();
    const LAppPerformanceMonitor::TimePoint matrixStart = LAppPerformanceMonitor::Now();
    matrix.MultiplyByMatrix(_modelMatrix);

    GetRenderer<Rendering::CubismRenderer_PortableGL>()->SetMvpMatrix(&matrix);
    performanceMonitor.AddModelMatrixTime(LAppPerformanceMonitor::DurationMs(matrixStart));

    const LAppPerformanceMonitor::TimePoint submitStart = LAppPerformanceMonitor::Now();
    DoDraw();
    performanceMonitor.AddModelSubmitTime(LAppPerformanceMonitor::DurationMs(submitStart));

    const Rendering::CubismRenderer_PortableGL::FrameStats& rendererStats =
        GetRenderer<Rendering::CubismRenderer_PortableGL>()->GetLastFrameStats();
    performanceMonitor.AddModelMaskPassTime(rendererStats.MaskPassMilliseconds);
    performanceMonitor.AddModelMainDrawPassTime(rendererStats.MainDrawMilliseconds);
    performanceMonitor.AddModelSubmitRenderTargetTime(rendererStats.SubmitRenderTargetMilliseconds);
    performanceMonitor.AddModelSubmitSortTime(rendererStats.SubmitSortMilliseconds);
    performanceMonitor.AddModelSubmitRenderObjectTime(rendererStats.SubmitRenderObjectMilliseconds);
    performanceMonitor.AddModelSubmitTailFlushTime(rendererStats.SubmitTailFlushMilliseconds);
    performanceMonitor.AddModelSubmitParentOffscreenTime(rendererStats.SubmitParentSubmitMilliseconds);
    performanceMonitor.AddModelSubmitAddOffscreenTime(rendererStats.SubmitAddOffscreenMilliseconds);
    performanceMonitor.AddModelSubmitDrawOffscreenTime(rendererStats.SubmitDrawOffscreenMilliseconds);
    performanceMonitor.AddModelSubmitMeshTime(rendererStats.SubmitMeshTotalMilliseconds);
    performanceMonitor.AddModelSubmitMeshShaderTime(rendererStats.SubmitMeshShaderMilliseconds);
    performanceMonitor.AddModelSubmitMeshDrawTime(rendererStats.SubmitMeshDrawMilliseconds);
    performanceMonitor.AddModelSubmitCompositeTime(rendererStats.SubmitCompositeMilliseconds);
    performanceMonitor.AddModelPipelineTime(rendererStats.PipelineMilliseconds);
    performanceMonitor.AddModelVertexStageTime(rendererStats.VertexStageMilliseconds);
    performanceMonitor.AddModelPrimitiveLoopTime(rendererStats.PrimitiveLoopMilliseconds);
    performanceMonitor.AddModelTriangleFastTime(rendererStats.TriangleFastMilliseconds);
    performanceMonitor.AddModelTriangleSampleTime(rendererStats.TriangleSampleMilliseconds);
    performanceMonitor.AddModelTriangleShadeTime(rendererStats.TriangleShadeMilliseconds);
    performanceMonitor.AddModelTriangleBlendTime(rendererStats.TriangleBlendMilliseconds);
    performanceMonitor.AddModelTriangleScalarTime(rendererStats.TriangleScalarMilliseconds);
    performanceMonitor.AddModelTriangleScalarTailTime(rendererStats.TriangleScalarTailMilliseconds);
    performanceMonitor.AddModelPipelineCallCount(rendererStats.PipelineCallCount);
    performanceMonitor.AddModelFastTriangleCount(rendererStats.FastTriangleCount);
    performanceMonitor.AddModelScalarTriangleCount(rendererStats.ScalarTriangleCount);
    performanceMonitor.AddModelSampleCallCount(rendererStats.SampleCallCount);
    performanceMonitor.AddModelVisibleDrawableCount(rendererStats.VisibleDrawableCount);
    performanceMonitor.AddModelMaskedDrawableCount(rendererStats.MaskedDrawableCount);
    performanceMonitor.AddModelMaskDrawCallCount(rendererStats.MaskDrawCallCount);
    performanceMonitor.AddModelMainDrawCallCount(rendererStats.MainDrawCallCount);
    performanceMonitor.AddModelIndexCount(rendererStats.TotalIndexCount);
    performanceMonitor.AddModelTriangleCount(rendererStats.TotalTriangleCount);
    performanceMonitor.AddModelMaskIndexCount(rendererStats.MaskIndexCount);
    performanceMonitor.AddModelMaskTriangleCount(rendererStats.MaskTriangleCount);
    performanceMonitor.AddModelMainIndexCount(rendererStats.MainIndexCount);
    performanceMonitor.AddModelMainTriangleCount(rendererStats.MainTriangleCount);
}

csmBool LAppModel::HitTest(const csmChar* hitAreaName, csmFloat32 x, csmFloat32 y)
{
    // 透明時は当たり判定なし。
    if (_opacity < 1)
    {
        return false;
    }
    const csmInt32 count = _modelSetting->GetHitAreasCount();
    for (csmInt32 i = 0; i < count; i++)
    {
        if (strcmp(_modelSetting->GetHitAreaName(i), hitAreaName) == 0)
        {
            const CubismIdHandle drawID = _modelSetting->GetHitAreaId(i);
            return IsHit(drawID, x, y);
        }
    }
    return false; // 存在しない場合はfalse
}

void LAppModel::SetExpression(const csmChar* expressionID)
{
    ACubismMotion* motion = _expressions[expressionID];
    if (_debugMode)
    {
        LAppPal::PrintLogLn("[APP]expression: [%s]", expressionID);
    }

    if (motion != NULL)
    {
        _expressionManager->StartMotion(motion, false);
    }
    else
    {
        if (_debugMode) LAppPal::PrintLogLn("[APP]expression[%s] is null ", expressionID);
    }
}

void LAppModel::SetRandomExpression()
{
    if (_expressions.GetSize() == 0)
    {
        return;
    }

    csmInt32 no = rand() % _expressions.GetSize();
    csmMap<csmString, ACubismMotion*>::const_iterator map_ite;
    csmInt32 i = 0;
    for (map_ite = _expressions.Begin(); map_ite != _expressions.End(); map_ite++)
    {
        if (i == no)
        {
            csmString name = (*map_ite).First;
            SetExpression(name.GetRawString());
            return;
        }
        i++;
    }
}

void LAppModel::ReloadRenderer()
{
    DeleteRenderer();
    ReleaseOwnedTextures();

    CreateRenderer(LAppDelegate::GetInstance()->GetWindowWidth(), LAppDelegate::GetInstance()->GetWindowHeight());
#ifdef CSM_TARGET_ESP_PGL
    ConfigurePortableGLMaskBuffers(*this);
#endif

    SetupTextures();
}

void LAppModel::SetupTextures()
{
    ReleaseOwnedTextures();

    for (csmInt32 modelTextureNumber = 0; modelTextureNumber < _modelSetting->GetTextureCount(); modelTextureNumber++)
    {
        // テクスチャ名が空文字だった場合はロード・バインド処理をスキップ
        if (strcmp(_modelSetting->GetTextureFileName(modelTextureNumber), "") == 0)
        {
            continue;
        }

        //OpenGLのテクスチャユニットにテクスチャをロードする
        csmString texturePath = _modelSetting->GetTextureFileName(modelTextureNumber);
        texturePath = _modelHomeDir + texturePath;

        LAppTextureManager::TextureInfo* texture =
            LAppDelegate::GetInstance()->GetTextureManager()->CreateTextureFromPngFile(
                texturePath.GetRawString(),
                LAppTextureManager::TextureUsage_Model);
        if (texture == NULL)
        {
            continue;
        }

        _ownedTextureFiles.push_back(texturePath.GetRawString());
        const csmInt32 glTextueNumber = texture->id;

        //OpenGL
        GetRenderer<Rendering::CubismRenderer_PortableGL>()->BindTexture(modelTextureNumber, glTextueNumber);
    }

#ifdef PREMULTIPLIED_ALPHA_ENABLE
    GetRenderer<Rendering::CubismRenderer_PortableGL>()->IsPremultipliedAlpha(true);
#else
    GetRenderer<Rendering::CubismRenderer_PortableGL>()->IsPremultipliedAlpha(false);
#endif

}

void LAppModel::ReleaseOwnedTextures()
{
    LAppTextureManager* textureManager = LAppDelegate::GetInstance() != NULL
        ? LAppDelegate::GetInstance()->GetTextureManager()
        : NULL;
    if (textureManager == NULL)
    {
        _ownedTextureFiles.clear();
        return;
    }

    for (std::size_t i = 0; i < _ownedTextureFiles.size(); ++i)
    {
        textureManager->ReleaseTexture(_ownedTextureFiles[i]);
    }

    _ownedTextureFiles.clear();
}

void LAppModel::MotionEventFired(const csmString& eventValue)
{
    CubismLogInfo("%s is fired on LAppModel!!", eventValue.GetRawString());
}

Csm::Rendering::CubismRenderTarget_PortableGL& LAppModel::GetRenderBuffer()
{
    return _renderBuffer;
}
