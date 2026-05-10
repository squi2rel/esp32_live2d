/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "CubismShader_PortableGL.hpp"

#include "CubismPortableGLFastPath.hpp"
#include "CubismRenderer_PortableGL.hpp"
#include "Type/csmRectF.hpp"
#include "sdkconfig.h"

#include <cstring>

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

namespace {
    CubismShader_PortableGL* s_instance = NULL;
    csmBool s_loggedBlendFallback = false;

    const csmFloat32 renderTargetVertexArray[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    const csmFloat32 renderTargetUvArray[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    const csmFloat32 renderTargetReverseUvArray[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
    };

    thread_local CubismPortableGLCopyUniforms s_copyUniforms = {};
    thread_local CubismPortableGLSetupMaskUniforms s_setupMaskUniforms = {};
    thread_local CubismPortableGLDrawableUniforms s_drawableUniforms = {};
    thread_local CubismPortableGLMaskedDrawableUniforms s_maskedDrawableUniforms = {};

    inline pgl_vec4 MakeVec4(csmFloat32 x, csmFloat32 y, csmFloat32 z, csmFloat32 w)
    {
        pgl_vec4 value = {x, y, z, w};
        return value;
    }

    inline pgl_vec4 ToPglColor(const CubismRenderer::CubismTextureColor& color)
    {
        return MakeVec4(color.R, color.G, color.B, color.A);
    }

    void CopyMatrix(pgl_mat4 dst, const csmFloat32* src)
    {
        std::memcpy(dst, src, sizeof(csmFloat32) * 16);
    }

    pgl_vec4 MultiplyVec4(const pgl_vec4& lhs, const pgl_vec4& rhs)
    {
        return MakeVec4(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w);
    }

    pgl_vec4 ApplyMultiplyAndScreen(pgl_vec4 texColor, const pgl_vec4& multiplyColor, const pgl_vec4& screenColor, csmBool premultipliedAlpha)
    {
        texColor.x *= multiplyColor.x;
        texColor.y *= multiplyColor.y;
        texColor.z *= multiplyColor.z;

        if (premultipliedAlpha)
        {
            texColor.x = (texColor.x + screenColor.x * texColor.w) - (texColor.x * screenColor.x);
            texColor.y = (texColor.y + screenColor.y * texColor.w) - (texColor.y * screenColor.y);
            texColor.z = (texColor.z + screenColor.z * texColor.w) - (texColor.z * screenColor.z);
        }
        else
        {
            texColor.x = texColor.x + screenColor.x - (texColor.x * screenColor.x);
            texColor.y = texColor.y + screenColor.y - (texColor.y * screenColor.y);
            texColor.z = texColor.z + screenColor.z - (texColor.z * screenColor.z);
        }

        return texColor;
    }

    pgl_vec4 EvaluateDrawableColor(
        GLuint texture,
        float u,
        float v,
        const pgl_vec4& baseColor,
        const pgl_vec4& multiplyColor,
        const pgl_vec4& screenColor,
        csmBool premultipliedAlpha)
    {
        pgl_vec4 texColor = texture2D(texture, u, v);
        texColor = ApplyMultiplyAndScreen(texColor, multiplyColor, screenColor, premultipliedAlpha);

        pgl_vec4 outColor = MultiplyVec4(texColor, baseColor);
        if (!premultipliedAlpha)
        {
            outColor.x *= outColor.w;
            outColor.y *= outColor.w;
            outColor.z *= outColor.w;
        }

        return outColor;
    }

    float EvaluateMaskValue(GLuint texture, const pgl_vec4& channelFlag, const float* clipPos)
    {
        const float invW = clipPos[3] != 0.0f ? (1.0f / clipPos[3]) : 0.0f;
        const float u = clipPos[0] * invW;
        const float v = 1.0f - (clipPos[1] * invW);
        const pgl_vec4 clipMask = texture2D(texture, u, v);
        return (1.0f - clipMask.x) * channelFlag.x
             + (1.0f - clipMask.y) * channelFlag.y
             + (1.0f - clipMask.z) * channelFlag.z
             + (1.0f - clipMask.w) * channelFlag.w;
    }

    void CopyVs(float* vs_output, pgl_vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
    {
        PGL_UNUSED(uniforms);

        pgl_vec4 position = vertex_attribs[0];
        position.z = 0.0f;
        position.w = 1.0f;
        builtins->gl_Position = position;

        vs_output[0] = vertex_attribs[1].x;
        vs_output[1] = 1.0f - vertex_attribs[1].y;
    }

    void DrawableVs(float* vs_output, pgl_vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
    {
        CubismPortableGLDrawableUniforms* shaderUniforms = static_cast<CubismPortableGLDrawableUniforms*>(uniforms);

        pgl_vec4 position = vertex_attribs[0];
        position.z = 0.0f;
        position.w = 1.0f;
        builtins->gl_Position = mult_m4_v4(shaderUniforms->Matrix, position);

        vs_output[0] = vertex_attribs[1].x;
        vs_output[1] = 1.0f - vertex_attribs[1].y;
    }

    void MaskedDrawableVs(float* vs_output, pgl_vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
    {
        CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<CubismPortableGLMaskedDrawableUniforms*>(uniforms);

        pgl_vec4 position = vertex_attribs[0];
        position.z = 0.0f;
        position.w = 1.0f;
        builtins->gl_Position = mult_m4_v4(shaderUniforms->Matrix, position);

        const pgl_vec4 clipPosition = mult_m4_v4(shaderUniforms->ClipMatrix, position);

        vs_output[0] = vertex_attribs[1].x;
        vs_output[1] = 1.0f - vertex_attribs[1].y;
        vs_output[2] = clipPosition.x;
        vs_output[3] = clipPosition.y;
        vs_output[4] = clipPosition.z;
        vs_output[5] = clipPosition.w;
    }

    void SetupMaskVs(float* vs_output, pgl_vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
    {
        CubismPortableGLSetupMaskUniforms* shaderUniforms = static_cast<CubismPortableGLSetupMaskUniforms*>(uniforms);

        pgl_vec4 position = vertex_attribs[0];
        position.z = 0.0f;
        position.w = 1.0f;
        const pgl_vec4 clipPosition = mult_m4_v4(shaderUniforms->ClipMatrix, position);

        builtins->gl_Position = clipPosition;

        vs_output[0] = vertex_attribs[1].x;
        vs_output[1] = 1.0f - vertex_attribs[1].y;
        vs_output[2] = clipPosition.x;
        vs_output[3] = clipPosition.y;
        vs_output[4] = clipPosition.z;
        vs_output[5] = clipPosition.w;
    }

    void CopyFs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        const CubismPortableGLCopyUniforms* shaderUniforms = static_cast<const CubismPortableGLCopyUniforms*>(uniforms);
        const pgl_vec4 texColor = texture2D(shaderUniforms->Texture0, fs_input[0], fs_input[1]);
        builtins->gl_FragColor = MultiplyVec4(texColor, shaderUniforms->BaseColor);
    }

    void DrawableFs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        const CubismPortableGLDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLDrawableUniforms*>(uniforms);
        builtins->gl_FragColor = EvaluateDrawableColor(
            shaderUniforms->Texture0,
            fs_input[0],
            fs_input[1],
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            false
        );
    }

    void DrawableMaskedFs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniforms);

        pgl_vec4 color = EvaluateDrawableColor(
            shaderUniforms->Texture0,
            fs_input[0],
            fs_input[1],
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            false
        );

        const float maskValue = EvaluateMaskValue(shaderUniforms->Texture1, shaderUniforms->ChannelFlag, fs_input + 2);
        color.x *= maskValue;
        color.y *= maskValue;
        color.z *= maskValue;
        color.w *= maskValue;
        builtins->gl_FragColor = color;
    }

    void DrawableMaskedInvertedFs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniforms);

        pgl_vec4 color = EvaluateDrawableColor(
            shaderUniforms->Texture0,
            fs_input[0],
            fs_input[1],
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            false
        );

        const float maskValue = 1.0f - EvaluateMaskValue(shaderUniforms->Texture1, shaderUniforms->ChannelFlag, fs_input + 2);
        color.x *= maskValue;
        color.y *= maskValue;
        color.z *= maskValue;
        color.w *= maskValue;
        builtins->gl_FragColor = color;
    }

