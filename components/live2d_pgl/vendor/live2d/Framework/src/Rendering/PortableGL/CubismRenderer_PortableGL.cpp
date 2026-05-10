/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "CubismRenderer_PortableGL.hpp"

#include "CubismOffscreenManager_PortableGL.hpp"
#include "CubismShader_PortableGL.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Model/CubismModel.hpp"
#include "Type/csmVectorSort.hpp"

#include <chrono>

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

namespace {
    const csmUint16 ModelRenderTargetIndexArray[] = {
        0, 1, 2,
        2, 1, 3,
    };

    using RendererClock = std::chrono::steady_clock;

    csmFloat32 DurationMilliseconds(const RendererClock::time_point& startTime)
    {
        return static_cast<csmFloat32>(std::chrono::duration<double, std::milli>(RendererClock::now() - startTime).count());
    }

    class ScopedMilliseconds
    {
    public:
        explicit ScopedMilliseconds(csmFloat32& target)
            : _target(target)
            , _start(RendererClock::now())
        {
        }

        ~ScopedMilliseconds()
        {
            _target += DurationMilliseconds(_start);
        }

    private:
        csmFloat32& _target;
        RendererClock::time_point _start;
    };
}

void CubismClippingManager_PortableGL::SetupClippingContext(CubismModel& model, CubismRenderer_PortableGL* renderer, GLint lastFBO, GLint lastViewport[4], CubismRenderer::DrawableObjectType drawableObjectType)
{
    const RendererClock::time_point maskPassStart = RendererClock::now();
    csmInt32 usingClipCount = 0;
    for (csmUint32 clipIndex = 0; clipIndex < _clippingContextListForMask.GetSize(); ++clipIndex)
    {
        CubismClippingContext_PortableGL* cc = _clippingContextListForMask[clipIndex];
        CalcClippedTotalBounds(model, cc, drawableObjectType);
        if (cc->_isUsing)
        {
            ++usingClipCount;
        }
    }

    if (usingClipCount <= 0)
    {
        renderer->AddMaskPassMilliseconds(DurationMilliseconds(maskPassStart));
        return;
    }

    glViewport(0, 0, _clippingMaskBufferSize.X, _clippingMaskBufferSize.Y);

    switch (drawableObjectType)
    {
    case CubismRenderer::DrawableObjectType_Offscreen:
        _currentMaskBuffer = renderer->GetOffscreenMaskBuffer(0);
        break;
    case CubismRenderer::DrawableObjectType_Drawable:
    default:
        _currentMaskBuffer = renderer->GetDrawableMaskBuffer(0);
        break;
    }

    _currentMaskBuffer->BeginDraw(lastFBO);
    renderer->PreDraw();
    SetupLayoutBounds(usingClipCount);

    if (_clearedMaskBufferFlags.GetSize() != _renderTextureCount)
    {
        _clearedMaskBufferFlags.Clear();
        for (csmInt32 i = 0; i < _renderTextureCount; ++i)
        {
            _clearedMaskBufferFlags.PushBack(false);
        }
    }
    else
    {
        for (csmInt32 i = 0; i < _renderTextureCount; ++i)
        {
            _clearedMaskBufferFlags[i] = false;
        }
    }

    for (csmUint32 clipIndex = 0; clipIndex < _clippingContextListForMask.GetSize(); ++clipIndex)
    {
        CubismClippingContext_PortableGL* clipContext = _clippingContextListForMask[clipIndex];
        csmRectF* allClippedDrawRect = clipContext->_allClippedDrawRect;
        csmRectF* layoutBoundsOnTex01 = clipContext->_layoutBounds;
        const csmFloat32 margin = 0.05f;

        CubismRenderTarget_PortableGL* maskBuffer = NULL;
        switch (drawableObjectType)
        {
        case CubismRenderer::DrawableObjectType_Offscreen:
            maskBuffer = renderer->GetOffscreenMaskBuffer(clipContext->_bufferIndex);
            break;
        case CubismRenderer::DrawableObjectType_Drawable:
        default:
            maskBuffer = renderer->GetDrawableMaskBuffer(clipContext->_bufferIndex);
            break;
        }

        if (_currentMaskBuffer != maskBuffer)
        {
            _currentMaskBuffer->EndDraw();
            _currentMaskBuffer = maskBuffer;
            _currentMaskBuffer->BeginDraw(lastFBO);
            renderer->PreDraw();
        }

        _tmpBoundsOnModel.SetRect(allClippedDrawRect);
        _tmpBoundsOnModel.Expand(allClippedDrawRect->Width * margin, allClippedDrawRect->Height * margin);

        const csmFloat32 scaleX = layoutBoundsOnTex01->Width / _tmpBoundsOnModel.Width;
        const csmFloat32 scaleY = layoutBoundsOnTex01->Height / _tmpBoundsOnModel.Height;

        CreateMatrixForMask(false, layoutBoundsOnTex01, scaleX, scaleY);

        clipContext->_matrixForMask.SetMatrix(_tmpMatrixForMask.GetArray());
        clipContext->_matrixForDraw.SetMatrix(_tmpMatrixForDraw.GetArray());

        if (drawableObjectType == CubismRenderer::DrawableObjectType_Offscreen)
        {
            CubismMatrix44 invertMvp = renderer->GetMvpMatrix().GetInvert();
            clipContext->_matrixForDraw.MultiplyByMatrix(&invertMvp);
        }

        const csmInt32 clipDrawCount = clipContext->_clippingIdCount;
        for (csmInt32 i = 0; i < clipDrawCount; ++i)
        {
            const csmInt32 clipDrawIndex = clipContext->_clippingIdList[i];
            if (!model.GetDrawableDynamicFlagVertexPositionsDidChange(clipDrawIndex))
            {
                continue;
            }

            renderer->IsCulling(model.GetDrawableCulling(clipDrawIndex) != 0);

            if (!_clearedMaskBufferFlags[clipContext->_bufferIndex])
            {
                glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                _clearedMaskBufferFlags[clipContext->_bufferIndex] = true;
            }

            renderer->SetClippingContextBufferForMask(clipContext);
            renderer->DrawMeshPortableGL(model, clipDrawIndex);
        }
    }

    _currentMaskBuffer->EndDraw();
    renderer->SetClippingContextBufferForMask(NULL);
    glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
    renderer->AddMaskPassMilliseconds(DurationMilliseconds(maskPassStart));
}

