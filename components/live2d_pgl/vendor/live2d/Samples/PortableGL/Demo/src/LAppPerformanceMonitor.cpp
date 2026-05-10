#include "LAppPerformanceMonitor.hpp"

#include <cstdio>
#include <cstring>
#if !defined(CSM_TARGET_ESP_PGL)
#include <fstream>
#include <sstream>
#include <chrono>
#else
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif
#include <algorithm>
#include <limits>
#include <string>

#include <Rendering/PortableGL/CubismRenderTarget_PortableGL.hpp>

#include "LAppDelegate.hpp"
#include "LAppTextureManager.hpp"
#include "LAppX11Window.hpp"

namespace {
constexpr double ReportIntervalSeconds = 1.0;
constexpr double BytesPerMiB = 1024.0 * 1024.0;

double DurationMsBetween(LAppPerformanceMonitor::TimePoint startTime, LAppPerformanceMonitor::TimePoint endTime)
{
    return static_cast<double>(endTime - startTime) / 1000.0;
}

double DurationSecondsBetween(LAppPerformanceMonitor::TimePoint startTime, LAppPerformanceMonitor::TimePoint endTime)
{
    return static_cast<double>(endTime - startTime) / 1000000.0;
}

double ToMiB(std::size_t bytes)
{
    return static_cast<double>(bytes) / BytesPerMiB;
}

const char* TextureUsageName(int usage)
{
    switch (usage)
    {
    case LAppTextureManager::TextureUsage_Model:
        return "model";
    case LAppTextureManager::TextureUsage_Ui:
        return "ui";
    default:
        return "other";
    }
}

std::string ShortTextureName(const std::string& fileName)
{
    const std::string marker = "/Resources/";
    const std::size_t markerPos = fileName.find(marker);
    if (markerPos != std::string::npos)
    {
        return fileName.substr(markerPos + marker.size());
    }

    const std::size_t fallbackPos = fileName.find("Resources/");
    if (fallbackPos != std::string::npos)
    {
        return fileName.substr(fallbackPos + std::strlen("Resources/"));
    }

    return fileName;
}

std::size_t ParseStatusBytes(const char* key)
{
#if defined(CSM_TARGET_ESP_PGL)
    (void)key;
    return 0;
#else
    std::ifstream statusFile("/proc/self/status");
    if (!statusFile)
    {
        return 0;
    }

    std::string line;
    while (std::getline(statusFile, line))
    {
        if (line.compare(0, std::strlen(key), key) != 0)
        {
            continue;
        }

        std::istringstream stream(line.substr(std::strlen(key)));
        std::size_t valueKb = 0;
        stream >> valueKb;
        return valueKb * 1024;
    }

    return 0;
#endif
}
}

LAppPerformanceMonitor& LAppPerformanceMonitor::GetInstance()
{
    static LAppPerformanceMonitor instance;
    return instance;
}

