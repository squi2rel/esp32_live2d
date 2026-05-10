/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "CubismRenderTarget_PortableGL.hpp"

#include <cstring>

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

namespace {
std::size_t s_allocatedRenderTargetBytes = 0;
std::size_t s_allocatedRenderTargetCount = 0;
}

void CubismRenderTarget_PortableGL::CopyBuffer(const CubismRenderTarget_PortableGL& src, const CubismRenderTarget_PortableGL& dst)
{
    if (!src.IsValid() || !dst.IsValid())
    {
        return;
    }

    void* srcData = NULL;
    void* dstData = NULL;
    pglGetTextureData(src._colorBuffer, &srcData);
    pglGetTextureData(dst._colorBuffer, &dstData);

    if (!srcData || !dstData)
    {
        return;
    }

    const csmUint32 copyWidth = src._bufferWidth < dst._bufferWidth ? src._bufferWidth : dst._bufferWidth;
    const csmUint32 copyHeight = src._bufferHeight < dst._bufferHeight ? src._bufferHeight : dst._bufferHeight;
    const size_t rowBytes = static_cast<size_t>(copyWidth) * sizeof(pix_t);

    for (csmUint32 y = 0; y < copyHeight; ++y)
    {
        std::memcpy(
            static_cast<pix_t*>(dstData) + y * dst._bufferWidth,
            static_cast<const pix_t*>(srcData) + y * src._bufferWidth,
            rowBytes
        );
    }
}

std::size_t CubismRenderTarget_PortableGL::GetAllocatedRenderTargetBytes()
{
    return s_allocatedRenderTargetBytes;
}

std::size_t CubismRenderTarget_PortableGL::GetAllocatedRenderTargetCount()
{
    return s_allocatedRenderTargetCount;
}

CubismRenderTarget_PortableGL::CubismRenderTarget_PortableGL()
    : _renderTexture(0)
    , _colorBuffer(0)
    , _oldFBO(0)
    , _oldViewport{0, 0, 0, 0}
    , _bufferWidth(0)
    , _bufferHeight(0)
    , _isColorBufferInherited(false)
    , _ownedColorBufferBytes(0)
{
}

void CubismRenderTarget_PortableGL::BeginDraw(GLint restoreFBO)
{
    if (_renderTexture == 0)
    {
        return;
    }

    _oldFBO = restoreFBO >= 0 ? restoreFBO : static_cast<GLint>(CubismPortableGLGetCurrentRenderTarget());
    glGetIntegerv(GL_VIEWPORT, _oldViewport);
    CubismPortableGLBindRenderTarget(_renderTexture);
}

void CubismRenderTarget_PortableGL::EndDraw()
{
    if (_renderTexture == 0)
    {
        return;
    }

    CubismPortableGLBindRenderTarget(static_cast<GLuint>(_oldFBO));
    glViewport(_oldViewport[0], _oldViewport[1], _oldViewport[2], _oldViewport[3]);
}

void CubismRenderTarget_PortableGL::Clear(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

csmBool CubismRenderTarget_PortableGL::CreateRenderTarget(csmUint32 displayBufferWidth, csmUint32 displayBufferHeight, GLuint colorBuffer)
{
    DestroyRenderTarget();

    if (colorBuffer == 0)
    {
        glGenTextures(1, &_colorBuffer);
        glBindTexture(GL_TEXTURE_2D, _colorBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, displayBufferWidth, displayBufferHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        _isColorBufferInherited = false;
    }
    else
    {
        _colorBuffer = colorBuffer;
        _isColorBufferInherited = true;
    }

    _renderTexture = _colorBuffer;
    _bufferWidth = displayBufferWidth;
    _bufferHeight = displayBufferHeight;
    _ownedColorBufferBytes = 0;

    if (!_isColorBufferInherited && _renderTexture != 0)
    {
        _ownedColorBufferBytes = static_cast<std::size_t>(displayBufferWidth) *
            static_cast<std::size_t>(displayBufferHeight) * sizeof(pix_t);
        s_allocatedRenderTargetBytes += _ownedColorBufferBytes;
        ++s_allocatedRenderTargetCount;
    }

    return _renderTexture != 0;
}

void CubismRenderTarget_PortableGL::DestroyRenderTarget()
{
    if (_ownedColorBufferBytes != 0)
    {
        s_allocatedRenderTargetBytes -= _ownedColorBufferBytes;
        --s_allocatedRenderTargetCount;
        _ownedColorBufferBytes = 0;
    }

    if (!_isColorBufferInherited && _colorBuffer != 0)
    {
        glDeleteTextures(1, &_colorBuffer);
    }

    _renderTexture = 0;
    _colorBuffer = 0;
    _bufferWidth = 0;
    _bufferHeight = 0;
    _isColorBufferInherited = false;
}

GLuint CubismRenderTarget_PortableGL::GetRenderTexture() const
{
    return _renderTexture;
}

GLuint CubismRenderTarget_PortableGL::GetColorBuffer() const
{
    return _colorBuffer;
}

csmUint32 CubismRenderTarget_PortableGL::GetBufferWidth() const
{
    return _bufferWidth;
}

csmUint32 CubismRenderTarget_PortableGL::GetBufferHeight() const
{
    return _bufferHeight;
}

csmBool CubismRenderTarget_PortableGL::IsValid() const
{
    return _renderTexture != 0;
}

GLint CubismRenderTarget_PortableGL::GetOldFBO() const
{
    return _oldFBO;
}

}}}}
