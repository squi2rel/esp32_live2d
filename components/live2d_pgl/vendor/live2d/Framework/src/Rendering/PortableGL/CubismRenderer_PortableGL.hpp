/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include "../CubismClippingManager.hpp"
#include "../CubismRenderer.hpp"
#include "CubismFramework.hpp"
#include "CubismOffscreenRenderTarget_PortableGL.hpp"
#include "CubismPortableGL.hpp"
#include "CubismRenderTarget_PortableGL.hpp"
#include "Math/CubismVector2.hpp"
#include "Type/csmMap.hpp"
#include "Type/csmRectF.hpp"
#include "Type/csmVector.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

class CubismRenderer_PortableGL;
class CubismClippingContext_PortableGL;
class CubismShader_PortableGL;

class CubismClippingManager_PortableGL : public CubismClippingManager<CubismClippingContext_PortableGL, CubismRenderTarget_PortableGL>
{
public:
    void SetupClippingContext(CubismModel& model, CubismRenderer_PortableGL* renderer, GLint lastFBO, GLint lastViewport[4], CubismRenderer::DrawableObjectType drawableObjectType);
};

class CubismClippingContext_PortableGL : public CubismClippingContext
{
    friend class CubismClippingManager_PortableGL;
    friend class CubismRenderer_PortableGL;

public:
    CubismClippingContext_PortableGL(CubismClippingManager<CubismClippingContext_PortableGL, CubismRenderTarget_PortableGL>* manager, CubismModel& model, const csmInt32* clippingDrawableIndices, csmInt32 clipCount);
    virtual ~CubismClippingContext_PortableGL();

    CubismClippingManager<CubismClippingContext_PortableGL, CubismRenderTarget_PortableGL>* GetClippingManager();

    CubismClippingManager<CubismClippingContext_PortableGL, CubismRenderTarget_PortableGL>* _owner;
};

class CubismRendererProfile_PortableGL
{
    friend class CubismRenderer_PortableGL;

private:
    CubismRendererProfile_PortableGL() {}
    ~CubismRendererProfile_PortableGL() {}

    void Save() {}
    void Restore() {}
};

class CubismRenderer_PortableGL : public CubismRenderer
{
    friend class CubismRenderer;
    friend class CubismClippingManager_PortableGL;
    friend class CubismShader_PortableGL;

public:
    struct FrameStats
    {
        csmFloat32 MaskPassMilliseconds;
        csmFloat32 MainDrawMilliseconds;
        csmFloat32 SubmitRenderTargetMilliseconds;
        csmFloat32 SubmitSortMilliseconds;
        csmFloat32 SubmitRenderObjectMilliseconds;
        csmFloat32 SubmitTailFlushMilliseconds;
        csmFloat32 SubmitParentSubmitMilliseconds;
        csmFloat32 SubmitAddOffscreenMilliseconds;
        csmFloat32 SubmitDrawOffscreenMilliseconds;
        csmFloat32 SubmitMeshTotalMilliseconds;
        csmFloat32 SubmitMeshShaderMilliseconds;
        csmFloat32 SubmitMeshDrawMilliseconds;
        csmFloat32 SubmitCompositeMilliseconds;
        csmFloat32 PipelineMilliseconds;
        csmFloat32 VertexStageMilliseconds;
        csmFloat32 PrimitiveLoopMilliseconds;
        csmFloat32 TriangleFastMilliseconds;
        csmFloat32 TriangleSampleMilliseconds;
        csmFloat32 TriangleShadeMilliseconds;
        csmFloat32 TriangleBlendMilliseconds;
        csmFloat32 TriangleScalarMilliseconds;
        csmFloat32 TriangleScalarTailMilliseconds;
        csmUint32 PipelineCallCount;
        csmUint32 FastTriangleCount;
        csmUint32 ScalarTriangleCount;
        csmUint32 SampleCallCount;
        csmUint32 VisibleDrawableCount;
        csmUint32 MaskedDrawableCount;
        csmUint32 MaskDrawCallCount;
        csmUint32 MainDrawCallCount;
        csmUint32 TotalIndexCount;
        csmUint32 TotalTriangleCount;
        csmUint32 MaskIndexCount;
        csmUint32 MaskTriangleCount;
        csmUint32 MainIndexCount;
        csmUint32 MainTriangleCount;
    };

    void Initialize(Framework::CubismModel* model) override;
    void Initialize(Framework::CubismModel* model, csmInt32 maskBufferCount) override;
    void SetupParentOffscreens(const CubismModel* model, csmInt32 offscreenCount);