    void DrawablePremultipliedFs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        const CubismPortableGLDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLDrawableUniforms*>(uniforms);
        builtins->gl_FragColor = EvaluateDrawableColor(
            shaderUniforms->Texture0,
            fs_input[0],
            fs_input[1],
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            true
        );
    }

    void DrawableMaskedPremultipliedFs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniforms);

        pgl_vec4 color = EvaluateDrawableColor(
            shaderUniforms->Texture0,
            fs_input[0],
            fs_input[1],
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            true
        );

        const float maskValue = EvaluateMaskValue(shaderUniforms->Texture1, shaderUniforms->ChannelFlag, fs_input + 2);
        color.x *= maskValue;
        color.y *= maskValue;
        color.z *= maskValue;
        color.w *= maskValue;
        builtins->gl_FragColor = color;
    }

    void DrawableMaskedInvertedPremultipliedFs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniforms);

        pgl_vec4 color = EvaluateDrawableColor(
            shaderUniforms->Texture0,
            fs_input[0],
            fs_input[1],
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            true
        );

        const float maskValue = 1.0f - EvaluateMaskValue(shaderUniforms->Texture1, shaderUniforms->ChannelFlag, fs_input + 2);
        color.x *= maskValue;
        color.y *= maskValue;
        color.z *= maskValue;
        color.w *= maskValue;
        builtins->gl_FragColor = color;
    }

    void SetupMaskFs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        const CubismPortableGLSetupMaskUniforms* shaderUniforms = static_cast<const CubismPortableGLSetupMaskUniforms*>(uniforms);

        const float x = fs_input[2] / fs_input[5];
        const float y = fs_input[3] / fs_input[5];
        const float isInside =
            (x >= shaderUniforms->BaseColor.x ? 1.0f : 0.0f) *
            (y >= shaderUniforms->BaseColor.y ? 1.0f : 0.0f) *
            (x <= shaderUniforms->BaseColor.z ? 1.0f : 0.0f) *
            (y <= shaderUniforms->BaseColor.w ? 1.0f : 0.0f);

        const float alpha = texture2D(shaderUniforms->Texture0, fs_input[0], fs_input[1]).w * isInside;
        builtins->gl_FragColor = MakeVec4(
            shaderUniforms->ChannelFlag.x * alpha,
            shaderUniforms->ChannelFlag.y * alpha,
            shaderUniforms->ChannelFlag.z * alpha,
            shaderUniforms->ChannelFlag.w * alpha
        );
    }
}

