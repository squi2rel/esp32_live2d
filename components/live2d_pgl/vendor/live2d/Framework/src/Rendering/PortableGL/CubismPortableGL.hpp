#pragma once

#include "CubismFramework.hpp"

#ifndef PGL_PREFIX_TYPES
#define PGL_PREFIX_TYPES
#endif

#ifndef PGL_EXCLUDE_STUBS
#define PGL_EXCLUDE_STUBS
#endif

#ifndef PGL_NO_DEPTH_NO_STENCIL
#define PGL_NO_DEPTH_NO_STENCIL
#endif

#if defined(CSM_TARGET_ESP_PGL)
#ifndef GL_MAX_VERTEX_ATTRIBS
#define GL_MAX_VERTEX_ATTRIBS 4
#endif

#ifndef PGL_MAX_VERTICES
#define PGL_MAX_VERTICES 5000
#endif
#endif

#include "../../../../../PortableGL/portablegl.h"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

struct CubismPortableGLMainFramebufferState
{
    pix_t* Pixels;
    csmUint32 Width;
    csmUint32 Height;
    GLboolean UserOwned;
};

struct CubismPortableGLPerfStats
{
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
};

void CubismPortableGLSetMainFramebuffer(pix_t* pixels, csmUint32 width, csmUint32 height, GLboolean userOwned);
void CubismPortableGLRefreshFramebufferState();
void CubismPortableGLBindMainFramebuffer();
void CubismPortableGLBindRenderTarget(GLuint renderTarget);
GLuint CubismPortableGLGetCurrentRenderTarget();
const CubismPortableGLMainFramebufferState& CubismPortableGLGetMainFramebufferState();
void CubismPortableGLResetPerfStats();
const CubismPortableGLPerfStats& CubismPortableGLGetPerfStats();

}}}}
