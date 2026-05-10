/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include "../ICubismOffscreenRenderTarget.hpp"
#include "CubismRenderTarget_PortableGL.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

class CubismOffscreenRenderTarget_PortableGL : public ICubismOffscreenRenderTarget<CubismOffscreenRenderTarget_PortableGL, CubismRenderTarget_PortableGL>
{
public:
    CubismOffscreenRenderTarget_PortableGL();
    ~CubismOffscreenRenderTarget_PortableGL();

    void SetOffscreenRenderTarget(csmUint32 width, csmUint32 height);
    csmBool GetUsingRenderTextureState() const;
    void StopUsingRenderTexture();
};

}}}}