CubismShader_PortableGL::CubismShader_PortableGL()
{
}

CubismShader_PortableGL::~CubismShader_PortableGL()
{
    ReleaseShaderProgram();
}

CubismShader_PortableGL* CubismShader_PortableGL::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = CSM_NEW CubismShader_PortableGL();
        s_instance->GenerateShaders();
    }

    return s_instance;
}

void CubismShader_PortableGL::DeleteInstance()
{
    if (s_instance)
    {
        CSM_DELETE_SELF(CubismShader_PortableGL, s_instance);
        s_instance = NULL;
    }
}

void CubismShader_PortableGL::ReleaseShaderProgram()
{
    for (csmUint32 i = 0; i < _shaderSets.GetSize(); ++i)
    {
        if (_shaderSets[i] && _shaderSets[i]->ShaderProgram)
        {
            glDeleteProgram(_shaderSets[i]->ShaderProgram);
        }
        CSM_DELETE(_shaderSets[i]);
    }

    _shaderSets.Clear();
}

void CubismShader_PortableGL::ReleaseInvalidShaderProgram()
{
    for (csmUint32 i = 0; i < _shaderSets.GetSize(); ++i)
    {
        CSM_DELETE(_shaderSets[i]);
    }

    _shaderSets.Clear();
}

csmInt32 CubismShader_PortableGL::GetShaderNamesBegin(const csmBlendMode blendMode)
{
    if (blendMode.GetAlphaBlendType() != Core::csmAlphaBlendType_Over
        || (blendMode.GetColorBlendType() != Core::csmColorBlendType_Normal
            && blendMode.GetColorBlendType() != Core::csmColorBlendType_AddCompatible
            && blendMode.GetColorBlendType() != Core::csmColorBlendType_MultiplyCompatible))
    {
        if (!s_loggedBlendFallback)
        {
            s_loggedBlendFallback = true;
            CubismLogWarning("PortableGL renderer falls back to normal-over for unsupported Cubism blend modes.");
        }
        return ShaderNames_Normal;
    }

    switch (blendMode.GetColorBlendType())
    {
    case Core::csmColorBlendType_AddCompatible:
        return ShaderNames_Add;
    case Core::csmColorBlendType_MultiplyCompatible:
        return ShaderNames_Mult;
    case Core::csmColorBlendType_Normal:
    default:
        return ShaderNames_Normal;
    }
}