CubismClippingContext_PortableGL::CubismClippingContext_PortableGL(CubismClippingManager<CubismClippingContext_PortableGL, CubismRenderTarget_PortableGL>* manager, CubismModel& model, const csmInt32* clippingDrawableIndices, csmInt32 clipCount)
    : CubismClippingContext(clippingDrawableIndices, clipCount)
{
    PGL_UNUSED(model);
    _owner = manager;
}

CubismClippingContext_PortableGL::~CubismClippingContext_PortableGL()
{
}

CubismClippingManager<CubismClippingContext_PortableGL, CubismRenderTarget_PortableGL>* CubismClippingContext_PortableGL::GetClippingManager()
{
    return _owner;
}

CubismRenderer* CubismRenderer::Create(csmUint32 width, csmUint32 height)
{
    return CSM_NEW CubismRenderer_PortableGL(width, height);
}

void CubismRenderer::StaticRelease()
{
    CubismRenderer_PortableGL::DoStaticRelease();
}

CubismRenderer_PortableGL::CubismRenderer_PortableGL(csmUint32 width, csmUint32 height)
    : CubismRenderer(width, height)
    , _drawableClippingManager(NULL)
    , _offscreenClippingManager(NULL)
    , _clippingContextBufferForMask(NULL)
    , _clippingContextBufferForDrawable(NULL)
    , _clippingContextBufferForOffscreen(NULL)
    , _currentFBO(0)
    , _currentOffscreen(NULL)
    , _modelRootFBO(0)
    , _frameStats()
{
    _textures.PrepareCapacity(32, true);
    ResetFrameStats();
}

CubismRenderer_PortableGL::~CubismRenderer_PortableGL()
{
    CSM_DELETE_SELF(CubismClippingManager_PortableGL, _drawableClippingManager);
    CSM_DELETE_SELF(CubismClippingManager_PortableGL, _offscreenClippingManager);

    for (csmInt32 i = 0; i < _modelRenderTargets.GetSize(); ++i)
    {
        if (_modelRenderTargets[i].IsValid())
        {
            _modelRenderTargets[i].DestroyRenderTarget();
        }
    }
    _modelRenderTargets.Clear();

    for (csmInt32 i = 0; i < _drawableMasks.GetSize(); ++i)
    {
        if (_drawableMasks[i].IsValid())
        {
            _drawableMasks[i].DestroyRenderTarget();
        }
    }
    _drawableMasks.Clear();

    for (csmInt32 i = 0; i < _offscreenMasks.GetSize(); ++i)
    {
        if (_offscreenMasks[i].IsValid())
        {
            _offscreenMasks[i].DestroyRenderTarget();
        }
    }
    _offscreenMasks.Clear();
}

void CubismRenderer_PortableGL::DoStaticRelease()
{
    CubismOffscreenManager_PortableGL::ReleaseInstance();
    CubismShader_PortableGL::DeleteInstance();
}

csmBool CubismRenderer_PortableGL::CanUseTextureBarrier()
{
    return false;
}

void CubismRenderer_PortableGL::Initialize(CubismModel* model)
{
    Initialize(model, 1);
}

void CubismRenderer_PortableGL::Initialize(CubismModel* model, csmInt32 maskBufferCount)
{
    if (maskBufferCount < 1)
    {
        maskBufferCount = 1;
        CubismLogWarning("The number of render textures must be an integer greater than or equal to 1. Set the number of render textures to 1.");
    }

    _modelRenderTargets.Clear();
    if (model->IsBlendModeEnabled())
    {
        for (csmInt32 i = 0; i < 2; ++i)
        {
            CubismRenderTarget_PortableGL renderTarget;
            renderTarget.CreateRenderTarget(_modelRenderTargetWidth, _modelRenderTargetHeight, 0);
            _modelRenderTargets.PushBack(renderTarget);
        }
    }

    if (model->IsUsingMasking())
    {
        _drawableClippingManager = CSM_NEW CubismClippingManager_PortableGL();
        _drawableClippingManager->Initialize(*model, maskBufferCount, CubismRenderer::DrawableObjectType_Drawable);

        _drawableMasks.Clear();
        for (csmInt32 i = 0; i < maskBufferCount; ++i)
        {
            CubismRenderTarget_PortableGL masks;
            masks.CreateRenderTarget(_drawableClippingManager->GetClippingMaskBufferSize().X, _drawableClippingManager->GetClippingMaskBufferSize().Y);
            _drawableMasks.PushBack(masks);
        }
    }

    if (model->IsUsingMaskingForOffscreen())
    {
        _offscreenClippingManager = CSM_NEW CubismClippingManager_PortableGL();
        _offscreenClippingManager->Initialize(*model, maskBufferCount, CubismRenderer::DrawableObjectType_Offscreen);

        _offscreenMasks.Clear();
        for (csmInt32 i = 0; i < maskBufferCount; ++i)
        {
            CubismRenderTarget_PortableGL masks;
            masks.CreateRenderTarget(_offscreenClippingManager->GetClippingMaskBufferSize().X, _offscreenClippingManager->GetClippingMaskBufferSize().Y);
            _offscreenMasks.PushBack(masks);
        }
    }

    _sortedObjectsIndexList.Resize(model->GetDrawableCount() + model->GetOffscreenCount(), 0);
    _sortedObjectsTypeList.Resize(model->GetDrawableCount() + model->GetOffscreenCount(), DrawableObjectType_Drawable);

    const csmInt32 offscreenCount = model->GetOffscreenCount();
    if (offscreenCount > 0)
    {
        _offscreenList.Clear();
        _offscreenList.PrepareCapacity(offscreenCount);
        for (csmInt32 offscreenIndex = 0; offscreenIndex < offscreenCount; ++offscreenIndex)
        {
            CubismOffscreenRenderTarget_PortableGL renderTarget;
            renderTarget.SetOffscreenIndex(offscreenIndex);
            _offscreenList.PushBack(renderTarget);
        }

        SetupParentOffscreens(model, offscreenCount);
    }

    CubismRenderer::Initialize(model, maskBufferCount);
    CubismShader_PortableGL::GetInstance();
}

