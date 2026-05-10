#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class LAppPerformanceMonitor
{
public:
    static LAppPerformanceMonitor& GetInstance();

    using TimePoint = std::uint64_t;

    static TimePoint Now();
    static double DurationMs(const TimePoint& startTime);

    void BeginFrame(int width, int height);
    void EndFrame();
    void Flush();

    void AddClearTime(double milliseconds);
    void AddBackgroundTime(double milliseconds);
    void AddUiTime(double milliseconds);
    void AddModelTotalTime(double milliseconds);
    void AddModelFrameBeginTime(double milliseconds);
    void AddModelFrameEndTime(double milliseconds);
    void AddModelUpdateTime(double milliseconds);
    void AddModelDrawTime(double milliseconds);
    void AddModelLoadParametersTime(double milliseconds);
    void AddModelIdleMotionStartTime(double milliseconds);
    void AddModelMotionEvaluateTime(double milliseconds);
    void AddModelSaveParametersTime(double milliseconds);
    void AddModelLateUpdateTime(double milliseconds);
    void AddModelCoreUpdateTime(double milliseconds);
    void AddModelPreDrawTime(double milliseconds);
    void AddModelMatrixTime(double milliseconds);
    void AddModelSubmitTime(double milliseconds);
    void AddModelPostDrawTime(double milliseconds);
    void AddModelMaskPassTime(double milliseconds);
    void AddModelMainDrawPassTime(double milliseconds);
    void AddModelSubmitRenderTargetTime(double milliseconds);
    void AddModelSubmitSortTime(double milliseconds);
    void AddModelSubmitRenderObjectTime(double milliseconds);
    void AddModelSubmitTailFlushTime(double milliseconds);
    void AddModelSubmitParentOffscreenTime(double milliseconds);
    void AddModelSubmitAddOffscreenTime(double milliseconds);
    void AddModelSubmitDrawOffscreenTime(double milliseconds);
    void AddModelSubmitMeshTime(double milliseconds);
    void AddModelSubmitMeshShaderTime(double milliseconds);
    void AddModelSubmitMeshDrawTime(double milliseconds);
    void AddModelSubmitCompositeTime(double milliseconds);
    void AddModelPipelineTime(double milliseconds);
    void AddModelVertexStageTime(double milliseconds);
    void AddModelPrimitiveLoopTime(double milliseconds);
    void AddModelTriangleFastTime(double milliseconds);
    void AddModelTriangleSampleTime(double milliseconds);
    void AddModelTriangleShadeTime(double milliseconds);
    void AddModelTriangleBlendTime(double milliseconds);
    void AddModelTriangleScalarTime(double milliseconds);
    void AddModelTriangleScalarTailTime(double milliseconds);
    void AddModelPipelineCallCount(std::size_t count);
    void AddModelFastTriangleCount(std::size_t count);
    void AddModelScalarTriangleCount(std::size_t count);
    void AddModelSampleCallCount(std::size_t count);
    void AddModelVisibleDrawableCount(std::size_t count);
    void AddModelMaskedDrawableCount(std::size_t count);
    void AddModelMaskDrawCallCount(std::size_t count);
    void AddModelMainDrawCallCount(std::size_t count);
    void AddModelIndexCount(std::size_t count);
    void AddModelTriangleCount(std::size_t count);
    void AddModelMaskIndexCount(std::size_t count);
    void AddModelMaskTriangleCount(std::size_t count);
    void AddModelMainIndexCount(std::size_t count);
    void AddModelMainTriangleCount(std::size_t count);
    void AddModelUpdateTime(int modelIndex, double milliseconds);
    void AddModelDrawTime(int modelIndex, double milliseconds);
    void AddPresentTime(double milliseconds);
    void AddPresentCopyTime(double milliseconds);
    void AddPresentFlushTime(double milliseconds);

private:
    struct RunningStat
    {
        double Total;
        double Min;
        double Max;
        std::size_t Samples;

        RunningStat();

        void Add(double value);
        void Reset();
        double Average() const;
    };

    struct ModelFrameStat
    {
        double UpdateMs;
        double DrawMs;
    };

    struct ModelAggregateStat
    {
        RunningStat Update;
        RunningStat Draw;
    };

    struct MemorySnapshot
    {
        struct TextureDetail
        {
            std::string FileName;
            std::size_t Bytes;
            std::size_t RefCount;
            int Usage;
        };

        std::size_t ProcessResidentBytes;
        std::size_t ProcessPeakResidentBytes;
        std::size_t ProcessVirtualBytes;
        std::size_t HeapFree8BitBytes;
        std::size_t HeapMinimumFree8BitBytes;
        std::size_t HeapLargestFree8BitBytes;
        std::size_t HeapFreeInternalBytes;
        std::size_t HeapLargestFreeInternalBytes;
        std::size_t HeapFreeSpiramBytes;
        std::size_t TextureBytes;
        std::size_t TextureCount;
        std::size_t ModelTextureBytes;
        std::size_t ModelTextureCount;
        std::size_t UiTextureBytes;
        std::size_t UiTextureCount;
        std::size_t OtherTextureBytes;
        std::size_t OtherTextureCount;
        std::size_t RenderTargetBytes;
        std::size_t RenderTargetCount;
        std::size_t MainBackBufferBytes;
        std::size_t PresentBufferBytes;
        std::vector<TextureDetail> TextureDetails;
    };

    LAppPerformanceMonitor();

    void ResetCurrentFrame();
    void ResetInterval();
    void EnsureModelSlotCount(std::size_t count);
    void ReportIfNeeded(const TimePoint& now);
    void PrintReport(double intervalSeconds, const MemorySnapshot& snapshot) const;
    MemorySnapshot SampleMemorySnapshot() const;

    TimePoint _frameStartTime;
    TimePoint _intervalStartTime;
    int _frameWidth;
    int _frameHeight;

    double _clearMs;
    double _backgroundMs;
    double _uiMs;
    double _modelTotalMs;
    double _modelFrameBeginMs;
    double _modelFrameEndMs;
    double _modelUpdateMs;
    double _modelDrawMs;
    double _modelLoadParametersMs;
    double _modelIdleMotionStartMs;
    double _modelMotionEvaluateMs;
    double _modelSaveParametersMs;
    double _modelLateUpdateMs;
    double _modelCoreUpdateMs;
    double _modelPreDrawMs;
    double _modelMatrixMs;
    double _modelSubmitMs;
    double _modelPostDrawMs;
    double _modelMaskPassMs;
    double _modelMainDrawPassMs;
    double _modelSubmitRenderTargetMs;
    double _modelSubmitSortMs;
    double _modelSubmitRenderObjectMs;
    double _modelSubmitTailFlushMs;
    double _modelSubmitParentOffscreenMs;
    double _modelSubmitAddOffscreenMs;
    double _modelSubmitDrawOffscreenMs;
    double _modelSubmitMeshMs;
    double _modelSubmitMeshShaderMs;
    double _modelSubmitMeshDrawMs;
    double _modelSubmitCompositeMs;
    double _modelPipelineMs;
    double _modelVertexStageMs;
    double _modelPrimitiveLoopMs;
    double _modelTriangleFastMs;
    double _modelTriangleSampleMs;
    double _modelTriangleShadeMs;
    double _modelTriangleBlendMs;
    double _modelTriangleScalarMs;
    double _modelTriangleScalarTailMs;
    double _modelPipelineCallCount;
    double _modelFastTriangleCount;
    double _modelScalarTriangleCount;
    double _modelSampleCallCount;
    double _modelVisibleDrawableCount;
    double _modelMaskedDrawableCount;
    double _modelMaskDrawCallCount;
    double _modelMainDrawCallCount;
    double _modelIndexCount;
    double _modelTriangleCount;
    double _modelMaskIndexCount;
    double _modelMaskTriangleCount;
    double _modelMainIndexCount;
    double _modelMainTriangleCount;
    double _presentMs;
    double _presentCopyMs;
    double _presentFlushMs;

    std::size_t _intervalFrameCount;
    RunningStat _frameStat;
    RunningStat _clearStat;
    RunningStat _backgroundStat;
    RunningStat _uiStat;
    RunningStat _modelTotalStat;
    RunningStat _modelFrameBeginStat;
    RunningStat _modelFrameEndStat;
    RunningStat _modelUpdateStat;
    RunningStat _modelDrawStat;
    RunningStat _modelLoadParametersStat;
    RunningStat _modelIdleMotionStartStat;
    RunningStat _modelMotionEvaluateStat;
    RunningStat _modelSaveParametersStat;
    RunningStat _modelLateUpdateStat;
    RunningStat _modelCoreUpdateStat;
    RunningStat _modelPreDrawStat;
    RunningStat _modelMatrixStat;
    RunningStat _modelSubmitStat;
    RunningStat _modelPostDrawStat;
    RunningStat _modelMaskPassStat;
    RunningStat _modelMainDrawPassStat;
    RunningStat _modelSubmitRenderTargetStat;
    RunningStat _modelSubmitSortStat;
    RunningStat _modelSubmitRenderObjectStat;
    RunningStat _modelSubmitTailFlushStat;
    RunningStat _modelSubmitParentOffscreenStat;
    RunningStat _modelSubmitAddOffscreenStat;
    RunningStat _modelSubmitDrawOffscreenStat;
    RunningStat _modelSubmitMeshStat;
    RunningStat _modelSubmitMeshShaderStat;
    RunningStat _modelSubmitMeshDrawStat;
    RunningStat _modelSubmitCompositeStat;
    RunningStat _modelPipelineStat;
    RunningStat _modelVertexStageStat;
    RunningStat _modelPrimitiveLoopStat;
    RunningStat _modelTriangleFastStat;
    RunningStat _modelTriangleSampleStat;
    RunningStat _modelTriangleShadeStat;
    RunningStat _modelTriangleBlendStat;
    RunningStat _modelTriangleScalarStat;
    RunningStat _modelTriangleScalarTailStat;
    RunningStat _modelPipelineCallStat;
    RunningStat _modelFastTriangleCountStat;
    RunningStat _modelScalarTriangleCountStat;
    RunningStat _modelSampleCallCountStat;
    RunningStat _modelVisibleDrawableStat;
    RunningStat _modelMaskedDrawableStat;
    RunningStat _modelMaskDrawCallStat;
    RunningStat _modelMainDrawCallStat;
    RunningStat _modelIndexCountStat;
    RunningStat _modelTriangleCountStat;
    RunningStat _modelMaskIndexCountStat;
    RunningStat _modelMaskTriangleCountStat;
    RunningStat _modelMainIndexCountStat;
    RunningStat _modelMainTriangleCountStat;
    RunningStat _presentStat;
    RunningStat _presentCopyStat;
    RunningStat _presentFlushStat;

    std::vector<ModelFrameStat> _currentModelStats;
    std::vector<ModelAggregateStat> _intervalModelStats;
};
