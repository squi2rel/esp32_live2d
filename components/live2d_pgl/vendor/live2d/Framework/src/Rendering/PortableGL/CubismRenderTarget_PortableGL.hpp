/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <cstddef>

#include "CubismFramework.hpp"
#include "CubismPortableGL.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

class CubismRenderTarget_PortableGL
{
public:
    static void CopyBuffer(const CubismRenderTarget_PortableGL& src, const CubismRenderTarget_PortableGL& dst);
    static std::size_t GetAllocatedRenderTargetBytes();
    static std::size_t GetAllocatedRenderTargetCount();

    CubismRenderTarget_PortableGL();

    void BeginDraw(GLint restoreFBO = -1);
    void EndDraw();
    void Clear(float r, float g, float b, float a);
    csmBool CreateRenderTarget(csmUint32 displayBufferWidth, csmUint32 displayBufferHeight, GLuint colorBuffer = 0);
    void DestroyRenderTarget();

    GLuint GetRenderTexture() const;
    GLuint GetColorBuffer() const;
    csmUint32 GetBufferWidth() const;
    csmUint32 GetBufferHeight() const;
    csmBool IsValid() const;
    GLint GetOldFBO() const;

private:
    GLuint _renderTexture;
    GLuint _colorBuffer;
    GLint _oldFBO;
    GLint _oldViewport[4];
    csmUint32 _bufferWidth;
    csmUint32 _bufferHeight;
    csmBool _isColorBufferInherited;
    std::size_t _ownedColorBufferBytes;
};

}}}}