void CubismRenderer_PortableGL::SetupParentOffscreens(const CubismModel* model, csmInt32 offscreenCount)
{
    CubismOffscreenRenderTarget_PortableGL* parentOffscreen;
    for (csmInt32 offscreenIndex = 0; offscreenIndex < offscreenCount; ++offscreenIndex)
    {
        parentOffscreen = NULL;
        const csmInt32 ownerIndex = model->GetOffscreenOwnerIndices()[offscreenIndex];
        csmInt32 parentIndex = model->GetPartParentPartIndex(ownerIndex);

        while (parentIndex != CubismModel::CubismNoIndex_Parent)
        {
            for (csmInt32 i = 0; i < offscreenCount; ++i)
            {
                if (model->GetOffscreenOwnerIndices()[_offscreenList.At(i).GetOffscreenIndex()] != parentIndex)
                {
                    continue;
                }

                parentOffscreen = &_offscreenList.At(i);
                break;
            }

            if (parentOffscreen != NULL)
            {
                break;
            }

            parentIndex = model->GetPartParentPartIndex(parentIndex);
        }

        _offscreenList.At(offscreenIndex).SetParentPartOffscreen(parentOffscreen);
    }
}

void CubismRenderer_PortableGL::PreDraw()
{
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glColorMask(1, 1, 1, 1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    CubismPortableGLRefreshFramebufferState();
}

void CubismRenderer_PortableGL::DoDrawModel()
{
    ResetFrameStats();
    CubismPortableGLResetPerfStats();
    const GLint lastFBO = static_cast<GLint>(CubismPortableGLGetCurrentRenderTarget());
    GLint lastViewport[4];
    glGetIntegerv(GL_VIEWPORT, lastViewport);

    {
        ScopedMilliseconds renderTargetTimer(_frameStats.SubmitRenderTargetMilliseconds);
        BeforeDrawModelRenderTarget();
    }

    if (_drawableClippingManager != NULL)
    {
        PreDraw();

        for (csmInt32 i = 0; i < _drawableClippingManager->GetRenderTextureCount(); ++i)
        {
            if (_drawableMasks[i].GetBufferWidth() != static_cast<csmUint32>(_drawableClippingManager->GetClippingMaskBufferSize().X)
                || _drawableMasks[i].GetBufferHeight() != static_cast<csmUint32>(_drawableClippingManager->GetClippingMaskBufferSize().Y))
            {
                _drawableMasks[i].CreateRenderTarget(
                    static_cast<csmUint32>(_drawableClippingManager->GetClippingMaskBufferSize().X),
                    static_cast<csmUint32>(_drawableClippingManager->GetClippingMaskBufferSize().Y)
                );
            }
        }

        if (IsUsingHighPrecisionMask())
        {
            _drawableClippingManager->SetupMatrixForHighPrecision(*GetModel(), false, DrawableObjectType_Drawable);
        }
        else
        {
            _drawableClippingManager->SetupClippingContext(*GetModel(), this, lastFBO, lastViewport, DrawableObjectType_Drawable);
        }
    }

    if (_offscreenClippingManager != NULL)
    {
        PreDraw();

        for (csmInt32 i = 0; i < _offscreenClippingManager->GetRenderTextureCount(); ++i)
        {
            if (_offscreenMasks[i].GetBufferWidth() != static_cast<csmUint32>(_offscreenClippingManager->GetClippingMaskBufferSize().X)
                || _offscreenMasks[i].GetBufferHeight() != static_cast<csmUint32>(_offscreenClippingManager->GetClippingMaskBufferSize().Y))
            {
                _offscreenMasks[i].CreateRenderTarget(
                    static_cast<csmUint32>(_offscreenClippingManager->GetClippingMaskBufferSize().X),
                    static_cast<csmUint32>(_offscreenClippingManager->GetClippingMaskBufferSize().Y)
                );
            }
        }

        if (IsUsingHighPrecisionMask())
        {
            _offscreenClippingManager->SetupMatrixForHighPrecision(*GetModel(), false, DrawableObjectType_Offscreen, GetMvpMatrix());
        }
        else
        {
            _offscreenClippingManager->SetupClippingContext(*GetModel(), this, lastFBO, lastViewport, DrawableObjectType_Offscreen);
        }
    }

    PreDraw();
    const RendererClock::time_point mainDrawStart = RendererClock::now();
    DrawObjectLoop(lastFBO, lastViewport);
    _frameStats.MainDrawMilliseconds += DurationMilliseconds(mainDrawStart);
    PostDraw();
    {
        ScopedMilliseconds renderTargetTimer(_frameStats.SubmitRenderTargetMilliseconds);
        AfterDrawModelRenderTarget();
    }

    const CubismPortableGLPerfStats& perfStats = CubismPortableGLGetPerfStats();
    _frameStats.PipelineMilliseconds = perfStats.PipelineMilliseconds;
    _frameStats.VertexStageMilliseconds = perfStats.VertexStageMilliseconds;
    _frameStats.PrimitiveLoopMilliseconds = perfStats.PrimitiveLoopMilliseconds;
    _frameStats.TriangleFastMilliseconds = perfStats.TriangleFastMilliseconds;
    _frameStats.TriangleSampleMilliseconds = perfStats.TriangleSampleMilliseconds;
    _frameStats.TriangleShadeMilliseconds = perfStats.TriangleShadeMilliseconds;
    _frameStats.TriangleBlendMilliseconds = perfStats.TriangleBlendMilliseconds;
    _frameStats.TriangleScalarMilliseconds = perfStats.TriangleScalarMilliseconds;
    _frameStats.TriangleScalarTailMilliseconds = perfStats.TriangleScalarTailMilliseconds;
    _frameStats.PipelineCallCount = perfStats.PipelineCallCount;
    _frameStats.FastTriangleCount = perfStats.FastTriangleCount;
    _frameStats.ScalarTriangleCount = perfStats.ScalarTriangleCount;
    _frameStats.SampleCallCount = perfStats.SampleCallCount;
}

void CubismRenderer_PortableGL::DrawObjectLoop(GLint lastFBO, GLint lastViewport[4])
{
    PGL_UNUSED(lastViewport);

    const csmInt32 drawableCount = GetModel()->GetDrawableCount();
    const csmInt32 offscreenCount = GetModel()->GetOffscreenCount();
    const csmInt32 totalCount = drawableCount + offscreenCount;
    const csmInt32* renderOrder = GetModel()->GetRenderOrders();

    _currentOffscreen = NULL;
    _currentFBO = lastFBO;
    _modelRootFBO = lastFBO;

    {
        ScopedMilliseconds sortTimer(_frameStats.SubmitSortMilliseconds);
        for (csmInt32 i = 0; i < totalCount; ++i)
        {
            const csmInt32 order = renderOrder[i];
            if (i < drawableCount)
            {
                _sortedObjectsIndexList[order] = i;
                _sortedObjectsTypeList[order] = DrawableObjectType_Drawable;
            }
            else
            {
                _sortedObjectsIndexList[order] = i - drawableCount;
                _sortedObjectsTypeList[order] = DrawableObjectType_Offscreen;
            }
        }
    }

    {
        ScopedMilliseconds renderObjectTimer(_frameStats.SubmitRenderObjectMilliseconds);
        for (csmInt32 i = 0; i < totalCount; ++i)
        {
            RenderObject(_sortedObjectsIndexList[i], _sortedObjectsTypeList[i]);
        }
    }

    {
        ScopedMilliseconds tailFlushTimer(_frameStats.SubmitTailFlushMilliseconds);
        while (_currentOffscreen != NULL)
        {
            SubmitDrawToParentOffscreen(_currentOffscreen->GetOffscreenIndex(), DrawableObjectType_Offscreen);
        }
    }
}

void CubismRenderer_PortableGL::RenderObject(csmInt32 objectIndex, csmInt32 objectType)
{
    switch (objectType)
    {
    case DrawableObjectType_Drawable:
        DrawDrawable(objectIndex);
        break;
    case DrawableObjectType_Offscreen:
        AddOffscreen(objectIndex);
        break;
    default:
        CubismLogError("Unknown drawable type: %d", objectType);
        break;
    }
}

void CubismRenderer_PortableGL::DrawDrawable(csmInt32 drawableIndex)
{
    if (!GetModel()->GetDrawableDynamicFlagIsVisible(drawableIndex))
    {
        return;
    }

    ++_frameStats.VisibleDrawableCount;
    SubmitDrawToParentOffscreen(drawableIndex, DrawableObjectType_Drawable);

    CubismClippingContext_PortableGL* clipContext = (_drawableClippingManager != NULL)
        ? (*_drawableClippingManager->GetClippingContextListForDraw())[drawableIndex]
        : NULL;

    if (clipContext != NULL && clipContext->_isUsing)
    {
        ++_frameStats.MaskedDrawableCount;
    }

    if (clipContext != NULL && IsUsingHighPrecisionMask())
    {
        const RendererClock::time_point highPrecisionMaskStart = RendererClock::now();
        GLint preHighPrecisionMaskViewport[4];
        glGetIntegerv(GL_VIEWPORT, preHighPrecisionMaskViewport);

        if (clipContext->_isUsing)
        {
            glViewport(0, 0, _drawableClippingManager->GetClippingMaskBufferSize().X, _drawableClippingManager->GetClippingMaskBufferSize().Y);
            PreDraw();
            GetDrawableMaskBuffer(clipContext->_bufferIndex)->BeginDraw(_currentFBO);
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        const csmInt32 clipDrawCount = clipContext->_clippingIdCount;
        for (csmInt32 index = 0; index < clipDrawCount; ++index)
        {
            const csmInt32 clipDrawIndex = clipContext->_clippingIdList[index];
            if (!GetModel()->GetDrawableDynamicFlagVertexPositionsDidChange(clipDrawIndex))
            {
                continue;
            }

            IsCulling(GetModel()->GetDrawableCulling(clipDrawIndex) != 0);
            SetClippingContextBufferForMask(clipContext);
            DrawMeshPortableGL(*GetModel(), clipDrawIndex);
        }

        GetDrawableMaskBuffer(clipContext->_bufferIndex)->EndDraw();
        SetClippingContextBufferForMask(NULL);
        glViewport(preHighPrecisionMaskViewport[0], preHighPrecisionMaskViewport[1], preHighPrecisionMaskViewport[2], preHighPrecisionMaskViewport[3]);
        PreDraw();
        AddMaskPassMilliseconds(DurationMilliseconds(highPrecisionMaskStart));
    }

    SetClippingContextBufferForDrawable(clipContext);
    IsCulling(GetModel()->GetDrawableCulling(drawableIndex) != 0);
    DrawMeshPortableGL(*GetModel(), drawableIndex);
}

void CubismRenderer_PortableGL::SubmitDrawToParentOffscreen(csmInt32 objectIndex, const DrawableObjectType objectType)
{
    ScopedMilliseconds parentSubmitTimer(_frameStats.SubmitParentSubmitMilliseconds);

    if (_currentOffscreen == NULL || objectIndex == CubismModel::CubismNoIndex_Offscreen)
    {
        return;
    }

    csmInt32 currentOwnerIndex = GetModel()->GetOffscreenOwnerIndices()[_currentOffscreen->GetOffscreenIndex()];
    if (currentOwnerIndex == CubismModel::CubismNoIndex_Offscreen)
    {
        return;
    }

    csmInt32 targetParentIndex = CubismModel::CubismNoIndex_Parent;
    switch (objectType)
    {
    case DrawableObjectType_Drawable:
        targetParentIndex = GetModel()->GetDrawableParentPartIndex(objectIndex);
        break;
    case DrawableObjectType_Offscreen:
        targetParentIndex = GetModel()->GetPartParentPartIndex(GetModel()->GetOffscreenOwnerIndices()[objectIndex]);
        break;
    default:
        return;
    }

    while (targetParentIndex != CubismModel::CubismNoIndex_Parent)
    {
        if (targetParentIndex == currentOwnerIndex)
        {
            return;
        }
        targetParentIndex = GetModel()->GetPartParentPartIndex(targetParentIndex);
    }

    DrawOffscreen(_currentOffscreen);
    SubmitDrawToParentOffscreen(objectIndex, objectType);
}

void CubismRenderer_PortableGL::AddOffscreen(csmInt32 offscreenIndex)
{
    ScopedMilliseconds addOffscreenTimer(_frameStats.SubmitAddOffscreenMilliseconds);

    if (_currentOffscreen != NULL && _currentOffscreen->GetOffscreenIndex() != offscreenIndex)
    {
        csmBool isParent = false;
        csmInt32 ownerIndex = GetModel()->GetOffscreenOwnerIndices()[offscreenIndex];
        csmInt32 parentIndex = GetModel()->GetPartParentPartIndex(ownerIndex);

        const csmInt32 currentOffscreenIndex = _currentOffscreen->GetOffscreenIndex();
        const csmInt32 currentOwnerIndex = GetModel()->GetOffscreenOwnerIndices()[currentOffscreenIndex];
        while (parentIndex != CubismModel::CubismNoIndex_Parent)
        {
            if (parentIndex == currentOwnerIndex)
            {
                isParent = true;
                break;
            }
            parentIndex = GetModel()->GetPartParentPartIndex(parentIndex);
        }

        if (!isParent)
        {
            SubmitDrawToParentOffscreen(offscreenIndex, DrawableObjectType_Offscreen);
        }
    }

    CubismOffscreenRenderTarget_PortableGL* offscreen = &_offscreenList.At(offscreenIndex);
    offscreen->SetOffscreenRenderTarget(_modelRenderTargetWidth, _modelRenderTargetHeight);

    CubismOffscreenRenderTarget_PortableGL* oldOffscreen = offscreen->GetParentPartOffscreen();
    offscreen->SetOldOffscreen(oldOffscreen);

    GLint oldFBO = 0;
    if (oldOffscreen != NULL)
    {
        oldFBO = oldOffscreen->GetRenderTarget()->GetRenderTexture();
    }
    if (oldFBO == 0)
    {
        oldFBO = _modelRootFBO;
    }

    offscreen->GetRenderTarget()->BeginDraw(oldFBO);
    glViewport(0, 0, _modelRenderTargetWidth, _modelRenderTargetHeight);
    offscreen->GetRenderTarget()->Clear(0.0f, 0.0f, 0.0f, 0.0f);

    _currentOffscreen = offscreen;
    _currentFBO = offscreen->GetRenderTarget()->GetRenderTexture();
}

void CubismRenderer_PortableGL::DrawOffscreen(CubismOffscreenRenderTarget_PortableGL* currentOffscreen)
{
    ScopedMilliseconds drawOffscreenTimer(_frameStats.SubmitDrawOffscreenMilliseconds);

    const csmInt32 offscreenIndex = currentOffscreen->GetOffscreenIndex();
    CubismClippingContext_PortableGL* clipContext = (_offscreenClippingManager != NULL)
        ? (*_offscreenClippingManager->GetClippingContextListForOffscreen())[offscreenIndex]
        : NULL;

    if (clipContext != NULL && IsUsingHighPrecisionMask())
    {
        const RendererClock::time_point highPrecisionMaskStart = RendererClock::now();
        GLint preHighPrecisionMaskViewport[4];
        glGetIntegerv(GL_VIEWPORT, preHighPrecisionMaskViewport);

        if (clipContext->_isUsing)
        {
            glViewport(0, 0, _offscreenClippingManager->GetClippingMaskBufferSize().X, _offscreenClippingManager->GetClippingMaskBufferSize().Y);
            PreDraw();
            GetOffscreenMaskBuffer(clipContext->_bufferIndex)->BeginDraw(_currentFBO);
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        const csmInt32 clipDrawCount = clipContext->_clippingIdCount;
        for (csmInt32 index = 0; index < clipDrawCount; ++index)
        {
            const csmInt32 clipDrawIndex = clipContext->_clippingIdList[index];
            if (!GetModel()->GetDrawableDynamicFlagVertexPositionsDidChange(clipDrawIndex))
            {
                continue;
            }

            IsCulling(GetModel()->GetDrawableCulling(clipDrawIndex) != 0);
            SetClippingContextBufferForMask(clipContext);
            DrawMeshPortableGL(*GetModel(), clipDrawIndex);
        }

        GetOffscreenMaskBuffer(clipContext->_bufferIndex)->EndDraw();
        SetClippingContextBufferForMask(NULL);
        glViewport(preHighPrecisionMaskViewport[0], preHighPrecisionMaskViewport[1], preHighPrecisionMaskViewport[2], preHighPrecisionMaskViewport[3]);
        PreDraw();
        AddMaskPassMilliseconds(DurationMilliseconds(highPrecisionMaskStart));
    }

    SetClippingContextBufferForOffscreen(clipContext);
    IsCulling(GetModel()->GetOffscreenCulling(offscreenIndex) != 0);
    DrawOffscreenPortableGL(*GetModel(), currentOffscreen);
}

void CubismRenderer_PortableGL::DrawMeshPortableGL(const CubismModel& model, csmInt32 index)
{
    ScopedMilliseconds meshTotalTimer(_frameStats.SubmitMeshTotalMilliseconds);

#ifndef CSM_DEBUG
    if (GetBindedTextureId(model.GetDrawableTextureIndex(index)) == 0)
    {
        return;
    }
#endif

    if (IsCulling())
    {
        glEnable(GL_CULL_FACE);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }

    glFrontFace(GL_CCW);

    {
        ScopedMilliseconds meshShaderTimer(_frameStats.SubmitMeshShaderMilliseconds);
        if (IsGeneratingMask())
        {
            CubismShader_PortableGL::GetInstance()->SetupShaderProgramForMask(this, model, index);
        }
        else
        {
            CubismShader_PortableGL::GetInstance()->SetupShaderProgramForDrawable(this, model, index);
        }
    }

    const csmInt32 indexCount = model.GetDrawableVertexIndexCount(index);
    const csmInt32 vertexCount = model.GetDrawableVertexCount(index);
    const csmFloat32* const vertexArray = model.GetDrawableVertices(index);
    const Core::csmVector2* const uvArray = model.GetDrawableVertexUvs(index);
    csmUint16* indexArray = const_cast<csmUint16*>(model.GetDrawableVertexIndices(index));
    if (indexCount <= 0 || vertexCount <= 0 || indexArray == NULL || vertexArray == NULL || uvArray == NULL)
    {
        CubismLogWarning("[PGL] skip drawable %d due to missing geometry: indexCount=%d vertexCount=%d indices=%p vertices=%p uvs=%p",
            index,
            indexCount,
            vertexCount,
            indexArray,
            vertexArray,
            uvArray);
        glUseProgram(0);
        SetClippingContextBufferForDrawable(NULL);
        SetClippingContextBufferForMask(NULL);
        return;
    }
    AddDrawCallGeometry(indexCount, IsGeneratingMask());
    {
        ScopedMilliseconds meshDrawTimer(_frameStats.SubmitMeshDrawMilliseconds);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, indexArray);
    }

    glUseProgram(0);
    SetClippingContextBufferForDrawable(NULL);
    SetClippingContextBufferForMask(NULL);
}

void CubismRenderer_PortableGL::DrawOffscreenPortableGL(const CubismModel& model, CubismOffscreenRenderTarget_PortableGL* offscreen)
{
    ScopedMilliseconds compositeTimer(_frameStats.SubmitCompositeMilliseconds);

    if (IsCulling())
    {
        glEnable(GL_CULL_FACE);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }

    glFrontFace(GL_CCW);

    offscreen->GetRenderTarget()->EndDraw();
    _currentOffscreen = _currentOffscreen->GetOldOffscreen();
    _currentFBO = offscreen->GetRenderTarget()->GetOldFBO();

    CubismShader_PortableGL::GetInstance()->SetupShaderProgramForOffscreen(this, model, offscreen);
    AddOffscreenGeometry(static_cast<csmInt32>(sizeof(ModelRenderTargetIndexArray) / sizeof(csmUint16)));
    glDrawElements(GL_TRIANGLES, sizeof(ModelRenderTargetIndexArray) / sizeof(csmUint16), GL_UNSIGNED_SHORT, ModelRenderTargetIndexArray);

    offscreen->StopUsingRenderTexture();
    glUseProgram(0);
    SetClippingContextBufferForOffscreen(NULL);
    SetClippingContextBufferForMask(NULL);
}

void CubismRenderer_PortableGL::SaveProfile()
{
    _rendererProfile.Save();
}

void CubismRenderer_PortableGL::RestoreProfile()
{
    _rendererProfile.Restore();
}

void CubismRenderer_PortableGL::BeforeDrawModelRenderTarget()
{
    if (_modelRenderTargets.GetSize() == 0)
    {
        return;
    }

    for (csmInt32 i = 0; i < _modelRenderTargets.GetSize(); ++i)
    {
        if (_modelRenderTargets[i].GetBufferWidth() != _modelRenderTargetWidth
            || _modelRenderTargets[i].GetBufferHeight() != _modelRenderTargetHeight)
        {
            _modelRenderTargets[i].CreateRenderTarget(_modelRenderTargetWidth, _modelRenderTargetHeight, 0);
        }
    }

    _modelRenderTargets[0].BeginDraw();
    glViewport(0, 0, _modelRenderTargetWidth, _modelRenderTargetHeight);
    _modelRenderTargets[0].Clear(0.0f, 0.0f, 0.0f, 0.0f);
}

void CubismRenderer_PortableGL::AfterDrawModelRenderTarget()
{
    if (_modelRenderTargets.GetSize() == 0)
    {
        return;
    }

    _modelRenderTargets[0].EndDraw();
    {
        ScopedMilliseconds compositeTimer(_frameStats.SubmitCompositeMilliseconds);
        CubismShader_PortableGL::GetInstance()->SetupShaderProgramForOffscreenRenderTarget(this);
        glDrawElements(GL_TRIANGLES, sizeof(ModelRenderTargetIndexArray) / sizeof(csmUint16), GL_UNSIGNED_SHORT, ModelRenderTargetIndexArray);
    }
    glUseProgram(0);
}

void CubismRenderer_PortableGL::BindTexture(csmUint32 modelTextureIndex, GLuint glTextureIndex)
{
    _textures[modelTextureIndex] = glTextureIndex;
}

const csmMap<csmInt32, GLuint>& CubismRenderer_PortableGL::GetBindedTextures() const
{
    return _textures;
}

void CubismRenderer_PortableGL::SetDrawableClippingMaskBufferSize(csmFloat32 width, csmFloat32 height)
{
    if (_drawableClippingManager == NULL)
    {
        return;
    }

    const csmInt32 renderTextureCount = _drawableClippingManager->GetRenderTextureCount();
    CSM_DELETE_SELF(CubismClippingManager_PortableGL, _drawableClippingManager);
    _drawableClippingManager = CSM_NEW CubismClippingManager_PortableGL();
    _drawableClippingManager->SetClippingMaskBufferSize(width, height);
    _drawableClippingManager->Initialize(*GetModel(), renderTextureCount, CubismRenderer::DrawableObjectType_Drawable);
}

void CubismRenderer_PortableGL::SetOffscreenClippingMaskBufferSize(csmFloat32 width, csmFloat32 height)
{
    if (_offscreenClippingManager == NULL)
    {
        return;
    }

    const csmInt32 renderTextureCount = _offscreenClippingManager->GetRenderTextureCount();
    CSM_DELETE_SELF(CubismClippingManager_PortableGL, _offscreenClippingManager);
    _offscreenClippingManager = CSM_NEW CubismClippingManager_PortableGL();
    _offscreenClippingManager->SetClippingMaskBufferSize(width, height);
    _offscreenClippingManager->Initialize(*GetModel(), renderTextureCount, CubismRenderer::DrawableObjectType_Offscreen);
}

csmInt32 CubismRenderer_PortableGL::GetDrawableRenderTextureCount() const
{
    return _drawableClippingManager ? _drawableClippingManager->GetRenderTextureCount() : 0;
}

csmInt32 CubismRenderer_PortableGL::GetOffscreenRenderTextureCount() const
{
    return _offscreenClippingManager ? _offscreenClippingManager->GetRenderTextureCount() : 0;
}

CubismVector2 CubismRenderer_PortableGL::GetDrawableClippingMaskBufferSize() const
{
    return _drawableClippingManager ? _drawableClippingManager->GetClippingMaskBufferSize() : CubismVector2(0.0f, 0.0f);
}

CubismVector2 CubismRenderer_PortableGL::GetOffscreenClippingMaskBufferSize() const
{
    return _offscreenClippingManager ? _offscreenClippingManager->GetClippingMaskBufferSize() : CubismVector2(0.0f, 0.0f);
}

const CubismRenderTarget_PortableGL* CubismRenderer_PortableGL::CopyOffscreenRenderTarget()
{
    if (_modelRenderTargets.GetSize() == 0)
    {
        return NULL;
    }

    if (_modelRenderTargets.GetSize() < 2)
    {
        return &_modelRenderTargets[0];
    }

    return CopyRenderTarget(_modelRenderTargets[0]);
}

const CubismRenderTarget_PortableGL* CubismRenderer_PortableGL::CopyRenderTarget(const CubismRenderTarget_PortableGL& srcBuffer)
{
    if (_modelRenderTargets.GetSize() < 2)
    {
        return &srcBuffer;
    }

    if (_modelRenderTargets[1].GetBufferWidth() != srcBuffer.GetBufferWidth()
        || _modelRenderTargets[1].GetBufferHeight() != srcBuffer.GetBufferHeight())
    {
        _modelRenderTargets[1].CreateRenderTarget(srcBuffer.GetBufferWidth(), srcBuffer.GetBufferHeight(), 0);
    }

    CubismRenderTarget_PortableGL::CopyBuffer(srcBuffer, _modelRenderTargets[1]);
    return &_modelRenderTargets[1];
}

CubismRenderTarget_PortableGL* CubismRenderer_PortableGL::GetDrawableMaskBuffer(csmInt32 index)
{
    return &_drawableMasks[index];
}

CubismRenderTarget_PortableGL* CubismRenderer_PortableGL::GetOffscreenMaskBuffer(csmInt32 index)
{
    return &_offscreenMasks[index];
}

CubismOffscreenRenderTarget_PortableGL* CubismRenderer_PortableGL::GetCurrentOffscreen() const
{
    return _currentOffscreen;
}

const CubismRenderer_PortableGL::FrameStats& CubismRenderer_PortableGL::GetLastFrameStats() const
{
    return _frameStats;
}

void CubismRenderer_PortableGL::SetClippingContextBufferForMask(CubismClippingContext_PortableGL* clip)
{
    _clippingContextBufferForMask = clip;
}

CubismClippingContext_PortableGL* CubismRenderer_PortableGL::GetClippingContextBufferForMask() const
{
    return _clippingContextBufferForMask;
}

void CubismRenderer_PortableGL::SetClippingContextBufferForDrawable(CubismClippingContext_PortableGL* clip)
{
    _clippingContextBufferForDrawable = clip;
}

CubismClippingContext_PortableGL* CubismRenderer_PortableGL::GetClippingContextBufferForDrawable() const
{
    return _clippingContextBufferForDrawable;
}

void CubismRenderer_PortableGL::SetClippingContextBufferForOffscreen(CubismClippingContext_PortableGL* clip)
{
    _clippingContextBufferForOffscreen = clip;
}

CubismClippingContext_PortableGL* CubismRenderer_PortableGL::GetClippingContextBufferForOffscreen() const
{
    return _clippingContextBufferForOffscreen;
}

csmBool inline CubismRenderer_PortableGL::IsGeneratingMask() const
{
    return GetClippingContextBufferForMask() != NULL;
}

GLuint CubismRenderer_PortableGL::GetBindedTextureId(csmInt32 textureId)
{
    return (_textures[textureId] != 0) ? _textures[textureId] : 0;
}

void CubismRenderer_PortableGL::ResetFrameStats()
{
    _frameStats.MaskPassMilliseconds = 0.0f;
    _frameStats.MainDrawMilliseconds = 0.0f;
    _frameStats.SubmitRenderTargetMilliseconds = 0.0f;
    _frameStats.SubmitSortMilliseconds = 0.0f;
    _frameStats.SubmitRenderObjectMilliseconds = 0.0f;
    _frameStats.SubmitTailFlushMilliseconds = 0.0f;
    _frameStats.SubmitParentSubmitMilliseconds = 0.0f;
    _frameStats.SubmitAddOffscreenMilliseconds = 0.0f;
    _frameStats.SubmitDrawOffscreenMilliseconds = 0.0f;
    _frameStats.SubmitMeshTotalMilliseconds = 0.0f;
    _frameStats.SubmitMeshShaderMilliseconds = 0.0f;
    _frameStats.SubmitMeshDrawMilliseconds = 0.0f;
    _frameStats.SubmitCompositeMilliseconds = 0.0f;
    _frameStats.PipelineMilliseconds = 0.0f;
    _frameStats.VertexStageMilliseconds = 0.0f;
    _frameStats.PrimitiveLoopMilliseconds = 0.0f;
    _frameStats.TriangleFastMilliseconds = 0.0f;
    _frameStats.TriangleSampleMilliseconds = 0.0f;
    _frameStats.TriangleShadeMilliseconds = 0.0f;
    _frameStats.TriangleBlendMilliseconds = 0.0f;
    _frameStats.TriangleScalarMilliseconds = 0.0f;
    _frameStats.TriangleScalarTailMilliseconds = 0.0f;
    _frameStats.PipelineCallCount = 0;
    _frameStats.FastTriangleCount = 0;
    _frameStats.ScalarTriangleCount = 0;
    _frameStats.SampleCallCount = 0;
    _frameStats.VisibleDrawableCount = 0;
    _frameStats.MaskedDrawableCount = 0;
    _frameStats.MaskDrawCallCount = 0;
    _frameStats.MainDrawCallCount = 0;
    _frameStats.TotalIndexCount = 0;
    _frameStats.TotalTriangleCount = 0;
    _frameStats.MaskIndexCount = 0;
    _frameStats.MaskTriangleCount = 0;
    _frameStats.MainIndexCount = 0;
    _frameStats.MainTriangleCount = 0;
}

void CubismRenderer_PortableGL::AddMaskPassMilliseconds(csmFloat32 milliseconds)
{
    _frameStats.MaskPassMilliseconds += milliseconds;
}

void CubismRenderer_PortableGL::AddDrawCallGeometry(csmInt32 indexCount, csmBool isMaskPass)
{
    if (indexCount <= 0)
    {
        return;
    }

    const csmUint32 unsignedIndexCount = static_cast<csmUint32>(indexCount);
    const csmUint32 triangleCount = unsignedIndexCount / 3u;

    _frameStats.TotalIndexCount += unsignedIndexCount;
    _frameStats.TotalTriangleCount += triangleCount;

    if (isMaskPass)
    {
        ++_frameStats.MaskDrawCallCount;
        _frameStats.MaskIndexCount += unsignedIndexCount;
        _frameStats.MaskTriangleCount += triangleCount;
    }
    else
    {
        ++_frameStats.MainDrawCallCount;
        _frameStats.MainIndexCount += unsignedIndexCount;
        _frameStats.MainTriangleCount += triangleCount;
    }
}

void CubismRenderer_PortableGL::AddOffscreenGeometry(csmInt32 indexCount)
{
    AddDrawCallGeometry(indexCount, false);
}

}}}}