void CubismShader_PortableGL::GenerateShaders()
{
    if (_shaderSets.GetSize() > 0)
    {
        return;
    }

    for (csmInt32 i = 0; i < ShaderNames_ShaderCount; ++i)
    {
        _shaderSets.PushBack(CSM_NEW CubismShaderSet());
        _shaderSets[i]->ShaderProgram = 0;
    }

    const GLenum uvInterpolation[2] = {PGL_SMOOTH2};
    const GLenum maskedInterpolation[6] = {PGL_SMOOTH2, PGL_SMOOTH4};

    auto setProgramTag = [](GLuint program, CubismPortableGLProgramTag tag)
    {
        pglSetProgramUserTag(program, static_cast<GLint>(tag));
    };

    _shaderSets[ShaderNames_Copy]->ShaderProgram = pglCreateProgram(CopyVs, CopyFs, 2, const_cast<GLenum*>(uvInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_Copy]->ShaderProgram, CubismPortableGLProgramTag_Copy);
    _shaderSets[ShaderNames_SetupMask]->ShaderProgram = pglCreateProgram(SetupMaskVs, SetupMaskFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_SetupMask]->ShaderProgram, CubismPortableGLProgramTag_SetupMask);

    _shaderSets[ShaderNames_Normal]->ShaderProgram = pglCreateProgram(DrawableVs, DrawableFs, 2, const_cast<GLenum*>(uvInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_Normal]->ShaderProgram, CubismPortableGLProgramTag_Drawable);
    _shaderSets[ShaderNames_NormalMasked]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_NormalMasked]->ShaderProgram, CubismPortableGLProgramTag_DrawableMasked);
    _shaderSets[ShaderNames_NormalMaskedInverted]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedInvertedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_NormalMaskedInverted]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedInverted);
    _shaderSets[ShaderNames_NormalPremultipliedAlpha]->ShaderProgram = pglCreateProgram(DrawableVs, DrawablePremultipliedFs, 2, const_cast<GLenum*>(uvInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_NormalPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawablePremultiplied);
    _shaderSets[ShaderNames_NormalMaskedPremultipliedAlpha]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedPremultipliedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_NormalMaskedPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedPremultiplied);
    _shaderSets[ShaderNames_NormalMaskedInvertedPremultipliedAlpha]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedInvertedPremultipliedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_NormalMaskedInvertedPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied);

    _shaderSets[ShaderNames_Add]->ShaderProgram = pglCreateProgram(DrawableVs, DrawableFs, 2, const_cast<GLenum*>(uvInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_Add]->ShaderProgram, CubismPortableGLProgramTag_Drawable);
    _shaderSets[ShaderNames_AddMasked]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_AddMasked]->ShaderProgram, CubismPortableGLProgramTag_DrawableMasked);
    _shaderSets[ShaderNames_AddMaskedInverted]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedInvertedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_AddMaskedInverted]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedInverted);
    _shaderSets[ShaderNames_AddPremultipliedAlpha]->ShaderProgram = pglCreateProgram(DrawableVs, DrawablePremultipliedFs, 2, const_cast<GLenum*>(uvInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_AddPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawablePremultiplied);
    _shaderSets[ShaderNames_AddMaskedPremultipliedAlpha]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedPremultipliedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_AddMaskedPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedPremultiplied);
    _shaderSets[ShaderNames_AddMaskedInvertedPremultipliedAlpha]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedInvertedPremultipliedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_AddMaskedInvertedPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied);

    _shaderSets[ShaderNames_Mult]->ShaderProgram = pglCreateProgram(DrawableVs, DrawableFs, 2, const_cast<GLenum*>(uvInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_Mult]->ShaderProgram, CubismPortableGLProgramTag_Drawable);
    _shaderSets[ShaderNames_MultMasked]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_MultMasked]->ShaderProgram, CubismPortableGLProgramTag_DrawableMasked);
    _shaderSets[ShaderNames_MultMaskedInverted]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedInvertedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_MultMaskedInverted]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedInverted);
    _shaderSets[ShaderNames_MultPremultipliedAlpha]->ShaderProgram = pglCreateProgram(DrawableVs, DrawablePremultipliedFs, 2, const_cast<GLenum*>(uvInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_MultPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawablePremultiplied);
    _shaderSets[ShaderNames_MultMaskedPremultipliedAlpha]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedPremultipliedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_MultMaskedPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedPremultiplied);
    _shaderSets[ShaderNames_MultMaskedInvertedPremultipliedAlpha]->ShaderProgram = pglCreateProgram(MaskedDrawableVs, DrawableMaskedInvertedPremultipliedFs, 6, const_cast<GLenum*>(maskedInterpolation), GL_FALSE);
    setProgramTag(_shaderSets[ShaderNames_MultMaskedInvertedPremultipliedAlpha]->ShaderProgram, CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied);
}

