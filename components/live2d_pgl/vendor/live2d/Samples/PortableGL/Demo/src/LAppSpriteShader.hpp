/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <Rendering/PortableGL/CubismPortableGL.hpp>

class LAppSpriteShader
{
public:
    LAppSpriteShader();
    ~LAppSpriteShader();

    GLuint GetShaderId() const;
    static void ApplyUniforms(GLuint programId, GLuint textureId, const float color[4]);

private:
    GLuint CreateShader();

    GLuint _programId;
};
