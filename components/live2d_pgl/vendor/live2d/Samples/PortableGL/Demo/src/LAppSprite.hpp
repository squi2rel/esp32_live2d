/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <Rendering/PortableGL/CubismPortableGL.hpp>

#include "LAppSprite_Common.hpp"

class LAppSprite : public LAppSprite_Common
{
public:
    LAppSprite(float x, float y, float width, float height, GLuint textureId, GLuint programId);
    ~LAppSprite();

    void Render() const;
    void RenderImmidiate(GLuint textureId, const GLfloat uvVertex[8]) const;
    bool IsHit(float pointX, float pointY) const;
    void SetColor(float r, float g, float b, float a);
    void ResetRect(float x, float y, float width, float height);

private:
    Rect _rect;
    GLuint _programId;
    float _spriteColor[4];
};
