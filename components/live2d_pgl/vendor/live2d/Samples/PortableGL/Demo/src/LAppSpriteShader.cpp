/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppSpriteShader.hpp"

#include <cstdlib>

namespace {
struct SpriteUniforms
{
    GLuint Texture;
    pgl_vec4 BaseColor;
};

thread_local SpriteUniforms s_spriteUniforms = {};

void SpriteVertexShader(float* vs_output, pgl_vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(uniforms);

    pgl_vec4 position = vertex_attribs[0];
    position.z = 0.0f;
    position.w = 1.0f;
    builtins->gl_Position = position;

    vs_output[0] = vertex_attribs[1].x;
    vs_output[1] = vertex_attribs[1].y;
}

void SpriteFragmentShader(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    const SpriteUniforms* shaderUniforms = static_cast<const SpriteUniforms*>(uniforms);
    static const bool debugSolid = (std::getenv("LAPP_PGL_DEBUG_SOLID_SPRITES") != NULL);
    if (debugSolid)
    {
        builtins->gl_FragColor = shaderUniforms->BaseColor;
        return;
    }

    const pgl_vec4 texColor = texture2D(shaderUniforms->Texture, fs_input[0], fs_input[1]);
    builtins->gl_FragColor = pgl_vec4{
        texColor.x * shaderUniforms->BaseColor.x,
        texColor.y * shaderUniforms->BaseColor.y,
        texColor.z * shaderUniforms->BaseColor.z,
        texColor.w * shaderUniforms->BaseColor.w
    };
}
}

LAppSpriteShader::LAppSpriteShader()
{
    _programId = CreateShader();
}

LAppSpriteShader::~LAppSpriteShader()
{
    glDeleteProgram(_programId);
}

GLuint LAppSpriteShader::GetShaderId() const
{
    return _programId;
}

void LAppSpriteShader::ApplyUniforms(GLuint programId, GLuint textureId, const float color[4])
{
    s_spriteUniforms = {};
    s_spriteUniforms.Texture = textureId;
    s_spriteUniforms.BaseColor = pgl_vec4{color[0], color[1], color[2], color[3]};
    pglSetProgramUniform(programId, &s_spriteUniforms);
    glUseProgram(programId);
}

GLuint LAppSpriteShader::CreateShader()
{
    const GLenum interpolation[2] = {PGL_SMOOTH2};
    return pglCreateProgram(SpriteVertexShader, SpriteFragmentShader, 2, const_cast<GLenum*>(interpolation), GL_FALSE);
}