void CubismShader_PortableGL::SetVertexAttributes(const CubismModel& model, csmInt32 index)
{
    const csmFloat32* vertexArray = model.GetDrawableVertices(index);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(csmFloat32) * 2, vertexArray);

    const csmFloat32* uvArray = reinterpret_cast<const csmFloat32*>(model.GetDrawableVertexUvs(index));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(csmFloat32) * 2, uvArray);
}

void CubismShader_PortableGL::SetupShaderProgramForDrawable(CubismRenderer_PortableGL* renderer, const CubismModel& model, csmInt32 index)
{
    csmInt32 srcColor = GL_ONE;
    csmInt32 dstColor = GL_ONE_MINUS_SRC_ALPHA;
    csmInt32 srcAlpha = GL_ONE;
    csmInt32 dstAlpha = GL_ONE_MINUS_SRC_ALPHA;

    const csmBool masked = renderer->GetClippingContextBufferForDrawable() != NULL;
    const csmBool invertedMask = model.GetDrawableInvertedMask(index);
    const csmBool premultipliedAlpha = renderer->IsPremultipliedAlpha();
    const csmInt32 offset = (masked ? (invertedMask ? 2 : 1) : 0) + (premultipliedAlpha ? 3 : 0);
    const csmInt32 shaderNameBegin = GetShaderNamesBegin(model.GetDrawableBlendModeType(index));
    CubismShaderSet* shaderSet = _shaderSets[shaderNameBegin + offset];

    switch (shaderNameBegin)
    {
    case ShaderNames_Add:
        srcColor = GL_ONE;
        dstColor = GL_ONE;
        srcAlpha = GL_ZERO;
        dstAlpha = GL_ONE;
        break;
    case ShaderNames_Mult:
        srcColor = GL_DST_COLOR;
        dstColor = GL_ONE_MINUS_SRC_ALPHA;
        srcAlpha = GL_ZERO;
        dstAlpha = GL_ONE;
        break;
    case ShaderNames_Normal:
    default:
        break;
    }

    glUseProgram(shaderSet->ShaderProgram);
    SetVertexAttributes(model, index);

    CubismRenderer::CubismTextureColor baseColor;
    if (model.IsBlendModeEnabled())
    {
        const csmFloat32 drawableOpacity = model.GetDrawableOpacity(index);
        baseColor.A = drawableOpacity;
        if (premultipliedAlpha)
        {
            baseColor.R = drawableOpacity;
            baseColor.G = drawableOpacity;
            baseColor.B = drawableOpacity;
        }
    }
    else
    {
        baseColor = renderer->GetModelColorWithOpacity(model.GetDrawableOpacity(index));
    }

    const CubismModelMultiplyAndScreenColor& overrideMultiplyAndScreenColor = model.GetOverrideMultiplyAndScreenColor();
    const CubismRenderer::CubismTextureColor multiplyColor = overrideMultiplyAndScreenColor.GetDrawableMultiplyColor(index);
    const CubismRenderer::CubismTextureColor screenColor = overrideMultiplyAndScreenColor.GetDrawableScreenColor(index);
    CubismMatrix44 mvpMatrix = renderer->GetMvpMatrix();

    if (masked)
    {
        s_maskedDrawableUniforms = {};
        CopyMatrix(s_maskedDrawableUniforms.Matrix, mvpMatrix.GetArray());
        CopyMatrix(s_maskedDrawableUniforms.ClipMatrix, renderer->GetClippingContextBufferForDrawable()->_matrixForDraw.GetArray());
        s_maskedDrawableUniforms.Texture0 = renderer->GetBindedTextureId(model.GetDrawableTextureIndex(index));
        s_maskedDrawableUniforms.Texture1 = renderer->GetDrawableMaskBuffer(renderer->GetClippingContextBufferForDrawable()->_bufferIndex)->GetColorBuffer();
        s_maskedDrawableUniforms.BaseColor = ToPglColor(baseColor);
        s_maskedDrawableUniforms.MultiplyColor = ToPglColor(multiplyColor);
        s_maskedDrawableUniforms.ScreenColor = ToPglColor(screenColor);
        s_maskedDrawableUniforms.ChannelFlag = ToPglColor(*renderer->GetClippingContextBufferForDrawable()->GetClippingManager()->GetChannelFlagAsColor(
            renderer->GetClippingContextBufferForDrawable()->_layoutChannelIndex));
        pglSetProgramUniform(shaderSet->ShaderProgram, &s_maskedDrawableUniforms);
    }
    else
    {
        s_drawableUniforms = {};
        CopyMatrix(s_drawableUniforms.Matrix, mvpMatrix.GetArray());
        s_drawableUniforms.Texture0 = renderer->GetBindedTextureId(model.GetDrawableTextureIndex(index));
        s_drawableUniforms.BaseColor = ToPglColor(baseColor);
        s_drawableUniforms.MultiplyColor = ToPglColor(multiplyColor);
        s_drawableUniforms.ScreenColor = ToPglColor(screenColor);
        pglSetProgramUniform(shaderSet->ShaderProgram, &s_drawableUniforms);
    }

    glBlendFuncSeparate(srcColor, dstColor, srcAlpha, dstAlpha);
}