LAppPerformanceMonitor::TimePoint LAppPerformanceMonitor::Now()
{
#if defined(CSM_TARGET_ESP_PGL)
    return static_cast<TimePoint>(esp_timer_get_time());
#else
    return static_cast<TimePoint>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

double LAppPerformanceMonitor::DurationMs(const TimePoint& startTime)
{
    return DurationMsBetween(startTime, Now());
}

LAppPerformanceMonitor::RunningStat::RunningStat()
    : Total(0.0)
    , Min(std::numeric_limits<double>::max())
    , Max(0.0)
    , Samples(0)
{
}

void LAppPerformanceMonitor::RunningStat::Add(double value)
{
    Total += value;
    if (Samples == 0 || value < Min)
    {
        Min = value;
    }
    if (Samples == 0 || value > Max)
    {
        Max = value;
    }
    ++Samples;
}

void LAppPerformanceMonitor::RunningStat::Reset()
{
    Total = 0.0;
    Min = std::numeric_limits<double>::max();
    Max = 0.0;
    Samples = 0;
}

double LAppPerformanceMonitor::RunningStat::Average() const
{
    return Samples == 0 ? 0.0 : Total / static_cast<double>(Samples);
}

LAppPerformanceMonitor::LAppPerformanceMonitor()
    : _frameStartTime()
    , _intervalStartTime(Now())
    , _frameWidth(0)
    , _frameHeight(0)
    , _clearMs(0.0)
    , _backgroundMs(0.0)
    , _uiMs(0.0)
    , _modelTotalMs(0.0)
    , _modelFrameBeginMs(0.0)
    , _modelFrameEndMs(0.0)
    , _modelUpdateMs(0.0)
    , _modelDrawMs(0.0)
    , _modelLoadParametersMs(0.0)
    , _modelIdleMotionStartMs(0.0)
    , _modelMotionEvaluateMs(0.0)
    , _modelSaveParametersMs(0.0)
    , _modelLateUpdateMs(0.0)
    , _modelCoreUpdateMs(0.0)
    , _modelPreDrawMs(0.0)
    , _modelMatrixMs(0.0)
    , _modelSubmitMs(0.0)
    , _modelPostDrawMs(0.0)
    , _modelMaskPassMs(0.0)
    , _modelMainDrawPassMs(0.0)
    , _modelSubmitRenderTargetMs(0.0)
    , _modelSubmitSortMs(0.0)
    , _modelSubmitRenderObjectMs(0.0)
    , _modelSubmitTailFlushMs(0.0)
    , _modelSubmitParentOffscreenMs(0.0)
    , _modelSubmitAddOffscreenMs(0.0)
    , _modelSubmitDrawOffscreenMs(0.0)
    , _modelSubmitMeshMs(0.0)
    , _modelSubmitMeshShaderMs(0.0)
    , _modelSubmitMeshDrawMs(0.0)
    , _modelSubmitCompositeMs(0.0)
    , _modelPipelineMs(0.0)
    , _modelVertexStageMs(0.0)
    , _modelPrimitiveLoopMs(0.0)
    , _modelTriangleFastMs(0.0)
    , _modelTriangleSampleMs(0.0)
    , _modelTriangleShadeMs(0.0)
    , _modelTriangleBlendMs(0.0)
    , _modelTriangleScalarMs(0.0)
    , _modelTriangleScalarTailMs(0.0)
    , _modelPipelineCallCount(0.0)
    , _modelFastTriangleCount(0.0)
    , _modelScalarTriangleCount(0.0)
    , _modelSampleCallCount(0.0)
    , _modelVisibleDrawableCount(0.0)
    , _modelMaskedDrawableCount(0.0)
    , _modelMaskDrawCallCount(0.0)
    , _modelMainDrawCallCount(0.0)
    , _modelIndexCount(0.0)
    , _modelTriangleCount(0.0)
    , _modelMaskIndexCount(0.0)
    , _modelMaskTriangleCount(0.0)
    , _modelMainIndexCount(0.0)
    , _modelMainTriangleCount(0.0)
    , _presentMs(0.0)
    , _presentCopyMs(0.0)
    , _presentFlushMs(0.0)
    , _intervalFrameCount(0)
{
}

void LAppPerformanceMonitor::BeginFrame(int width, int height)
{
    _frameWidth = width;
    _frameHeight = height;
    _frameStartTime = Now();
    ResetCurrentFrame();
}

void LAppPerformanceMonitor::EndFrame()
{
    if (_frameWidth <= 0 || _frameHeight <= 0)
    {
        return;
    }

    const TimePoint now = Now();

    _frameStat.Add(DurationMsBetween(_frameStartTime, now));
    _clearStat.Add(_clearMs);
    _backgroundStat.Add(_backgroundMs);
    _uiStat.Add(_uiMs);
    _modelTotalStat.Add(_modelTotalMs);
    _modelFrameBeginStat.Add(_modelFrameBeginMs);
    _modelFrameEndStat.Add(_modelFrameEndMs);
    _modelUpdateStat.Add(_modelUpdateMs);
    _modelDrawStat.Add(_modelDrawMs);
    _modelLoadParametersStat.Add(_modelLoadParametersMs);
    _modelIdleMotionStartStat.Add(_modelIdleMotionStartMs);
    _modelMotionEvaluateStat.Add(_modelMotionEvaluateMs);
    _modelSaveParametersStat.Add(_modelSaveParametersMs);
    _modelLateUpdateStat.Add(_modelLateUpdateMs);
    _modelCoreUpdateStat.Add(_modelCoreUpdateMs);
    _modelPreDrawStat.Add(_modelPreDrawMs);
    _modelMatrixStat.Add(_modelMatrixMs);
    _modelSubmitStat.Add(_modelSubmitMs);
    _modelPostDrawStat.Add(_modelPostDrawMs);
    _modelMaskPassStat.Add(_modelMaskPassMs);
    _modelMainDrawPassStat.Add(_modelMainDrawPassMs);
    _modelSubmitRenderTargetStat.Add(_modelSubmitRenderTargetMs);
    _modelSubmitSortStat.Add(_modelSubmitSortMs);
    _modelSubmitRenderObjectStat.Add(_modelSubmitRenderObjectMs);
    _modelSubmitTailFlushStat.Add(_modelSubmitTailFlushMs);
    _modelSubmitParentOffscreenStat.Add(_modelSubmitParentOffscreenMs);
    _modelSubmitAddOffscreenStat.Add(_modelSubmitAddOffscreenMs);
    _modelSubmitDrawOffscreenStat.Add(_modelSubmitDrawOffscreenMs);
    _modelSubmitMeshStat.Add(_modelSubmitMeshMs);
    _modelSubmitMeshShaderStat.Add(_modelSubmitMeshShaderMs);
    _modelSubmitMeshDrawStat.Add(_modelSubmitMeshDrawMs);
    _modelSubmitCompositeStat.Add(_modelSubmitCompositeMs);
    _modelPipelineStat.Add(_modelPipelineMs);
    _modelVertexStageStat.Add(_modelVertexStageMs);
    _modelPrimitiveLoopStat.Add(_modelPrimitiveLoopMs);
    _modelTriangleFastStat.Add(_modelTriangleFastMs);
    _modelTriangleSampleStat.Add(_modelTriangleSampleMs);
    _modelTriangleShadeStat.Add(_modelTriangleShadeMs);
    _modelTriangleBlendStat.Add(_modelTriangleBlendMs);
    _modelTriangleScalarStat.Add(_modelTriangleScalarMs);
    _modelTriangleScalarTailStat.Add(_modelTriangleScalarTailMs);
    _modelPipelineCallStat.Add(_modelPipelineCallCount);
    _modelFastTriangleCountStat.Add(_modelFastTriangleCount);
    _modelScalarTriangleCountStat.Add(_modelScalarTriangleCount);
    _modelSampleCallCountStat.Add(_modelSampleCallCount);
    _modelVisibleDrawableStat.Add(_modelVisibleDrawableCount);
    _modelMaskedDrawableStat.Add(_modelMaskedDrawableCount);
    _modelMaskDrawCallStat.Add(_modelMaskDrawCallCount);
    _modelMainDrawCallStat.Add(_modelMainDrawCallCount);
    _modelIndexCountStat.Add(_modelIndexCount);
    _modelTriangleCountStat.Add(_modelTriangleCount);
    _modelMaskIndexCountStat.Add(_modelMaskIndexCount);
    _modelMaskTriangleCountStat.Add(_modelMaskTriangleCount);
    _modelMainIndexCountStat.Add(_modelMainIndexCount);
    _modelMainTriangleCountStat.Add(_modelMainTriangleCount);
    _presentStat.Add(_presentMs);
    _presentCopyStat.Add(_presentCopyMs);
    _presentFlushStat.Add(_presentFlushMs);

    EnsureModelSlotCount(_currentModelStats.size());
    for (std::size_t i = 0; i < _currentModelStats.size(); ++i)
    {
        _intervalModelStats[i].Update.Add(_currentModelStats[i].UpdateMs);
        _intervalModelStats[i].Draw.Add(_currentModelStats[i].DrawMs);
    }

    ++_intervalFrameCount;
    ReportIfNeeded(now);
}

void LAppPerformanceMonitor::Flush()
{
    if (_intervalFrameCount == 0)
    {
        return;
    }

    const TimePoint now = Now();
    const double intervalSeconds = DurationSecondsBetween(_intervalStartTime, now);
    PrintReport(intervalSeconds, SampleMemorySnapshot());
    ResetInterval();
}

void LAppPerformanceMonitor::AddClearTime(double milliseconds)
{
    _clearMs += milliseconds;
}

void LAppPerformanceMonitor::AddBackgroundTime(double milliseconds)
{
    _backgroundMs += milliseconds;
}

void LAppPerformanceMonitor::AddUiTime(double milliseconds)
{
    _uiMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelTotalTime(double milliseconds)
{
    _modelTotalMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelFrameBeginTime(double milliseconds)
{
    _modelFrameBeginMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelFrameEndTime(double milliseconds)
{
    _modelFrameEndMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelUpdateTime(double milliseconds)
{
    _modelUpdateMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelDrawTime(double milliseconds)
{
    _modelDrawMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelLoadParametersTime(double milliseconds)
{
    _modelLoadParametersMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelIdleMotionStartTime(double milliseconds)
{
    _modelIdleMotionStartMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelMotionEvaluateTime(double milliseconds)
{
    _modelMotionEvaluateMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSaveParametersTime(double milliseconds)
{
    _modelSaveParametersMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelLateUpdateTime(double milliseconds)
{
    _modelLateUpdateMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelCoreUpdateTime(double milliseconds)
{
    _modelCoreUpdateMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelPreDrawTime(double milliseconds)
{
    _modelPreDrawMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelMatrixTime(double milliseconds)
{
    _modelMatrixMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitTime(double milliseconds)
{
    _modelSubmitMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelPostDrawTime(double milliseconds)
{
    _modelPostDrawMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelMaskPassTime(double milliseconds)
{
    _modelMaskPassMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelMainDrawPassTime(double milliseconds)
{
    _modelMainDrawPassMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitRenderTargetTime(double milliseconds)
{
    _modelSubmitRenderTargetMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitSortTime(double milliseconds)
{
    _modelSubmitSortMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitRenderObjectTime(double milliseconds)
{
    _modelSubmitRenderObjectMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitTailFlushTime(double milliseconds)
{
    _modelSubmitTailFlushMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitParentOffscreenTime(double milliseconds)
{
    _modelSubmitParentOffscreenMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitAddOffscreenTime(double milliseconds)
{
    _modelSubmitAddOffscreenMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitDrawOffscreenTime(double milliseconds)
{
    _modelSubmitDrawOffscreenMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitMeshTime(double milliseconds)
{
    _modelSubmitMeshMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitMeshShaderTime(double milliseconds)
{
    _modelSubmitMeshShaderMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitMeshDrawTime(double milliseconds)
{
    _modelSubmitMeshDrawMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelSubmitCompositeTime(double milliseconds)
{
    _modelSubmitCompositeMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelPipelineTime(double milliseconds)
{
    _modelPipelineMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelVertexStageTime(double milliseconds)
{
    _modelVertexStageMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelPrimitiveLoopTime(double milliseconds)
{
    _modelPrimitiveLoopMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelTriangleFastTime(double milliseconds)
{
    _modelTriangleFastMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelTriangleSampleTime(double milliseconds)
{
    _modelTriangleSampleMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelTriangleShadeTime(double milliseconds)
{
    _modelTriangleShadeMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelTriangleBlendTime(double milliseconds)
{
    _modelTriangleBlendMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelTriangleScalarTime(double milliseconds)
{
    _modelTriangleScalarMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelTriangleScalarTailTime(double milliseconds)
{
    _modelTriangleScalarTailMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelPipelineCallCount(std::size_t count)
{
    _modelPipelineCallCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelFastTriangleCount(std::size_t count)
{
    _modelFastTriangleCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelScalarTriangleCount(std::size_t count)
{
    _modelScalarTriangleCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelSampleCallCount(std::size_t count)
{
    _modelSampleCallCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelVisibleDrawableCount(std::size_t count)
{
    _modelVisibleDrawableCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelMaskedDrawableCount(std::size_t count)
{
    _modelMaskedDrawableCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelMaskDrawCallCount(std::size_t count)
{
    _modelMaskDrawCallCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelMainDrawCallCount(std::size_t count)
{
    _modelMainDrawCallCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelIndexCount(std::size_t count)
{
    _modelIndexCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelTriangleCount(std::size_t count)
{
    _modelTriangleCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelMaskIndexCount(std::size_t count)
{
    _modelMaskIndexCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelMaskTriangleCount(std::size_t count)
{
    _modelMaskTriangleCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelMainIndexCount(std::size_t count)
{
    _modelMainIndexCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelMainTriangleCount(std::size_t count)
{
    _modelMainTriangleCount += static_cast<double>(count);
}

void LAppPerformanceMonitor::AddModelUpdateTime(int modelIndex, double milliseconds)
{
    if (modelIndex < 0)
    {
        return;
    }

    EnsureModelSlotCount(static_cast<std::size_t>(modelIndex + 1));
    _currentModelStats[static_cast<std::size_t>(modelIndex)].UpdateMs += milliseconds;
}

void LAppPerformanceMonitor::AddModelDrawTime(int modelIndex, double milliseconds)
{
    if (modelIndex < 0)
    {
        return;
    }

    EnsureModelSlotCount(static_cast<std::size_t>(modelIndex + 1));
    _currentModelStats[static_cast<std::size_t>(modelIndex)].DrawMs += milliseconds;
}

void LAppPerformanceMonitor::AddPresentTime(double milliseconds)
{
    _presentMs += milliseconds;
}

void LAppPerformanceMonitor::AddPresentCopyTime(double milliseconds)
{
    _presentCopyMs += milliseconds;
}

void LAppPerformanceMonitor::AddPresentFlushTime(double milliseconds)
{
    _presentFlushMs += milliseconds;
}

void LAppPerformanceMonitor::ResetCurrentFrame()
{
    _clearMs = 0.0;
    _backgroundMs = 0.0;
    _uiMs = 0.0;
    _modelTotalMs = 0.0;
    _modelFrameBeginMs = 0.0;
    _modelFrameEndMs = 0.0;
    _modelUpdateMs = 0.0;
    _modelDrawMs = 0.0;
    _modelLoadParametersMs = 0.0;
    _modelIdleMotionStartMs = 0.0;
    _modelMotionEvaluateMs = 0.0;
    _modelSaveParametersMs = 0.0;
    _modelLateUpdateMs = 0.0;
    _modelCoreUpdateMs = 0.0;
    _modelPreDrawMs = 0.0;
    _modelMatrixMs = 0.0;
    _modelSubmitMs = 0.0;
    _modelPostDrawMs = 0.0;
    _modelMaskPassMs = 0.0;
    _modelMainDrawPassMs = 0.0;
    _modelSubmitRenderTargetMs = 0.0;
    _modelSubmitSortMs = 0.0;
    _modelSubmitRenderObjectMs = 0.0;
    _modelSubmitTailFlushMs = 0.0;
    _modelSubmitParentOffscreenMs = 0.0;
    _modelSubmitAddOffscreenMs = 0.0;
    _modelSubmitDrawOffscreenMs = 0.0;
    _modelSubmitMeshMs = 0.0;
    _modelSubmitMeshShaderMs = 0.0;
    _modelSubmitMeshDrawMs = 0.0;
    _modelSubmitCompositeMs = 0.0;
    _modelPipelineMs = 0.0;
    _modelVertexStageMs = 0.0;
    _modelPrimitiveLoopMs = 0.0;
    _modelTriangleFastMs = 0.0;
    _modelTriangleSampleMs = 0.0;
    _modelTriangleShadeMs = 0.0;
    _modelTriangleBlendMs = 0.0;
    _modelTriangleScalarMs = 0.0;
    _modelTriangleScalarTailMs = 0.0;
    _modelPipelineCallCount = 0.0;
    _modelFastTriangleCount = 0.0;
    _modelScalarTriangleCount = 0.0;
    _modelSampleCallCount = 0.0;
    _modelVisibleDrawableCount = 0.0;
    _modelMaskedDrawableCount = 0.0;
    _modelMaskDrawCallCount = 0.0;
    _modelMainDrawCallCount = 0.0;
    _modelIndexCount = 0.0;
    _modelTriangleCount = 0.0;
    _modelMaskIndexCount = 0.0;
    _modelMaskTriangleCount = 0.0;
    _modelMainIndexCount = 0.0;
    _modelMainTriangleCount = 0.0;
    _presentMs = 0.0;
    _presentCopyMs = 0.0;
    _presentFlushMs = 0.0;

    for (std::size_t i = 0; i < _currentModelStats.size(); ++i)
    {
        _currentModelStats[i].UpdateMs = 0.0;
        _currentModelStats[i].DrawMs = 0.0;
    }
}

void LAppPerformanceMonitor::ResetInterval()
{
    _intervalFrameCount = 0;
    _intervalStartTime = Now();
    _frameStat.Reset();
    _clearStat.Reset();
    _backgroundStat.Reset();
    _uiStat.Reset();
    _modelTotalStat.Reset();
    _modelFrameBeginStat.Reset();
    _modelFrameEndStat.Reset();
    _modelUpdateStat.Reset();
    _modelDrawStat.Reset();
    _modelLoadParametersStat.Reset();
    _modelIdleMotionStartStat.Reset();
    _modelMotionEvaluateStat.Reset();
    _modelSaveParametersStat.Reset();
    _modelLateUpdateStat.Reset();
    _modelCoreUpdateStat.Reset();
    _modelPreDrawStat.Reset();
    _modelMatrixStat.Reset();
    _modelSubmitStat.Reset();
    _modelPostDrawStat.Reset();
    _modelMaskPassStat.Reset();
    _modelMainDrawPassStat.Reset();
    _modelSubmitRenderTargetStat.Reset();
    _modelSubmitSortStat.Reset();
    _modelSubmitRenderObjectStat.Reset();
    _modelSubmitTailFlushStat.Reset();
    _modelSubmitParentOffscreenStat.Reset();
    _modelSubmitAddOffscreenStat.Reset();
    _modelSubmitDrawOffscreenStat.Reset();
    _modelSubmitMeshStat.Reset();
    _modelSubmitMeshShaderStat.Reset();
    _modelSubmitMeshDrawStat.Reset();
    _modelSubmitCompositeStat.Reset();
    _modelPipelineStat.Reset();
    _modelVertexStageStat.Reset();
    _modelPrimitiveLoopStat.Reset();
    _modelTriangleFastStat.Reset();
    _modelTriangleSampleStat.Reset();
    _modelTriangleShadeStat.Reset();
    _modelTriangleBlendStat.Reset();
    _modelTriangleScalarStat.Reset();
    _modelTriangleScalarTailStat.Reset();
    _modelPipelineCallStat.Reset();
    _modelFastTriangleCountStat.Reset();
    _modelScalarTriangleCountStat.Reset();
    _modelSampleCallCountStat.Reset();
    _modelVisibleDrawableStat.Reset();
    _modelMaskedDrawableStat.Reset();
    _modelMaskDrawCallStat.Reset();
    _modelMainDrawCallStat.Reset();
    _modelIndexCountStat.Reset();
    _modelTriangleCountStat.Reset();
    _modelMaskIndexCountStat.Reset();
    _modelMaskTriangleCountStat.Reset();
    _modelMainIndexCountStat.Reset();
    _modelMainTriangleCountStat.Reset();
    _presentStat.Reset();
    _presentCopyStat.Reset();
    _presentFlushStat.Reset();

    for (std::size_t i = 0; i < _intervalModelStats.size(); ++i)
    {
        _intervalModelStats[i].Update.Reset();
        _intervalModelStats[i].Draw.Reset();
    }
}

void LAppPerformanceMonitor::EnsureModelSlotCount(std::size_t count)
{
    if (_currentModelStats.size() < count)
    {
        _currentModelStats.resize(count);
    }
    if (_intervalModelStats.size() < count)
    {
        _intervalModelStats.resize(count);
    }
}

void LAppPerformanceMonitor::ReportIfNeeded(const TimePoint& now)
{
    const double intervalSeconds = DurationSecondsBetween(_intervalStartTime, now);
    if (_intervalFrameCount == 0 || intervalSeconds < ReportIntervalSeconds)
    {
        return;
    }

    PrintReport(intervalSeconds, SampleMemorySnapshot());
    ResetInterval();
}

void LAppPerformanceMonitor::PrintReport(double intervalSeconds, const MemorySnapshot& snapshot) const
{
    const double fps = intervalSeconds > 0.0 ? static_cast<double>(_intervalFrameCount) / intervalSeconds : 0.0;
    const double gfxTotalMiB = ToMiB(snapshot.TextureBytes + snapshot.RenderTargetBytes + snapshot.MainBackBufferBytes + snapshot.PresentBufferBytes);

    std::fprintf(stdout,
        "[PERF] interval=%.2fs frames=%zu fps=%.2f size=%dx%d\n",
        intervalSeconds,
        _intervalFrameCount,
        fps,
        _frameWidth,
        _frameHeight);

    std::fprintf(stdout,
        "[PERF] frame_ms avg/min/max: total=%.3f/%.3f/%.3f model=%.3f/%.3f/%.3f present=%.3f/%.3f/%.3f clear=%.3f/%.3f/%.3f\n",
        _frameStat.Average(), _frameStat.Min, _frameStat.Max,
        _modelTotalStat.Average(), _modelTotalStat.Min, _modelTotalStat.Max,
        _presentStat.Average(), _presentStat.Min, _presentStat.Max,
        _clearStat.Average(), _clearStat.Min, _clearStat.Max);

    std::fprintf(stdout,
        "[PERF] model_ms avg/min/max: update=%.3f/%.3f/%.3f draw=%.3f/%.3f/%.3f mask=%.3f/%.3f/%.3f main=%.3f/%.3f/%.3f\n",
        _modelUpdateStat.Average(), _modelUpdateStat.Min, _modelUpdateStat.Max,
        _modelDrawStat.Average(), _modelDrawStat.Min, _modelDrawStat.Max,
        _modelMaskPassStat.Average(), _modelMaskPassStat.Min, _modelMaskPassStat.Max,
        _modelMainDrawPassStat.Average(), _modelMainDrawPassStat.Min, _modelMainDrawPassStat.Max);

    std::fprintf(stdout,
        "[PERF] submit_ms avg/min/max: submit=%.3f/%.3f/%.3f mesh=%.3f/%.3f/%.3f mesh_shader=%.3f/%.3f/%.3f mesh_draw=%.3f/%.3f/%.3f present_flush=%.3f/%.3f/%.3f\n",
        _modelSubmitStat.Average(), _modelSubmitStat.Min, _modelSubmitStat.Max,
        _modelSubmitMeshStat.Average(), _modelSubmitMeshStat.Min, _modelSubmitMeshStat.Max,
        _modelSubmitMeshShaderStat.Average(), _modelSubmitMeshShaderStat.Min, _modelSubmitMeshShaderStat.Max,
        _modelSubmitMeshDrawStat.Average(), _modelSubmitMeshDrawStat.Min, _modelSubmitMeshDrawStat.Max,
        _presentFlushStat.Average(), _presentFlushStat.Min, _presentFlushStat.Max);

    std::fprintf(stdout,
        "[PERF] counts avg/min/max: visible_drawables=%.1f/%.1f/%.1f mask_draws=%.1f/%.1f/%.1f main_draws=%.1f/%.1f/%.1f triangles=%.1f/%.1f/%.1f sample_calls=%.1f/%.1f/%.1f\n",
        _modelVisibleDrawableStat.Average(), _modelVisibleDrawableStat.Min, _modelVisibleDrawableStat.Max,
        _modelMaskDrawCallStat.Average(), _modelMaskDrawCallStat.Min, _modelMaskDrawCallStat.Max,
        _modelMainDrawCallStat.Average(), _modelMainDrawCallStat.Min, _modelMainDrawCallStat.Max,
        _modelTriangleCountStat.Average(), _modelTriangleCountStat.Min, _modelTriangleCountStat.Max,
        _modelSampleCallCountStat.Average(), _modelSampleCallCountStat.Min, _modelSampleCallCountStat.Max);

#if defined(CSM_TARGET_ESP_PGL)
    std::fprintf(stdout,
        "[PERF] memory_mib: heap_free_8bit=%.2f heap_min_8bit=%.2f heap_largest_8bit=%.2f free_internal=%.2f largest_internal=%.2f free_spiram=%.2f gfx_est=%.2f\n",
        ToMiB(snapshot.HeapFree8BitBytes),
        ToMiB(snapshot.HeapMinimumFree8BitBytes),
        ToMiB(snapshot.HeapLargestFree8BitBytes),
        ToMiB(snapshot.HeapFreeInternalBytes),
        ToMiB(snapshot.HeapLargestFreeInternalBytes),
        ToMiB(snapshot.HeapFreeSpiramBytes),
        gfxTotalMiB);
#endif

    std::fprintf(stdout,
        "[PERF] gfx_mib: textures=%.2f(%zu) model=%.2f(%zu) ui=%.2f(%zu) other=%.2f(%zu) render_targets=%.2f(%zu) main_fb=%.2f present_buf=%.2f\n",
        ToMiB(snapshot.TextureBytes),
        snapshot.TextureCount,
        ToMiB(snapshot.ModelTextureBytes),
        snapshot.ModelTextureCount,
        ToMiB(snapshot.UiTextureBytes),
        snapshot.UiTextureCount,
        ToMiB(snapshot.OtherTextureBytes),
        snapshot.OtherTextureCount,
        ToMiB(snapshot.RenderTargetBytes),
        snapshot.RenderTargetCount,
        ToMiB(snapshot.MainBackBufferBytes),
        ToMiB(snapshot.PresentBufferBytes));

    std::fflush(stdout);
}

LAppPerformanceMonitor::MemorySnapshot LAppPerformanceMonitor::SampleMemorySnapshot() const
{
    MemorySnapshot snapshot = {};
#if defined(CSM_TARGET_ESP_PGL)
    snapshot.HeapFree8BitBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    snapshot.HeapMinimumFree8BitBytes = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    snapshot.HeapLargestFree8BitBytes = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    snapshot.HeapFreeInternalBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot.HeapLargestFreeInternalBytes = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot.HeapFreeSpiramBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    snapshot.ProcessResidentBytes = ParseStatusBytes("VmRSS:");
    snapshot.ProcessPeakResidentBytes = ParseStatusBytes("VmHWM:");
    snapshot.ProcessVirtualBytes = ParseStatusBytes("VmSize:");
#endif

    LAppDelegate* delegate = LAppDelegate::GetInstance();
    if (delegate == NULL)
    {
        return snapshot;
    }

    LAppTextureManager* textureManager = delegate->GetTextureManager();
    if (textureManager != NULL)
    {
        const LAppTextureManager::TextureStats textureStats = textureManager->CollectTextureStats();
        snapshot.TextureBytes = textureStats.TotalBytes;
        snapshot.TextureCount = textureStats.TotalCount;
        snapshot.ModelTextureBytes = textureStats.ModelBytes;
        snapshot.ModelTextureCount = textureStats.ModelCount;
        snapshot.UiTextureBytes = textureStats.UiBytes;
        snapshot.UiTextureCount = textureStats.UiCount;
        snapshot.OtherTextureBytes = textureStats.OtherBytes;
        snapshot.OtherTextureCount = textureStats.OtherCount;

        snapshot.TextureDetails.reserve(textureStats.Entries.size());
        for (std::size_t i = 0; i < textureStats.Entries.size(); ++i)
        {
            MemorySnapshot::TextureDetail detail = {};
            detail.FileName = textureStats.Entries[i].FileName;
            detail.Bytes = textureStats.Entries[i].Bytes;
            detail.RefCount = textureStats.Entries[i].RefCount;
            detail.Usage = textureStats.Entries[i].Usage;
            snapshot.TextureDetails.push_back(detail);
        }
    }

    LAppX11Window* window = delegate->GetWindow();
    if (window != NULL)
    {
        snapshot.MainBackBufferBytes = window->GetBackBufferBytes();
        snapshot.PresentBufferBytes = window->GetPresentBufferBytes();
    }

    snapshot.RenderTargetBytes = Live2D::Cubism::Framework::Rendering::CubismRenderTarget_PortableGL::GetAllocatedRenderTargetBytes();
    snapshot.RenderTargetCount = Live2D::Cubism::Framework::Rendering::CubismRenderTarget_PortableGL::GetAllocatedRenderTargetCount();
    return snapshot;
}
