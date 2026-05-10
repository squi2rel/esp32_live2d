#pragma once

#include "CubismPortableGL.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

enum CubismPortableGLProgramTag : GLint
{
    CubismPortableGLProgramTag_None = 0,
    CubismPortableGLProgramTag_Copy = 1,
    CubismPortableGLProgramTag_SetupMask = 2,
    CubismPortableGLProgramTag_Drawable = 3,
    CubismPortableGLProgramTag_DrawableMasked = 4,
    CubismPortableGLProgramTag_DrawableMaskedInverted = 5,
    CubismPortableGLProgramTag_DrawablePremultiplied = 6,
    CubismPortableGLProgramTag_DrawableMaskedPremultiplied = 7,
    CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied = 8,
};

struct CubismPortableGLCopyUniforms
{
    GLuint Texture0;
    pgl_vec4 BaseColor;
};

struct CubismPortableGLSetupMaskUniforms
{
    pgl_mat4 ClipMatrix;
    GLuint Texture0;
    pgl_vec4 ChannelFlag;
    pgl_vec4 BaseColor;
};

struct CubismPortableGLDrawableUniforms
{
    pgl_mat4 Matrix;
    GLuint Texture0;
    pgl_vec4 BaseColor;
    pgl_vec4 MultiplyColor;
    pgl_vec4 ScreenColor;
};

struct CubismPortableGLMaskedDrawableUniforms
{
    pgl_mat4 Matrix;
    pgl_mat4 ClipMatrix;
    GLuint Texture0;
    GLuint Texture1;
    pgl_vec4 ChannelFlag;
    pgl_vec4 BaseColor;
    pgl_vec4 MultiplyColor;
    pgl_vec4 ScreenColor;
};

}}}}
