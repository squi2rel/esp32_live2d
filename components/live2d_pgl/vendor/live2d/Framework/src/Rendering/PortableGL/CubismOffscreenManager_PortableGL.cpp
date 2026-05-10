/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "CubismOffscreenManager_PortableGL.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

namespace {
    CubismOffscreenManager_PortableGL* s_instance = nullptr;
}

CubismOffscreenManager_PortableGL* CubismOffscreenManager_PortableGL::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new CubismOffscreenManager_PortableGL();
    }

    return s_instance;
}

void CubismOffscreenManager_PortableGL::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

CubismRenderTarget_PortableGL* CubismOffscreenManager_PortableGL::GetOffscreenRenderTarget(csmUint32 width, csmUint32 height)
{
    UpdateRenderTargetCount();

    CubismRenderTarget_PortableGL* offscreenRenderTarget = GetUnusedOffscreenRenderTarget();
    if (offscreenRenderTarget != nullptr)
    {
        if (offscreenRenderTarget->GetBufferWidth() != width || offscreenRenderTarget->GetBufferHeight() != height)
        {
            offscreenRenderTarget->CreateRenderTarget(width, height);
        }

        return offscreenRenderTarget;
    }

    offscreenRenderTarget = CreateOffscreenRenderTarget();
    offscreenRenderTarget->CreateRenderTarget(width, height);
    return offscreenRenderTarget;
}

CubismOffscreenManager_PortableGL::CubismOffscreenManager_PortableGL()
{
}

CubismOffscreenManager_PortableGL::~CubismOffscreenManager_PortableGL()
{
}

}}}}