void CubismShader_PortableGL::SetupShaderProgramForMask(CubismRenderer_PortableGL* renderer, const CubismModel& model, csmInt32 index)
{
    PGL_UNUSED(model);

    CubismShaderSet* shaderSet = _shaderSets[ShaderNames_SetupMask];
    glUseProgram(shaderSet->ShaderProgram);
    SetVertexAttributes(model, index);

    s_setupMaskUniforms = {};
    CopyMatrix(s_setupMaskUniforms.ClipMatrix, renderer->GetClippingContextBufferForMask()->_matrixForMask.GetArray());
    s_setupMaskUniforms.Texture0 = renderer->GetBindedTextureId(model.GetDrawableTextureIndex(index));
    s_setupMaskUniforms.ChannelFlag = ToPglColor(*renderer->GetClippingContextBufferForMask()->GetClippingManager()->GetChannelFlagAsColor(
        renderer->GetClippingContextBufferForMask()->_layoutChannelIndex));

    csmRectF* rect = renderer->GetClippingContextBufferForMask()->_layoutBounds;
    s_setupMaskUniforms.BaseColor = MakeVec4(
        rect->X * 2.0f - 1.0f,
        rect->Y * 2.0f - 1.0f,
        rect->GetRight() * 2.0f - 1.0f,
        rect->GetBottom() * 2.0f - 1.0f
    );
    pglSetProgramUniform(shaderSet->ShaderProgram, &s_setupMaskUniforms);

    glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
}

