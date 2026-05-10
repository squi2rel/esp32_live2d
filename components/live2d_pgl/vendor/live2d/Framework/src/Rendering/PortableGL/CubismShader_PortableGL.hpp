/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include "../CubismRenderer.hpp"
#include "../csmBlendMode.hpp"
#include "CubismFramework.hpp"
#include "CubismPortableGL.hpp"
#include "Type/csmVector.hpp"

namespace Live2D { namespace Cubism { namespace Framework {

class CubismModel;

namespace Rendering {

class CubismRenderer_PortableGL;
class CubismOffscreenRenderTarget_PortableGL;

class CubismShader_PortableGL
{
public:
    static CubismShader_PortableGL* GetInstance();
    static void DeleteInstance();

    void ReleaseInvalidShaderProgram();

    void SetupShaderProgramForDrawable(CubismRenderer_PortableGL* renderer, const CubismModel& model, csmInt32 index);
    void SetupShaderProgramForMask(CubismRenderer_PortableGL* renderer, const CubismModel& model, csmInt32 index);
    void SetupShaderProgramForOffscreenRenderTarget(CubismRenderer_PortableGL* renderer);
    void SetupShaderProgramForOffscreen(CubismRenderer_PortableGL* renderer, const CubismModel& model, const CubismOffscreenRenderTarget_PortableGL* offscreen);

    void CopyTexture(
        GLint texture,
        csmInt32 srcColor = GL_ONE,
        csmInt32 dstColor = GL_ZERO,
        csmInt32 srcAlpha = GL_ONE,
        csmInt32 dstAlpha = GL_ZERO,
        CubismRenderer::CubismTextureColor baseColor = CubismRenderer::CubismTextureColor()
    );

private:
    enum ShaderNames
    {
        ShaderNames_Copy,
        ShaderNames_SetupMask,

        ShaderNames_Normal,
        ShaderNames_NormalMasked,
        ShaderNames_NormalMaskedInverted,
        ShaderNames_NormalPremultipliedAlpha,
        ShaderNames_NormalMaskedPremultipliedAlpha,
        ShaderNames_NormalMaskedInvertedPremultipliedAlpha,

        ShaderNames_Add,
        ShaderNames_AddMasked,
        ShaderNames_AddMaskedInverted,
        ShaderNames_AddPremultipliedAlpha,
        ShaderNames_AddMaskedPremultipliedAlpha,
        ShaderNames_AddMaskedInvertedPremultipliedAlpha,

        ShaderNames_Mult,
        ShaderNames_MultMasked,
        ShaderNames_MultMaskedInverted,
        ShaderNames_MultPremultipliedAlpha,
        ShaderNames_MultMaskedPremultipliedAlpha,
        ShaderNames_MultMaskedInvertedPremultipliedAlpha,

        ShaderNames_ShaderCount
    };

    struct CubismShaderSet
    {
        GLuint ShaderProgram;
    };

    static csmInt32 GetShaderNamesBegin(const csmBlendMode blendMode);

    CubismShader_PortableGL();
    ~CubismShader_PortableGL();

    void ReleaseShaderProgram();
    void GenerateShaders();

    void SetVertexAttributes(const CubismModel& model, csmInt32 index);

    csmVector<CubismShaderSet*> _shaderSets;
};

}}}}
