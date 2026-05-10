/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "CubismOffscreenRenderTarget_PortableGL.hpp"
#include "CubismOffscreenManager_PortableGL.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

CubismOffscreenRenderTarget_PortableGL::CubismOffscreenRenderTarget_PortableGL()
{
}

CubismOffscreenRenderTarget_PortableGL::~CubismOffscreenRenderTarget_PortableGL()
{
}

void CubismOffscreenRenderTarget_PortableGL::SetOffscreenRenderTarget(csmUint32 width, csmUint32 height)
{
    if (GetUsingRenderTextureState())
    {
        if (_renderTarget->GetBufferWidth() != width || _renderTarget->GetBufferHeight() != height)
        {
            _renderTarget->CreateRenderTarget(width, height);
        }
        return;
    }

    _renderTarget = CubismOffscreenManager_PortableGL::GetInstance()->GetOffscreenRenderTarget(width, height);
}

csmBool CubismOffscreenRenderTarget_PortableGL::GetUsingRenderTextureState() const
{
    return CubismOffscreenManager_PortableGL::GetInstance()->GetUsingRenderTextureState(_renderTarget);
}

void CubismOffscreenRenderTarget_PortableGL::StopUsingRenderTexture()
{
    CubismOffscreenManager_PortableGL::GetInstance()->StopUsingRenderTexture(_renderTarget);
    _renderTarget = nullptr;
}

}}}}