void CubismShader_PortableGL::CopyTexture(GLint texture, csmInt32 srcColor, csmInt32 dstColor, csmInt32 srcAlpha, csmInt32 dstAlpha, CubismRenderer::CubismTextureColor baseColor)
{
    CubismShaderSet* shaderSet = _shaderSets[ShaderNames_Copy];
    glUseProgram(shaderSet->ShaderProgram);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(csmFloat32) * 2, renderTargetVertexArray);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(csmFloat32) * 2, renderTargetUvArray);

    s_copyUniforms = {};
    s_copyUniforms.Texture0 = texture;
    s_copyUniforms.BaseColor = ToPglColor(baseColor);
    pglSetProgramUniform(shaderSet->ShaderProgram, &s_copyUniforms);

    glBlendFuncSeparate(srcColor, dstColor, srcAlpha, dstAlpha);
}

void CubismShader_PortableGL::SetupShaderProgramForOffscreenRenderTarget(CubismRenderer_PortableGL* renderer)
{
    const CubismRenderTarget_PortableGL* texture = renderer->CopyOffscreenRenderTarget();
    if (texture == NULL)
    {
        return;
    }

    CubismRenderer::CubismTextureColor baseColor = renderer->GetModelColor();
    baseColor.R *= baseColor.A;
    baseColor.G *= baseColor.A;
    baseColor.B *= baseColor.A;
    CopyTexture(texture->GetColorBuffer(), GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, baseColor);
}

