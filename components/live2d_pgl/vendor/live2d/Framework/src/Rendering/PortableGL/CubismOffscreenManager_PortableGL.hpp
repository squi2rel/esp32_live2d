/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include "../ICubismOffscreenManager.hpp"
#include "CubismRenderTarget_PortableGL.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

class CubismOffscreenManager_PortableGL : public ICubismOffscreenManager<CubismRenderTarget_PortableGL>
{
public:
    static CubismOffscreenManager_PortableGL* GetInstance();
    static void ReleaseInstance();

    CubismRenderTarget_PortableGL* GetOffscreenRenderTarget(csmUint32 width, csmUint32 height);

private:
    CubismOffscreenManager_PortableGL();
    ~CubismOffscreenManager_PortableGL();
};

}}}}