    void BindTexture(csmUint32 modelTextureIndex, GLuint glTextureIndex);
    const csmMap<csmInt32, GLuint>& GetBindedTextures() const;

    void SetDrawableClippingMaskBufferSize(csmFloat32 width, csmFloat32 height);
    void SetOffscreenClippingMaskBufferSize(csmFloat32 width, csmFloat32 height);

    csmInt32 GetDrawableRenderTextureCount() const;
    csmInt32 GetOffscreenRenderTextureCount() const;
    CubismVector2 GetDrawableClippingMaskBufferSize() const;
    CubismVector2 GetOffscreenClippingMaskBufferSize() const;

    const CubismRenderTarget_PortableGL* CopyOffscreenRenderTarget();
    const CubismRenderTarget_PortableGL* CopyRenderTarget(const CubismRenderTarget_PortableGL& srcBuffer);

    CubismRenderTarget_PortableGL* GetDrawableMaskBuffer(csmInt32 index);
    CubismRenderTarget_PortableGL* GetOffscreenMaskBuffer(csmInt32 index);
    CubismOffscreenRenderTarget_PortableGL* GetCurrentOffscreen() const;
    const FrameStats& GetLastFrameStats() const;

protected:
    CubismRenderer_PortableGL(csmUint32 width, csmUint32 height);
    virtual ~CubismRenderer_PortableGL();

    virtual void DoDrawModel() override;

    void DrawObjectLoop(GLint lastFBO, GLint lastViewport[4]);
    void RenderObject(csmInt32 objectIndex, csmInt32 objectType);
    void DrawDrawable(csmInt32 drawableIndex);
    void SubmitDrawToParentOffscreen(csmInt32 objectIndex, DrawableObjectType objectType);
    void AddOffscreen(csmInt32 offscreenIndex);
    void DrawOffscreen(CubismOffscreenRenderTarget_PortableGL* currentOffscreen);
    void DrawMeshPortableGL(const CubismModel& model, csmInt32 index);
    void DrawOffscreenPortableGL(const CubismModel& model, CubismOffscreenRenderTarget_PortableGL* offscreen);

private:
    CubismRenderer_PortableGL(const CubismRenderer_PortableGL&);
    CubismRenderer_PortableGL& operator=(const CubismRenderer_PortableGL&);

    static void DoStaticRelease();
    static csmBool CanUseTextureBarrier();

    void PreDraw();
    void PostDraw() {}

    void SaveProfile() override;
    void RestoreProfile() override;

    virtual void BeforeDrawModelRenderTarget();
    virtual void AfterDrawModelRenderTarget();

    void SetClippingContextBufferForMask(CubismClippingContext_PortableGL* clip);
    CubismClippingContext_PortableGL* GetClippingContextBufferForMask() const;

    void SetClippingContextBufferForDrawable(CubismClippingContext_PortableGL* clip);
    CubismClippingContext_PortableGL* GetClippingContextBufferForDrawable() const;

    void SetClippingContextBufferForOffscreen(CubismClippingContext_PortableGL* clip);
    CubismClippingContext_PortableGL* GetClippingContextBufferForOffscreen() const;

    csmBool inline IsGeneratingMask() const;
    GLuint GetBindedTextureId(csmInt32 textureId);
    void ResetFrameStats();
    void AddMaskPassMilliseconds(csmFloat32 milliseconds);
    void AddDrawCallGeometry(csmInt32 indexCount, csmBool isMaskPass);
    void AddOffscreenGeometry(csmInt32 indexCount);

    csmMap<csmInt32, GLuint> _textures;
    csmVector<csmInt32> _sortedObjectsIndexList;
    csmVector<DrawableObjectType> _sortedObjectsTypeList;
    CubismRendererProfile_PortableGL _rendererProfile;
    CubismClippingManager_PortableGL* _drawableClippingManager;
    CubismClippingManager_PortableGL* _offscreenClippingManager;
    CubismClippingContext_PortableGL* _clippingContextBufferForMask;
    CubismClippingContext_PortableGL* _clippingContextBufferForDrawable;
    CubismClippingContext_PortableGL* _clippingContextBufferForOffscreen;

    csmVector<CubismRenderTarget_PortableGL> _modelRenderTargets;
    csmVector<CubismRenderTarget_PortableGL> _drawableMasks;
    csmVector<CubismRenderTarget_PortableGL> _offscreenMasks;

    csmVector<CubismOffscreenRenderTarget_PortableGL> _offscreenList;
    GLint _currentFBO;
    CubismOffscreenRenderTarget_PortableGL* _currentOffscreen;
    GLint _modelRootFBO;
    FrameStats _frameStats;
};

}}}}