void CubismShader_PortableGL::SetupShaderProgramForOffscreen(CubismRenderer_PortableGL* renderer, const CubismModel& model, const CubismOffscreenRenderTarget_PortableGL* offscreen)
{
    csmInt32 srcColor = GL_ONE;
    csmInt32 dstColor = GL_ONE_MINUS_SRC_ALPHA;
    csmInt32 srcAlpha = GL_ONE;
    csmInt32 dstAlpha = GL_ONE_MINUS_SRC_ALPHA;

    const csmInt32 offscreenIndex = offscreen->GetOffscreenIndex();
    const csmBool masked = renderer->GetClippingContextBufferForOffscreen() != NULL;
    const csmBool invertedMask = model.GetOffscreenInvertedMask(offscreenIndex);
    const csmInt32 offset = (masked ? (invertedMask ? 2 : 1) : 0) + 3;
    const csmInt32 shaderNameBegin = GetShaderNamesBegin(model.GetOffscreenBlendModeType(offscreenIndex));
    CubismShaderSet* shaderSet = _shaderSets[shaderNameBegin + offset];

    switch (shaderNameBegin)
    {
    case ShaderNames_Add:
        srcColor = GL_ONE;
        dstColor = GL_ONE;
        srcAlpha = GL_ZERO;
        dstAlpha = GL_ONE;
        break;
    case ShaderNames_Mult:
        srcColor = GL_DST_COLOR;
        dstColor = GL_ONE_MINUS_SRC_ALPHA;
        srcAlpha = GL_ZERO;
        dstAlpha = GL_ONE;
        break;
    case ShaderNames_Normal:
    default:
        break;
    }

    glUseProgram(shaderSet->ShaderProgram);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(csmFloat32) * 2, renderTargetVertexArray);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(csmFloat32) * 2, renderTargetReverseUvArray);

    const csmFloat32 offscreenOpacity = model.GetOffscreenOpacity(offscreenIndex);
    const CubismRenderer::CubismTextureColor baseColor(offscreenOpacity, offscreenOpacity, offscreenOpacity, offscreenOpacity);
    const CubismModelMultiplyAndScreenColor& overrideMultiplyAndScreenColor = model.GetOverrideMultiplyAndScreenColor();
    const CubismRenderer::CubismTextureColor multiplyColor = overrideMultiplyAndScreenColor.GetOffscreenMultiplyColor(offscreenIndex);
    const CubismRenderer::CubismTextureColor screenColor = overrideMultiplyAndScreenColor.GetOffscreenScreenColor(offscreenIndex);

    if (masked)
    {
        s_maskedDrawableUniforms = {};
        CubismMatrix44 identityMatrix;
        identityMatrix.LoadIdentity();
        CopyMatrix(s_maskedDrawableUniforms.Matrix, identityMatrix.GetArray());
        CopyMatrix(s_maskedDrawableUniforms.ClipMatrix, renderer->GetClippingContextBufferForOffscreen()->_matrixForDraw.GetArray());
        s_maskedDrawableUniforms.Texture0 = offscreen->GetRenderTarget()->GetColorBuffer();
        s_maskedDrawableUniforms.Texture1 = renderer->GetOffscreenMaskBuffer(renderer->GetClippingContextBufferForOffscreen()->_bufferIndex)->GetColorBuffer();
        s_maskedDrawableUniforms.BaseColor = ToPglColor(baseColor);
        s_maskedDrawableUniforms.MultiplyColor = ToPglColor(multiplyColor);
        s_maskedDrawableUniforms.ScreenColor = ToPglColor(screenColor);
        s_maskedDrawableUniforms.ChannelFlag = ToPglColor(*renderer->GetClippingContextBufferForOffscreen()->GetClippingManager()->GetChannelFlagAsColor(
            renderer->GetClippingContextBufferForOffscreen()->_layoutChannelIndex));
        pglSetProgramUniform(shaderSet->ShaderProgram, &s_maskedDrawableUniforms);
    }
    else
    {
        s_drawableUniforms = {};
        CubismMatrix44 identityMatrix;
        identityMatrix.LoadIdentity();
        CopyMatrix(s_drawableUniforms.Matrix, identityMatrix.GetArray());
        s_drawableUniforms.Texture0 = offscreen->GetRenderTarget()->GetColorBuffer();
        s_drawableUniforms.BaseColor = ToPglColor(baseColor);
        s_drawableUniforms.MultiplyColor = ToPglColor(multiplyColor);
        s_drawableUniforms.ScreenColor = ToPglColor(screenColor);
        pglSetProgramUniform(shaderSet->ShaderProgram, &s_drawableUniforms);
    }

    glBlendFuncSeparate(srcColor, dstColor, srcAlpha, dstAlpha);
}

}}}}
