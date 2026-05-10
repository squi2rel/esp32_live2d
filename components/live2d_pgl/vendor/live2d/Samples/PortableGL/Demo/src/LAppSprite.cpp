/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppSprite.hpp"

#include "LAppDelegate.hpp"
#include "LAppSpriteShader.hpp"

LAppSprite::LAppSprite(float x, float y, float width, float height, GLuint textureId, GLuint programId)
    : LAppSprite_Common(textureId)
    , _rect()
    , _programId(programId)
{
    _rect.left = x - width * 0.5f;
    _rect.right = x + width * 0.5f;
    _rect.up = y + height * 0.5f;
    _rect.down = y - height * 0.5f;

    _spriteColor[0] = 1.0f;
    _spriteColor[1] = 1.0f;
    _spriteColor[2] = 1.0f;
    _spriteColor[3] = 1.0f;
}

LAppSprite::~LAppSprite()
{
}

void LAppSprite::Render() const
{
    const GLfloat uvVertex[] =
    {
        1.0f, 0.0f,
        0.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    RenderImmidiate(_textureId, uvVertex);
}

void LAppSprite::RenderImmidiate(GLuint textureId, const GLfloat uvVertex[8]) const
{
    int maxWidth = 0;
    int maxHeight = 0;
    LAppDelegate::GetClientSize(maxWidth, maxHeight);
    if (maxWidth == 0 || maxHeight == 0)
    {
        return;
    }

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    float positionVertex[] =
    {
        (_rect.right - maxWidth * 0.5f) / (maxWidth * 0.5f), (_rect.up - maxHeight * 0.5f) / (maxHeight * 0.5f),
        (_rect.left - maxWidth * 0.5f) / (maxWidth * 0.5f), (_rect.up - maxHeight * 0.5f) / (maxHeight * 0.5f),
        (_rect.left - maxWidth * 0.5f) / (maxWidth * 0.5f), (_rect.down - maxHeight * 0.5f) / (maxHeight * 0.5f),
        (_rect.right - maxWidth * 0.5f) / (maxWidth * 0.5f), (_rect.down - maxHeight * 0.5f) / (maxHeight * 0.5f)
    };

    LAppSpriteShader::ApplyUniforms(_programId, textureId, _spriteColor);
    glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, positionVertex);
    glVertexAttribPointer(1, 2, GL_FLOAT, false, 0, uvVertex);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glUseProgram(0);
}

bool LAppSprite::IsHit(float pointX, float pointY) const
{
    int maxWidth = 0;
    int maxHeight = 0;
    LAppDelegate::GetClientSize(maxWidth, maxHeight);
    if (maxWidth == 0 || maxHeight == 0)
    {
        return false;
    }

    const float y = maxHeight - pointY;
    return pointX >= _rect.left && pointX <= _rect.right && y <= _rect.up && y >= _rect.down;
}

void LAppSprite::SetColor(float r, float g, float b, float a)
{
    _spriteColor[0] = r;
    _spriteColor[1] = g;
    _spriteColor[2] = b;
    _spriteColor[3] = a;
}

void LAppSprite::ResetRect(float x, float y, float width, float height)
{
    _rect.left = x - width * 0.5f;
    _rect.right = x + width * 0.5f;
    _rect.up = y + height * 0.5f;
    _rect.down = y - height * 0.5f;
}
