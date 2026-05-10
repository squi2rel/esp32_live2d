#define PORTABLEGL_IMPLEMENTATION
#include "CubismPortableGL.hpp"
#include "CubismPortableGLFastPath.hpp"

#if defined(CSM_TARGET_ESP_PGL)
#include <esp_timer.h>
#include <esp_heap_caps.h>
#endif

namespace {

using Live2D::Cubism::Framework::Rendering::CubismPortableGLCopyUniforms;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLDrawableUniforms;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLMaskedDrawableUniforms;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag_Copy;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag_Drawable;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag_DrawableMasked;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag_DrawableMaskedInverted;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag_DrawableMaskedPremultiplied;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag_DrawablePremultiplied;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLProgramTag_SetupMask;
using Live2D::Cubism::Framework::Rendering::CubismPortableGLSetupMaskUniforms;

static inline bool CubismPortableGLIsSupportedTag(GLint tag)
{
    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_Copy:
    case CubismPortableGLProgramTag_SetupMask:
    case CubismPortableGLProgramTag_Drawable:
    case CubismPortableGLProgramTag_DrawableMasked:
    case CubismPortableGLProgramTag_DrawableMaskedInverted:
    case CubismPortableGLProgramTag_DrawablePremultiplied:
    case CubismPortableGLProgramTag_DrawableMaskedPremultiplied:
    case CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied:
        return true;
    default:
        return false;
    }
}

static inline bool CubismPortableGLIsMaskedTag(GLint tag)
{
    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_DrawableMasked:
    case CubismPortableGLProgramTag_DrawableMaskedInverted:
    case CubismPortableGLProgramTag_DrawableMaskedPremultiplied:
    case CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied:
        return true;
    default:
        return false;
    }
}

static inline bool CubismPortableGLIsPremultipliedTag(GLint tag)
{
    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_DrawablePremultiplied:
    case CubismPortableGLProgramTag_DrawableMaskedPremultiplied:
    case CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied:
        return true;
    default:
        return false;
    }
}

static inline bool CubismPortableGLIsInvertedMaskTag(GLint tag)
{
    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_DrawableMaskedInverted:
    case CubismPortableGLProgramTag_DrawableMaskedInvertedPremultiplied:
        return true;
    default:
        return false;
    }
}

struct CubismPortableGLLaneColor4
{
    pgl_simd_float4 r;
    pgl_simd_float4 g;
    pgl_simd_float4 b;
    pgl_simd_float4 a;
};

struct CubismPortableGLPackedColor4
{
    pgl_Color lane[4];
};

enum CubismPortableGLFastBlendKind
{
    CubismPortableGLFastBlendKind_Unsupported = 0,
    CubismPortableGLFastBlendKind_Opaque,
    CubismPortableGLFastBlendKind_Normal,
    CubismPortableGLFastBlendKind_Add,
    CubismPortableGLFastBlendKind_Multiply,
    CubismPortableGLFastBlendKind_SetupMask,
};

#if defined(__XTENSA__)
extern "C" int pgl_qacc_bilinear_u16x8(const uint16_t* texels, const uint16_t* weights, const uint16_t* bias, unsigned shift, uint16_t* out);
#endif

static inline glTexture* CubismPortableGLResolveTexture(GLuint texture)
{
    if (texture != 0) {
        return &c->textures.a[texture];
    }
    return &c->default_textures[GL_TEXTURE_2D - GL_TEXTURE_1D];
}

static inline pgl_simd_float4 CubismPortableGLLaneSet1(float value)
{
    pgl_simd_float4 out = {{value, value, value, value}};
    return out;
}

static inline pgl_simd_float4 CubismPortableGLLaneAdd(pgl_simd_float4 lhs, pgl_simd_float4 rhs)
{
    pgl_simd_float4 out = {{0}};
#if defined(__XTENSA__)
    pgl_lane_add4_f32(lhs.lane, rhs.lane, out.lane);
#else
    simd_add_f32(lhs.lane, rhs.lane, out.lane, 4);
#endif
    return out;
}

static inline pgl_simd_float4 CubismPortableGLLaneSub(pgl_simd_float4 lhs, pgl_simd_float4 rhs)
{
    pgl_simd_float4 out = {{0}};
#if defined(__XTENSA__)
    pgl_lane_sub4_f32(lhs.lane, rhs.lane, out.lane);
#else
    simd_sub_f32(lhs.lane, rhs.lane, out.lane, 4);
#endif
    return out;
}

static inline pgl_simd_float4 CubismPortableGLLaneMul(pgl_simd_float4 lhs, pgl_simd_float4 rhs)
{
    pgl_simd_float4 out = {{0}};
#if defined(__XTENSA__)
    pgl_lane_mul4_f32(lhs.lane, rhs.lane, out.lane);
#else
    simd_mul_shift_f32(lhs.lane, rhs.lane, out.lane, 0, 4);
#endif
    return out;
}

static inline pgl_simd_float4 CubismPortableGLLaneScale(pgl_simd_float4 value, float scalar)
{
    pgl_simd_float4 out = {{0}};
#if defined(__XTENSA__)
    pgl_lane_mul_scalar4_f32(value.lane, &scalar, out.lane);
#else
    simd_mul_scalar_f32(value.lane, &scalar, out.lane, 4);
#endif
    return out;
}

static inline pgl_simd_float4 CubismPortableGLLaneReciprocal(pgl_simd_float4 value)
{
    pgl_simd_float4 out = {{0}};
    for (int lane = 0; lane < 4; ++lane) {
        out.lane[lane] = value.lane[lane] != 0.0f ? (1.0f / value.lane[lane]) : 0.0f;
    }
    return out;
}

static inline pgl_simd_float4 CubismPortableGLLaneLerp(pgl_simd_float4 a, pgl_simd_float4 b, pgl_simd_float4 t)
{
    pgl_simd_float4 out = {{0}};
    const pgl_simd_float4 inv = CubismPortableGLLaneSub(CubismPortableGLLaneSet1(1.0f), t);
#if defined(__XTENSA__)
    pgl_lane_lerp4_f32(a.lane, b.lane, inv.lane, t.lane, out.lane);
#else
    out = CubismPortableGLLaneAdd(CubismPortableGLLaneMul(a, inv), CubismPortableGLLaneMul(b, t));
#endif
    return out;
}

static inline pgl_simd_float4 CubismPortableGLMix3Scalar4(pgl_simd_float4 w0, float s0, pgl_simd_float4 w1, float s1, pgl_simd_float4 w2, float s2)
{
    return CubismPortableGLLaneAdd(
        CubismPortableGLLaneAdd(CubismPortableGLLaneScale(w0, s0), CubismPortableGLLaneScale(w1, s1)),
        CubismPortableGLLaneScale(w2, s2)
    );
}

static inline uint16_t CubismPortableGLFixedWeight15(float value)
{
    float scaled = value * 32768.0f + 0.5f;
    if (scaled <= 0.0f) {
        return 0;
    }
    if (scaled >= 32768.0f) {
        return 32768;
    }
    return static_cast<uint16_t>(scaled);
}

static inline bool CubismPortableGLIsPow2(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

static inline void CubismPortableGLResolveLinearCoord(
    float uv,
    float sizeMinusEpsilon,
    int size,
    GLenum wrapMode,
    int* outI0,
    int* outI1,
    uint16_t* outFrac15)
{
    const float sample = uv * sizeMinusEpsilon + 0.5f;
    const float basef = floorf(sample);
    const int base = static_cast<int>(basef);
    float frac = sample - basef;

#ifdef PGL_HERMITE_SMOOTHING
    frac = frac * frac * (3.0f - 2.0f * frac);
#endif

    *outFrac15 = CubismPortableGLFixedWeight15(frac);

    if (wrapMode == GL_REPEAT && CubismPortableGLIsPow2(size)) {
        const int mask = size - 1;
        *outI0 = (base - 1) & mask;
        *outI1 = base & mask;
        return;
    }

    *outI0 = pgl_live2d_wrap(base - 1, size, wrapMode);
    *outI1 = pgl_live2d_wrap(base, size, wrapMode);
}

static inline bool CubismPortableGLCanUseFastTexture(GLuint texture);

#if defined(__XTENSA__)
static inline bool CubismPortableGLSampleTexture4LinearQaccPacked(glTexture* t, const pgl_simd_float4& u, const pgl_simd_float4& v, pgl_Color out[4])
{
    alignas(16) static const uint16_t kRoundBias15[8] = {
        1u << 14, 1u << 14, 1u << 14, 1u << 14,
        1u << 14, 1u << 14, 1u << 14, 1u << 14,
    };
    pgl_Color* texData = reinterpret_cast<pgl_Color*>(t->data);
    const int w = t->w;
    const int h = t->h;
    const float dw = w - 0.000001f;
    const float dh = h - 0.000001f;
    constexpr unsigned kShift = 15;
    alignas(16) uint16_t texelsLo[32];
    alignas(16) uint16_t texelsHi[32];
    alignas(16) uint16_t weightsLo[32];
    alignas(16) uint16_t weightsHi[32];
    alignas(16) uint16_t result16[16];

    for (int lane = 0; lane < 4; ++lane) {
        int i0 = 0;
        int i1 = 0;
        int j0 = 0;
        int j1 = 0;
        uint16_t a = 0;
        uint16_t b = 0;
        CubismPortableGLResolveLinearCoord(u.lane[lane], dw, w, t->wrap_s, &i0, &i1, &a);
        CubismPortableGLResolveLinearCoord(v.lane[lane], dh, h, t->wrap_t, &j0, &j1, &b);
        const uint16_t ia = static_cast<uint16_t>(32768u - a);
        const uint16_t ib = static_cast<uint16_t>(32768u - b);

        const pgl_Color c00 = texData[j0 * w + i0];
        const pgl_Color c10 = texData[j0 * w + i1];
        const pgl_Color c01 = texData[j1 * w + i0];
        const pgl_Color c11 = texData[j1 * w + i1];

        const int base = (lane & 1) * 4;
        uint16_t* texels = (lane < 2) ? texelsLo : texelsHi;
        uint16_t* weights = (lane < 2) ? weightsLo : weightsHi;

        texels[0 + base] = c00.r;
        texels[1 + base] = c00.g;
        texels[2 + base] = c00.b;
        texels[3 + base] = c00.a;
        texels[8 + base] = c10.r;
        texels[9 + base] = c10.g;
        texels[10 + base] = c10.b;
        texels[11 + base] = c10.a;
        texels[16 + base] = c01.r;
        texels[17 + base] = c01.g;
        texels[18 + base] = c01.b;
        texels[19 + base] = c01.a;
        texels[24 + base] = c11.r;
        texels[25 + base] = c11.g;
        texels[26 + base] = c11.b;
        texels[27 + base] = c11.a;

        for (int ch = 0; ch < 4; ++ch) {
            weights[0 + base + ch] = ia;
            weights[8 + base + ch] = a;
            weights[16 + base + ch] = ib;
            weights[24 + base + ch] = b;
        }
    }

    pgl_qacc_bilinear_u16x8(texelsLo, weightsLo, kRoundBias15, kShift, result16);
    pgl_qacc_bilinear_u16x8(texelsHi, weightsHi, kRoundBias15, kShift, result16 + 8);

    for (int lane = 0; lane < 4; ++lane) {
        const int base = lane * 4;
        out[lane] = make_Color(
            static_cast<uint8_t>(result16[base + 0] > 255u ? 255u : result16[base + 0]),
            static_cast<uint8_t>(result16[base + 1] > 255u ? 255u : result16[base + 1]),
            static_cast<uint8_t>(result16[base + 2] > 255u ? 255u : result16[base + 2]),
            static_cast<uint8_t>(result16[base + 3] > 255u ? 255u : result16[base + 3]));
    }

    return true;
}
#endif

static inline bool CubismPortableGLSampleTexture4Packed(GLuint texture, const pgl_simd_float4& u, const pgl_simd_float4& v, pgl_Color out[4])
{
#if defined(CSM_TARGET_ESP_PGL)
    const uint64_t sampleStartUs = pgl_cubism_perf_begin_timing();
    ++pgl_cubism_perf_current.SampleCallCount;
#endif
    glTexture* t = CubismPortableGLResolveTexture(texture);
    if (t == NULL || t->data == NULL || out == NULL) {
#if defined(CSM_TARGET_ESP_PGL)
        pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleSampleMilliseconds, sampleStartUs);
#endif
        return false;
    }

    if (!CubismPortableGLCanUseFastTexture(texture)) {
#if defined(CSM_TARGET_ESP_PGL)
        pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleSampleMilliseconds, sampleStartUs);
#endif
        return false;
    }

    pgl_Color* texData = reinterpret_cast<pgl_Color*>(t->data);
    const int w = t->w;
    const int h = t->h;
    const float dw = w - 0.000001f;
    const float dh = h - 0.000001f;

    if (t->mag_filter == GL_NEAREST && t->min_filter == GL_NEAREST) {
        for (int lane = 0; lane < 4; ++lane) {
            const float xw = u.lane[lane] * dw;
            const float yh = v.lane[lane] * dh;
            const int i0 = pgl_live2d_wrap(floorf(xw), w, t->wrap_s);
            const int j0 = pgl_live2d_wrap(floorf(yh), h, t->wrap_t);
            out[lane] = texData[j0 * w + i0];
        }
#if defined(CSM_TARGET_ESP_PGL)
        pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleSampleMilliseconds, sampleStartUs);
#endif
        return true;
    }

#if defined(__XTENSA__)
    {
        const bool ok = CubismPortableGLSampleTexture4LinearQaccPacked(t, u, v, out);
#if defined(CSM_TARGET_ESP_PGL)
        pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleSampleMilliseconds, sampleStartUs);
#endif
        return ok;
    }
#else
    for (int lane = 0; lane < 4; ++lane) {
        int i0 = 0;
        int i1 = 0;
        int j0 = 0;
        int j1 = 0;
        uint16_t a = 0;
        uint16_t b = 0;
        CubismPortableGLResolveLinearCoord(u.lane[lane], dw, w, t->wrap_s, &i0, &i1, &a);
        CubismPortableGLResolveLinearCoord(v.lane[lane], dh, h, t->wrap_t, &j0, &j1, &b);
        const pgl_Color c00 = texData[j0 * w + i0];
        const pgl_Color c10 = texData[j0 * w + i1];
        const pgl_Color c01 = texData[j1 * w + i0];
        const pgl_Color c11 = texData[j1 * w + i1];
        out[lane] = make_Color(
            CubismPortableGLBilinearByte(c00.r, c10.r, c01.r, c11.r, a, b),
            CubismPortableGLBilinearByte(c00.g, c10.g, c01.g, c11.g, a, b),
            CubismPortableGLBilinearByte(c00.b, c10.b, c01.b, c11.b, a, b),
            CubismPortableGLBilinearByte(c00.a, c10.a, c01.a, c11.a, a, b));
    }
#if defined(CSM_TARGET_ESP_PGL)
    pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleSampleMilliseconds, sampleStartUs);
#endif
    return true;
#endif
}

static inline void CubismPortableGLLaneClamp01(pgl_simd_float4* value)
{
    for (int lane = 0; lane < 4; ++lane) {
        if (value->lane[lane] < 0.0f) {
            value->lane[lane] = 0.0f;
        } else if (value->lane[lane] > 1.0f) {
            value->lane[lane] = 1.0f;
        }
    }
}

static inline void CubismPortableGLStoreColorLane(CubismPortableGLLaneColor4* out, int lane, pgl_Color color)
{
    out->r.lane[lane] = static_cast<float>(color.r) / 255.0f;
    out->g.lane[lane] = static_cast<float>(color.g) / 255.0f;
    out->b.lane[lane] = static_cast<float>(color.b) / 255.0f;
    out->a.lane[lane] = static_cast<float>(color.a) / 255.0f;
}

static inline uint8_t CubismPortableGLFloatToByte(float value)
{
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return 255;
    }
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

static inline uint8_t CubismPortableGLMulByte(uint8_t lhs, uint8_t rhs)
{
    return static_cast<uint8_t>((static_cast<unsigned>(lhs) * static_cast<unsigned>(rhs) + 127u) / 255u);
}

static inline uint8_t CubismPortableGLClampByte(unsigned value)
{
    return static_cast<uint8_t>(value > 255u ? 255u : value);
}

static inline uint8_t CubismPortableGLClampSignedByte(int value)
{
    if (value <= 0) {
        return 0;
    }
    return static_cast<uint8_t>(value >= 255 ? 255 : value);
}

static inline int CubismPortableGLResolveChannelIndex(const pgl_vec4& channelFlag)
{
    if (channelFlag.x >= 0.5f) {
        return 0;
    }
    if (channelFlag.y >= 0.5f) {
        return 1;
    }
    if (channelFlag.z >= 0.5f) {
        return 2;
    }
    return 3;
}

static inline uint8_t CubismPortableGLColorChannel(const pgl_Color& color, int channelIndex)
{
    switch (channelIndex) {
    case 0:
        return color.r;
    case 1:
        return color.g;
    case 2:
        return color.b;
    default:
        return color.a;
    }
}

static inline uint8_t CubismPortableGLBilinearByte(uint8_t c00, uint8_t c10, uint8_t c01, uint8_t c11, uint16_t a, uint16_t b)
{
    const unsigned ia = 32768u - a;
    const unsigned ib = 32768u - b;
    const unsigned row0 = (static_cast<unsigned>(c00) * ia + static_cast<unsigned>(c10) * a + (1u << 14)) >> 15;
    const unsigned row1 = (static_cast<unsigned>(c01) * ia + static_cast<unsigned>(c11) * a + (1u << 14)) >> 15;
    return static_cast<uint8_t>((row0 * ib + row1 * b + (1u << 14)) >> 15);
}

static inline void CubismPortableGLColorToPackedColors(const CubismPortableGLLaneColor4& value, unsigned laneMask, pgl_Color outColors[4])
{
    for (int lane = 0; lane < 4; ++lane) {
        if ((laneMask & (1u << lane)) == 0u) {
            continue;
        }

        outColors[lane] = make_Color(
            CubismPortableGLFloatToByte(value.r.lane[lane]),
            CubismPortableGLFloatToByte(value.g.lane[lane]),
            CubismPortableGLFloatToByte(value.b.lane[lane]),
            CubismPortableGLFloatToByte(value.a.lane[lane]));
    }
}

static inline CubismPortableGLLaneColor4 CubismPortableGLColorScaleConst(CubismPortableGLLaneColor4 value, const pgl_vec4& scalar)
{
    value.r = CubismPortableGLLaneScale(value.r, scalar.x);
    value.g = CubismPortableGLLaneScale(value.g, scalar.y);
    value.b = CubismPortableGLLaneScale(value.b, scalar.z);
    value.a = CubismPortableGLLaneScale(value.a, scalar.w);
    return value;
}

static inline CubismPortableGLLaneColor4 CubismPortableGLColorScaleLane(CubismPortableGLLaneColor4 value, pgl_simd_float4 scalar)
{
    value.r = CubismPortableGLLaneMul(value.r, scalar);
    value.g = CubismPortableGLLaneMul(value.g, scalar);
    value.b = CubismPortableGLLaneMul(value.b, scalar);
    value.a = CubismPortableGLLaneMul(value.a, scalar);
    return value;
}

static inline bool CubismPortableGLEvaluateDrawablePacked4(
    GLuint texture,
    const pgl_simd_float4& u,
    const pgl_simd_float4& v,
    const pgl_vec4& baseColor,
    const pgl_vec4& multiplyColor,
    const pgl_vec4& screenColor,
    bool premultipliedAlpha,
    pgl_Color out[4])
{
    pgl_Color texels[4] = {};
    if (!CubismPortableGLSampleTexture4Packed(texture, u, v, texels)) {
        return false;
    }

    const uint8_t baseR = CubismPortableGLFloatToByte(baseColor.x);
    const uint8_t baseG = CubismPortableGLFloatToByte(baseColor.y);
    const uint8_t baseB = CubismPortableGLFloatToByte(baseColor.z);
    const uint8_t baseA = CubismPortableGLFloatToByte(baseColor.w);
    const uint8_t mulR = CubismPortableGLFloatToByte(multiplyColor.x);
    const uint8_t mulG = CubismPortableGLFloatToByte(multiplyColor.y);
    const uint8_t mulB = CubismPortableGLFloatToByte(multiplyColor.z);
    const uint8_t screenR = CubismPortableGLFloatToByte(screenColor.x);
    const uint8_t screenG = CubismPortableGLFloatToByte(screenColor.y);
    const uint8_t screenB = CubismPortableGLFloatToByte(screenColor.z);

    for (int lane = 0; lane < 4; ++lane) {
        int r = CubismPortableGLMulByte(texels[lane].r, mulR);
        int g = CubismPortableGLMulByte(texels[lane].g, mulG);
        int b = CubismPortableGLMulByte(texels[lane].b, mulB);
        const uint8_t a = texels[lane].a;

        if (premultipliedAlpha) {
            r = r + CubismPortableGLMulByte(screenR, a) - CubismPortableGLMulByte(static_cast<uint8_t>(r), screenR);
            g = g + CubismPortableGLMulByte(screenG, a) - CubismPortableGLMulByte(static_cast<uint8_t>(g), screenG);
            b = b + CubismPortableGLMulByte(screenB, a) - CubismPortableGLMulByte(static_cast<uint8_t>(b), screenB);
        } else {
            r = r + screenR - CubismPortableGLMulByte(static_cast<uint8_t>(r), screenR);
            g = g + screenG - CubismPortableGLMulByte(static_cast<uint8_t>(g), screenG);
            b = b + screenB - CubismPortableGLMulByte(static_cast<uint8_t>(b), screenB);
        }

        out[lane] = make_Color(
            CubismPortableGLMulByte(CubismPortableGLClampSignedByte(r), baseR),
            CubismPortableGLMulByte(CubismPortableGLClampSignedByte(g), baseG),
            CubismPortableGLMulByte(CubismPortableGLClampSignedByte(b), baseB),
            CubismPortableGLMulByte(a, baseA));

        if (!premultipliedAlpha) {
            out[lane].r = CubismPortableGLMulByte(out[lane].r, out[lane].a);
            out[lane].g = CubismPortableGLMulByte(out[lane].g, out[lane].a);
            out[lane].b = CubismPortableGLMulByte(out[lane].b, out[lane].a);
        }
    }
    return true;
}

static inline bool CubismPortableGLEvaluateMaskPacked4(
    GLuint texture,
    const pgl_vec4& channelFlag,
    const pgl_simd_float4& u,
    const pgl_simd_float4& v,
    uint8_t out[4])
{
    pgl_Color texels[4] = {};
    if (!CubismPortableGLSampleTexture4Packed(texture, u, v, texels)) {
        return false;
    }
    const int channelIndex = CubismPortableGLResolveChannelIndex(channelFlag);
    for (int lane = 0; lane < 4; ++lane) {
        out[lane] = static_cast<uint8_t>(255u - CubismPortableGLColorChannel(texels[lane], channelIndex));
    }
    return true;
}

static inline bool CubismPortableGLCanUseFastTexture(GLuint texture)
{
    const glTexture* t = CubismPortableGLResolveTexture(texture);
    if (t == NULL || t->data == NULL) {
        return false;
    }

    if (t->wrap_s != GL_REPEAT || t->wrap_t != GL_REPEAT) {
        return false;
    }

    if (t->mag_filter == GL_NEAREST && t->min_filter == GL_NEAREST) {
        return true;
    }

    return t->mag_filter == GL_LINEAR && t->min_filter == GL_LINEAR;
}

static inline bool CubismPortableGLCanUseFastBlendState()
{
    if (!c->blend) {
        return true;
    }

    if (c->blend_eqRGB != GL_FUNC_ADD || c->blend_eqA != GL_FUNC_ADD) {
        return false;
    }

    if (c->blend_sRGB == GL_ONE && c->blend_dRGB == GL_ONE_MINUS_SRC_ALPHA &&
        c->blend_sA == GL_ONE && c->blend_dA == GL_ONE_MINUS_SRC_ALPHA) {
        return true;
    }

    if (c->blend_sRGB == GL_ONE && c->blend_dRGB == GL_ONE &&
        c->blend_sA == GL_ZERO && c->blend_dA == GL_ONE) {
        return true;
    }

    if (c->blend_sRGB == GL_DST_COLOR && c->blend_dRGB == GL_ONE_MINUS_SRC_ALPHA &&
        c->blend_sA == GL_ZERO && c->blend_dA == GL_ONE) {
        return true;
    }

    if (c->blend_sRGB == GL_ZERO && c->blend_dRGB == GL_ONE_MINUS_SRC_COLOR &&
        c->blend_sA == GL_ZERO && c->blend_dA == GL_ONE_MINUS_SRC_ALPHA) {
        return true;
    }

    return false;
}

static inline CubismPortableGLFastBlendKind CubismPortableGLResolveFastBlendKind()
{
    if (!c->blend) {
        return CubismPortableGLFastBlendKind_Opaque;
    }

    if (c->blend_eqRGB != GL_FUNC_ADD || c->blend_eqA != GL_FUNC_ADD) {
        return CubismPortableGLFastBlendKind_Unsupported;
    }

    if (c->blend_sRGB == GL_ONE && c->blend_dRGB == GL_ONE_MINUS_SRC_ALPHA &&
        c->blend_sA == GL_ONE && c->blend_dA == GL_ONE_MINUS_SRC_ALPHA) {
        return CubismPortableGLFastBlendKind_Normal;
    }

    if (c->blend_sRGB == GL_ONE && c->blend_dRGB == GL_ONE &&
        c->blend_sA == GL_ZERO && c->blend_dA == GL_ONE) {
        return CubismPortableGLFastBlendKind_Add;
    }

    if (c->blend_sRGB == GL_DST_COLOR && c->blend_dRGB == GL_ONE_MINUS_SRC_ALPHA &&
        c->blend_sA == GL_ZERO && c->blend_dA == GL_ONE) {
        return CubismPortableGLFastBlendKind_Multiply;
    }

    if (c->blend_sRGB == GL_ZERO && c->blend_dRGB == GL_ONE_MINUS_SRC_COLOR &&
        c->blend_sA == GL_ZERO && c->blend_dA == GL_ONE_MINUS_SRC_ALPHA) {
        return CubismPortableGLFastBlendKind_SetupMask;
    }

    return CubismPortableGLFastBlendKind_Unsupported;
}

static inline bool CubismPortableGLSampleTexture4(GLuint texture, const pgl_simd_float4& u, const pgl_simd_float4& v, CubismPortableGLLaneColor4* out)
{
    if (out == NULL) {
        return false;
    }

    out->r = {{0}};
    out->g = {{0}};
    out->b = {{0}};
    out->a = {{0}};
    pgl_Color packed[4] = {};
    if (!CubismPortableGLSampleTexture4Packed(texture, u, v, packed)) {
        return false;
    }
    for (int lane = 0; lane < 4; ++lane) {
        CubismPortableGLStoreColorLane(out, lane, packed[lane]);
    }
    return true;
}

static inline pgl_vec4 CubismPortableGLSimdApplyMultiplyAndScreen(pgl_vec4 texColor, const pgl_vec4& multiplyColor, const pgl_vec4& screenColor, bool premultipliedAlpha)
{
    pgl_vec4 multiply = multiplyColor;
    multiply.w = 1.0f;

    pgl_vec4 screen = screenColor;
    screen.w = 0.0f;

    texColor = pgl_simd_mul_vec4(texColor, multiply);
    const pgl_vec4 screenTerm = premultipliedAlpha ? pgl_simd_scale_vec4(screen, texColor.w) : screen;
    return pgl_simd_sub_vec4(
        pgl_simd_add_vec4(texColor, screenTerm),
        pgl_simd_mul_vec4(texColor, screen)
    );
}

static inline pgl_vec4 CubismPortableGLEvaluateDrawable(GLuint texture, float u, float v, const pgl_vec4& baseColor, const pgl_vec4& multiplyColor, const pgl_vec4& screenColor, bool premultipliedAlpha)
{
    pgl_vec4 texColor = texture2D(texture, u, v);
    texColor = CubismPortableGLSimdApplyMultiplyAndScreen(texColor, multiplyColor, screenColor, premultipliedAlpha);

    pgl_vec4 outColor = pgl_simd_mul_vec4(texColor, baseColor);
    if (!premultipliedAlpha) {
        pgl_vec4 alphaScale = {outColor.w, outColor.w, outColor.w, 1.0f};
        outColor = pgl_simd_mul_vec4(outColor, alphaScale);
    }
    return outColor;
}

static inline float CubismPortableGLEvaluateMaskValue(GLuint texture, const pgl_vec4& channelFlag, const float* clipPos)
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

static inline CubismPortableGLLaneColor4 CubismPortableGLApplyMultiplyAndScreen4(CubismPortableGLLaneColor4 texColor, const pgl_vec4& multiplyColor, const pgl_vec4& screenColor, bool premultipliedAlpha)
{
    texColor.r = CubismPortableGLLaneScale(texColor.r, multiplyColor.x);
    texColor.g = CubismPortableGLLaneScale(texColor.g, multiplyColor.y);
    texColor.b = CubismPortableGLLaneScale(texColor.b, multiplyColor.z);

    if (premultipliedAlpha) {
        const pgl_simd_float4 screenR = CubismPortableGLLaneScale(texColor.a, screenColor.x);
        const pgl_simd_float4 screenG = CubismPortableGLLaneScale(texColor.a, screenColor.y);
        const pgl_simd_float4 screenB = CubismPortableGLLaneScale(texColor.a, screenColor.z);
        texColor.r = CubismPortableGLLaneSub(CubismPortableGLLaneAdd(texColor.r, screenR), CubismPortableGLLaneScale(texColor.r, screenColor.x));
        texColor.g = CubismPortableGLLaneSub(CubismPortableGLLaneAdd(texColor.g, screenG), CubismPortableGLLaneScale(texColor.g, screenColor.y));
        texColor.b = CubismPortableGLLaneSub(CubismPortableGLLaneAdd(texColor.b, screenB), CubismPortableGLLaneScale(texColor.b, screenColor.z));
        return texColor;
    }

    texColor.r = CubismPortableGLLaneSub(CubismPortableGLLaneAdd(texColor.r, CubismPortableGLLaneSet1(screenColor.x)), CubismPortableGLLaneScale(texColor.r, screenColor.x));
    texColor.g = CubismPortableGLLaneSub(CubismPortableGLLaneAdd(texColor.g, CubismPortableGLLaneSet1(screenColor.y)), CubismPortableGLLaneScale(texColor.g, screenColor.y));
    texColor.b = CubismPortableGLLaneSub(CubismPortableGLLaneAdd(texColor.b, CubismPortableGLLaneSet1(screenColor.z)), CubismPortableGLLaneScale(texColor.b, screenColor.z));
    return texColor;
}

static inline bool CubismPortableGLEvaluateDrawable4(GLuint texture, const pgl_simd_float4& u, const pgl_simd_float4& v, const pgl_vec4& baseColor, const pgl_vec4& multiplyColor, const pgl_vec4& screenColor, bool premultipliedAlpha, CubismPortableGLLaneColor4* out)
{
    CubismPortableGLLaneColor4 texColor = {};
    if (!CubismPortableGLSampleTexture4(texture, u, v, &texColor)) {
        return false;
    }

    texColor = CubismPortableGLApplyMultiplyAndScreen4(texColor, multiplyColor, screenColor, premultipliedAlpha);
    texColor = CubismPortableGLColorScaleConst(texColor, baseColor);
    if (!premultipliedAlpha) {
        texColor.r = CubismPortableGLLaneMul(texColor.r, texColor.a);
        texColor.g = CubismPortableGLLaneMul(texColor.g, texColor.a);
        texColor.b = CubismPortableGLLaneMul(texColor.b, texColor.a);
    }

    *out = texColor;
    return true;
}

static inline bool CubismPortableGLEvaluateMaskValue4(GLuint texture, const pgl_vec4& channelFlag, float (*fsInput)[4], pgl_simd_float4* outMask)
{
    pgl_simd_float4 u = {{0}};
    pgl_simd_float4 v = {{0}};
    for (int lane = 0; lane < 4; ++lane) {
        const float invW = fsInput[5][lane] != 0.0f ? (1.0f / fsInput[5][lane]) : 0.0f;
        u.lane[lane] = fsInput[2][lane] * invW;
        v.lane[lane] = 1.0f - (fsInput[3][lane] * invW);
    }

    CubismPortableGLLaneColor4 clipMask = {};
    if (!CubismPortableGLSampleTexture4(texture, u, v, &clipMask)) {
        return false;
    }

    const pgl_simd_float4 one = CubismPortableGLLaneSet1(1.0f);
    const pgl_simd_float4 invR = CubismPortableGLLaneSub(one, clipMask.r);
    const pgl_simd_float4 invG = CubismPortableGLLaneSub(one, clipMask.g);
    const pgl_simd_float4 invB = CubismPortableGLLaneSub(one, clipMask.b);
    const pgl_simd_float4 invA = CubismPortableGLLaneSub(one, clipMask.a);

    *outMask = CubismPortableGLLaneAdd(
        CubismPortableGLLaneAdd(CubismPortableGLLaneScale(invR, channelFlag.x), CubismPortableGLLaneScale(invG, channelFlag.y)),
        CubismPortableGLLaneAdd(CubismPortableGLLaneScale(invB, channelFlag.z), CubismPortableGLLaneScale(invA, channelFlag.w))
    );
    return true;
}

static inline pgl_simd_float4 CubismPortableGLPerspectiveComponent4(
    pgl_simd_float4 alphaLane,
    pgl_simd_float4 betaLane,
    pgl_simd_float4 gammaLane,
    pgl_simd_float4 reciprocalW,
    float p0,
    float p1,
    float p2)
{
    return CubismPortableGLLaneMul(
        CubismPortableGLMix3Scalar4(alphaLane, p0, betaLane, p1, gammaLane, p2),
        reciprocalW);
}

static inline bool CubismPortableGLEvaluateProgramBary4(
    GLint tag,
    const float* perspective,
    pgl_simd_float4 alphaLane,
    pgl_simd_float4 betaLane,
    pgl_simd_float4 gammaLane,
    pgl_simd_float4 reciprocalW,
    void* uniform,
    CubismPortableGLLaneColor4* out);

static inline bool CubismPortableGLChannelFlagIsOneHot(const pgl_vec4& channelFlag)
{
    int active = 0;
    active += channelFlag.x >= 0.5f ? 1 : 0;
    active += channelFlag.y >= 0.5f ? 1 : 0;
    active += channelFlag.z >= 0.5f ? 1 : 0;
    active += channelFlag.w >= 0.5f ? 1 : 0;
    return active == 1;
}

static inline void CubismPortableGLPackLaneColors(const CubismPortableGLLaneColor4& src, pgl_Color out[4])
{
    CubismPortableGLColorToPackedColors(src, 0xFu, out);
}

static inline bool CubismPortableGLEvaluateMaskValueDirect4(
    GLuint texture,
    const pgl_vec4& channelFlag,
    pgl_simd_float4 clipX,
    pgl_simd_float4 clipY,
    pgl_simd_float4 clipW,
    pgl_simd_float4* outMask)
{
    const pgl_simd_float4 reciprocalClipW = CubismPortableGLLaneReciprocal(clipW);
    const pgl_simd_float4 u = CubismPortableGLLaneMul(clipX, reciprocalClipW);
    const pgl_simd_float4 v = CubismPortableGLLaneSub(CubismPortableGLLaneSet1(1.0f), CubismPortableGLLaneMul(clipY, reciprocalClipW));

    CubismPortableGLLaneColor4 clipMask = {};
    if (!CubismPortableGLSampleTexture4(texture, u, v, &clipMask)) {
        return false;
    }

    const pgl_simd_float4 one = CubismPortableGLLaneSet1(1.0f);
    const pgl_simd_float4 invR = CubismPortableGLLaneSub(one, clipMask.r);
    const pgl_simd_float4 invG = CubismPortableGLLaneSub(one, clipMask.g);
    const pgl_simd_float4 invB = CubismPortableGLLaneSub(one, clipMask.b);
    const pgl_simd_float4 invA = CubismPortableGLLaneSub(one, clipMask.a);

    *outMask = CubismPortableGLLaneAdd(
        CubismPortableGLLaneAdd(CubismPortableGLLaneScale(invR, channelFlag.x), CubismPortableGLLaneScale(invG, channelFlag.y)),
        CubismPortableGLLaneAdd(CubismPortableGLLaneScale(invB, channelFlag.z), CubismPortableGLLaneScale(invA, channelFlag.w))
    );
    return true;
}

static inline bool CubismPortableGLEvaluateProgramPackedBary4(
    GLint tag,
    const float* perspective,
    pgl_simd_float4 alphaLane,
    pgl_simd_float4 betaLane,
    pgl_simd_float4 gammaLane,
    pgl_simd_float4 reciprocalW,
    void* uniform,
    pgl_Color out[4])
{
    const pgl_simd_float4 u = CubismPortableGLPerspectiveComponent4(
        alphaLane,
        betaLane,
        gammaLane,
        reciprocalW,
        perspective[0],
        perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 0],
        perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 0]);
    const pgl_simd_float4 v = CubismPortableGLPerspectiveComponent4(
        alphaLane,
        betaLane,
        gammaLane,
        reciprocalW,
        perspective[1],
        perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 1],
        perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 1]);

    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_Copy: {
        const CubismPortableGLCopyUniforms* shaderUniforms = static_cast<const CubismPortableGLCopyUniforms*>(uniform);
        pgl_Color sampled[4] = {};
        if (!CubismPortableGLSampleTexture4Packed(shaderUniforms->Texture0, u, v, sampled)) {
            return false;
        }
        const uint8_t baseR = CubismPortableGLFloatToByte(shaderUniforms->BaseColor.x);
        const uint8_t baseG = CubismPortableGLFloatToByte(shaderUniforms->BaseColor.y);
        const uint8_t baseB = CubismPortableGLFloatToByte(shaderUniforms->BaseColor.z);
        const uint8_t baseA = CubismPortableGLFloatToByte(shaderUniforms->BaseColor.w);
        for (int lane = 0; lane < 4; ++lane) {
            out[lane] = make_Color(
                CubismPortableGLMulByte(sampled[lane].r, baseR),
                CubismPortableGLMulByte(sampled[lane].g, baseG),
                CubismPortableGLMulByte(sampled[lane].b, baseB),
                CubismPortableGLMulByte(sampled[lane].a, baseA));
        }
        return true;
    }
    case CubismPortableGLProgramTag_SetupMask: {
        const CubismPortableGLSetupMaskUniforms* shaderUniforms = static_cast<const CubismPortableGLSetupMaskUniforms*>(uniform);
        if (!CubismPortableGLChannelFlagIsOneHot(shaderUniforms->ChannelFlag)) {
            CubismPortableGLLaneColor4 floatOut = {};
            if (!CubismPortableGLEvaluateProgramBary4(tag, perspective, alphaLane, betaLane, gammaLane, reciprocalW, uniform, &floatOut)) {
                return false;
            }
            CubismPortableGLPackLaneColors(floatOut, out);
            return true;
        }
        pgl_Color sampled[4] = {};
        if (!CubismPortableGLSampleTexture4Packed(shaderUniforms->Texture0, u, v, sampled)) {
            return false;
        }
        const int channelIndex = CubismPortableGLResolveChannelIndex(shaderUniforms->ChannelFlag);
        const pgl_simd_float4 clipX = CubismPortableGLPerspectiveComponent4(
            alphaLane, betaLane, gammaLane, reciprocalW,
            perspective[2], perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 2], perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 2]);
        const pgl_simd_float4 clipY = CubismPortableGLPerspectiveComponent4(
            alphaLane, betaLane, gammaLane, reciprocalW,
            perspective[3], perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 3], perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 3]);
        const pgl_simd_float4 clipW = CubismPortableGLPerspectiveComponent4(
            alphaLane, betaLane, gammaLane, reciprocalW,
            perspective[5], perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 5], perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 5]);
        const pgl_simd_float4 reciprocalClipW = CubismPortableGLLaneReciprocal(clipW);
        const pgl_simd_float4 clipNX = CubismPortableGLLaneMul(clipX, reciprocalClipW);
        const pgl_simd_float4 clipNY = CubismPortableGLLaneMul(clipY, reciprocalClipW);

        for (int lane = 0; lane < 4; ++lane) {
            const float x = clipNX.lane[lane];
            const float y = clipNY.lane[lane];
            const bool inside =
                x >= shaderUniforms->BaseColor.x &&
                y >= shaderUniforms->BaseColor.y &&
                x <= shaderUniforms->BaseColor.z &&
                y <= shaderUniforms->BaseColor.w;
            uint8_t alpha = inside ? sampled[lane].a : 0;
            out[lane] = make_Color(0, 0, 0, 0);
            switch (channelIndex) {
            case 0: out[lane].r = alpha; break;
            case 1: out[lane].g = alpha; break;
            case 2: out[lane].b = alpha; break;
            default: out[lane].a = alpha; break;
            }
        }
        return true;
    }
    default: {
        if (CubismPortableGLIsMaskedTag(tag)) {
            const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniform);
            if (!CubismPortableGLChannelFlagIsOneHot(shaderUniforms->ChannelFlag)) {
                CubismPortableGLLaneColor4 floatOut = {};
                if (!CubismPortableGLEvaluateProgramBary4(tag, perspective, alphaLane, betaLane, gammaLane, reciprocalW, uniform, &floatOut)) {
                    return false;
                }
                CubismPortableGLPackLaneColors(floatOut, out);
                return true;
            }
            if (!CubismPortableGLEvaluateDrawablePacked4(
                    shaderUniforms->Texture0, u, v,
                    shaderUniforms->BaseColor,
                    shaderUniforms->MultiplyColor,
                    shaderUniforms->ScreenColor,
                    CubismPortableGLIsPremultipliedTag(tag),
                    out)) {
                return false;
            }

            const pgl_simd_float4 clipX = CubismPortableGLPerspectiveComponent4(
                alphaLane, betaLane, gammaLane, reciprocalW,
                perspective[2], perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 2], perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 2]);
            const pgl_simd_float4 clipY = CubismPortableGLPerspectiveComponent4(
                alphaLane, betaLane, gammaLane, reciprocalW,
                perspective[3], perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 3], perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 3]);
            const pgl_simd_float4 clipW = CubismPortableGLPerspectiveComponent4(
                alphaLane, betaLane, gammaLane, reciprocalW,
                perspective[5], perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 5], perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 5]);
            const pgl_simd_float4 reciprocalClipW = CubismPortableGLLaneReciprocal(clipW);
            const pgl_simd_float4 clipU = CubismPortableGLLaneMul(clipX, reciprocalClipW);
            const pgl_simd_float4 clipV = CubismPortableGLLaneSub(CubismPortableGLLaneSet1(1.0f), CubismPortableGLLaneMul(clipY, reciprocalClipW));

            uint8_t maskValue[4] = {};
            if (!CubismPortableGLEvaluateMaskPacked4(shaderUniforms->Texture1, shaderUniforms->ChannelFlag, clipU, clipV, maskValue)) {
                return false;
            }
            for (int lane = 0; lane < 4; ++lane) {
                const uint8_t mask = CubismPortableGLIsInvertedMaskTag(tag) ? static_cast<uint8_t>(255u - maskValue[lane]) : maskValue[lane];
                out[lane].r = CubismPortableGLMulByte(out[lane].r, mask);
                out[lane].g = CubismPortableGLMulByte(out[lane].g, mask);
                out[lane].b = CubismPortableGLMulByte(out[lane].b, mask);
                out[lane].a = CubismPortableGLMulByte(out[lane].a, mask);
            }
            return true;
        }

        const CubismPortableGLDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLDrawableUniforms*>(uniform);
        return CubismPortableGLEvaluateDrawablePacked4(
            shaderUniforms->Texture0, u, v,
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            CubismPortableGLIsPremultipliedTag(tag),
            out);
    }
    }
}

static inline bool CubismPortableGLEvaluateProgramBary4(
    GLint tag,
    const float* perspective,
    pgl_simd_float4 alphaLane,
    pgl_simd_float4 betaLane,
    pgl_simd_float4 gammaLane,
    pgl_simd_float4 reciprocalW,
    void* uniform,
    CubismPortableGLLaneColor4* out)
{
    const pgl_simd_float4 u = CubismPortableGLPerspectiveComponent4(
        alphaLane,
        betaLane,
        gammaLane,
        reciprocalW,
        perspective[0],
        perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 0],
        perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 0]);
    const pgl_simd_float4 v = CubismPortableGLPerspectiveComponent4(
        alphaLane,
        betaLane,
        gammaLane,
        reciprocalW,
        perspective[1],
        perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 1],
        perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 1]);

    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_Copy: {
        const CubismPortableGLCopyUniforms* shaderUniforms = static_cast<const CubismPortableGLCopyUniforms*>(uniform);
        if (!CubismPortableGLSampleTexture4(shaderUniforms->Texture0, u, v, out)) {
            return false;
        }
        *out = CubismPortableGLColorScaleConst(*out, shaderUniforms->BaseColor);
        return true;
    }
    case CubismPortableGLProgramTag_SetupMask: {
        const CubismPortableGLSetupMaskUniforms* shaderUniforms = static_cast<const CubismPortableGLSetupMaskUniforms*>(uniform);
        CubismPortableGLLaneColor4 texColor = {};
        if (!CubismPortableGLSampleTexture4(shaderUniforms->Texture0, u, v, &texColor)) {
            return false;
        }

        const pgl_simd_float4 clipX = CubismPortableGLPerspectiveComponent4(
            alphaLane,
            betaLane,
            gammaLane,
            reciprocalW,
            perspective[2],
            perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 2],
            perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 2]);
        const pgl_simd_float4 clipY = CubismPortableGLPerspectiveComponent4(
            alphaLane,
            betaLane,
            gammaLane,
            reciprocalW,
            perspective[3],
            perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 3],
            perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 3]);
        const pgl_simd_float4 clipW = CubismPortableGLPerspectiveComponent4(
            alphaLane,
            betaLane,
            gammaLane,
            reciprocalW,
            perspective[5],
            perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 5],
            perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 5]);
        const pgl_simd_float4 reciprocalClipW = CubismPortableGLLaneReciprocal(clipW);
        const pgl_simd_float4 clipNX = CubismPortableGLLaneMul(clipX, reciprocalClipW);
        const pgl_simd_float4 clipNY = CubismPortableGLLaneMul(clipY, reciprocalClipW);

        pgl_simd_float4 inside = {{0}};
        for (int lane = 0; lane < 4; ++lane) {
            const float x = clipNX.lane[lane];
            const float y = clipNY.lane[lane];
            inside.lane[lane] =
                (x >= shaderUniforms->BaseColor.x ? 1.0f : 0.0f) *
                (y >= shaderUniforms->BaseColor.y ? 1.0f : 0.0f) *
                (x <= shaderUniforms->BaseColor.z ? 1.0f : 0.0f) *
                (y <= shaderUniforms->BaseColor.w ? 1.0f : 0.0f);
        }

        const pgl_simd_float4 alpha = CubismPortableGLLaneMul(texColor.a, inside);
        out->r = CubismPortableGLLaneScale(alpha, shaderUniforms->ChannelFlag.x);
        out->g = CubismPortableGLLaneScale(alpha, shaderUniforms->ChannelFlag.y);
        out->b = CubismPortableGLLaneScale(alpha, shaderUniforms->ChannelFlag.z);
        out->a = CubismPortableGLLaneScale(alpha, shaderUniforms->ChannelFlag.w);
        return true;
    }
    default: {
        if (CubismPortableGLIsMaskedTag(tag)) {
            const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniform);
            if (!CubismPortableGLEvaluateDrawable4(
                    shaderUniforms->Texture0,
                    u,
                    v,
                    shaderUniforms->BaseColor,
                    shaderUniforms->MultiplyColor,
                    shaderUniforms->ScreenColor,
                    CubismPortableGLIsPremultipliedTag(tag),
                    out)) {
                return false;
            }

            const pgl_simd_float4 clipX = CubismPortableGLPerspectiveComponent4(
                alphaLane,
                betaLane,
                gammaLane,
                reciprocalW,
                perspective[2],
                perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 2],
                perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 2]);
            const pgl_simd_float4 clipY = CubismPortableGLPerspectiveComponent4(
                alphaLane,
                betaLane,
                gammaLane,
                reciprocalW,
                perspective[3],
                perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 3],
                perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 3]);
            const pgl_simd_float4 clipW = CubismPortableGLPerspectiveComponent4(
                alphaLane,
                betaLane,
                gammaLane,
                reciprocalW,
                perspective[5],
                perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + 5],
                perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + 5]);

            pgl_simd_float4 maskValue = {{0}};
            if (!CubismPortableGLEvaluateMaskValueDirect4(shaderUniforms->Texture1, shaderUniforms->ChannelFlag, clipX, clipY, clipW, &maskValue)) {
                return false;
            }
            if (CubismPortableGLIsInvertedMaskTag(tag)) {
                maskValue = CubismPortableGLLaneSub(CubismPortableGLLaneSet1(1.0f), maskValue);
            }
            *out = CubismPortableGLColorScaleLane(*out, maskValue);
            return true;
        }

        const CubismPortableGLDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLDrawableUniforms*>(uniform);
        return CubismPortableGLEvaluateDrawable4(
            shaderUniforms->Texture0,
            u,
            v,
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            CubismPortableGLIsPremultipliedTag(tag),
            out);
    }
    }
}

static inline bool CubismPortableGLEvaluateProgram4(GLint tag, float (*fsInput)[4], void* uniform, CubismPortableGLLaneColor4* out)
{
    const pgl_simd_float4 u = {{fsInput[0][0], fsInput[0][1], fsInput[0][2], fsInput[0][3]}};
    const pgl_simd_float4 v = {{fsInput[1][0], fsInput[1][1], fsInput[1][2], fsInput[1][3]}};

    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_Copy: {
        const CubismPortableGLCopyUniforms* shaderUniforms = static_cast<const CubismPortableGLCopyUniforms*>(uniform);
        if (!CubismPortableGLSampleTexture4(shaderUniforms->Texture0, u, v, out)) {
            return false;
        }
        *out = CubismPortableGLColorScaleConst(*out, shaderUniforms->BaseColor);
        return true;
    }
    case CubismPortableGLProgramTag_SetupMask: {
        const CubismPortableGLSetupMaskUniforms* shaderUniforms = static_cast<const CubismPortableGLSetupMaskUniforms*>(uniform);
        CubismPortableGLLaneColor4 texColor = {};
        if (!CubismPortableGLSampleTexture4(shaderUniforms->Texture0, u, v, &texColor)) {
            return false;
        }

        pgl_simd_float4 inside = {{0}};
        for (int lane = 0; lane < 4; ++lane) {
            const float invW = fsInput[5][lane] != 0.0f ? (1.0f / fsInput[5][lane]) : 0.0f;
            const float x = fsInput[2][lane] * invW;
            const float y = fsInput[3][lane] * invW;
            inside.lane[lane] =
                (x >= shaderUniforms->BaseColor.x ? 1.0f : 0.0f) *
                (y >= shaderUniforms->BaseColor.y ? 1.0f : 0.0f) *
                (x <= shaderUniforms->BaseColor.z ? 1.0f : 0.0f) *
                (y <= shaderUniforms->BaseColor.w ? 1.0f : 0.0f);
        }

        const pgl_simd_float4 alpha = CubismPortableGLLaneMul(texColor.a, inside);
        out->r = CubismPortableGLLaneScale(alpha, shaderUniforms->ChannelFlag.x);
        out->g = CubismPortableGLLaneScale(alpha, shaderUniforms->ChannelFlag.y);
        out->b = CubismPortableGLLaneScale(alpha, shaderUniforms->ChannelFlag.z);
        out->a = CubismPortableGLLaneScale(alpha, shaderUniforms->ChannelFlag.w);
        return true;
    }
    default: {
        if (CubismPortableGLIsMaskedTag(tag)) {
            const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniform);
            if (!CubismPortableGLEvaluateDrawable4(
                    shaderUniforms->Texture0,
                    u,
                    v,
                    shaderUniforms->BaseColor,
                    shaderUniforms->MultiplyColor,
                    shaderUniforms->ScreenColor,
                    CubismPortableGLIsPremultipliedTag(tag),
                    out)) {
                return false;
            }

            pgl_simd_float4 maskValue = {{0}};
            if (!CubismPortableGLEvaluateMaskValue4(shaderUniforms->Texture1, shaderUniforms->ChannelFlag, fsInput, &maskValue)) {
                return false;
            }
            if (CubismPortableGLIsInvertedMaskTag(tag)) {
                maskValue = CubismPortableGLLaneSub(CubismPortableGLLaneSet1(1.0f), maskValue);
            }

            *out = CubismPortableGLColorScaleLane(*out, maskValue);
            return true;
        }

        const CubismPortableGLDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLDrawableUniforms*>(uniform);
        return CubismPortableGLEvaluateDrawable4(
            shaderUniforms->Texture0,
            u,
            v,
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            CubismPortableGLIsPremultipliedTag(tag),
            out
        );
    }
    }
}

static inline pgl_vec4 CubismPortableGLEvaluateProgram(GLint tag, const float* fsInput, void* uniform)
{
    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_Copy: {
        const CubismPortableGLCopyUniforms* shaderUniforms = static_cast<const CubismPortableGLCopyUniforms*>(uniform);
        const pgl_vec4 texColor = texture2D(shaderUniforms->Texture0, fsInput[0], fsInput[1]);
        return pgl_simd_mul_vec4(texColor, shaderUniforms->BaseColor);
    }
    case CubismPortableGLProgramTag_SetupMask: {
        const CubismPortableGLSetupMaskUniforms* shaderUniforms = static_cast<const CubismPortableGLSetupMaskUniforms*>(uniform);
        const float x = fsInput[2] / fsInput[5];
        const float y = fsInput[3] / fsInput[5];
        const float isInside =
            (x >= shaderUniforms->BaseColor.x ? 1.0f : 0.0f) *
            (y >= shaderUniforms->BaseColor.y ? 1.0f : 0.0f) *
            (x <= shaderUniforms->BaseColor.z ? 1.0f : 0.0f) *
            (y <= shaderUniforms->BaseColor.w ? 1.0f : 0.0f);
        const float alpha = texture2D(shaderUniforms->Texture0, fsInput[0], fsInput[1]).w * isInside;
        return pgl_simd_scale_vec4(shaderUniforms->ChannelFlag, alpha);
    }
    default: {
        if (CubismPortableGLIsMaskedTag(tag)) {
            const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniform);
            pgl_vec4 color = CubismPortableGLEvaluateDrawable(
                shaderUniforms->Texture0,
                fsInput[0],
                fsInput[1],
                shaderUniforms->BaseColor,
                shaderUniforms->MultiplyColor,
                shaderUniforms->ScreenColor,
                CubismPortableGLIsPremultipliedTag(tag)
            );

            float maskValue = CubismPortableGLEvaluateMaskValue(shaderUniforms->Texture1, shaderUniforms->ChannelFlag, fsInput + 2);
            if (CubismPortableGLIsInvertedMaskTag(tag)) {
                maskValue = 1.0f - maskValue;
            }
            return pgl_simd_scale_vec4(color, maskValue);
        }

        const CubismPortableGLDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLDrawableUniforms*>(uniform);
        return CubismPortableGLEvaluateDrawable(
            shaderUniforms->Texture0,
            fsInput[0],
            fsInput[1],
            shaderUniforms->BaseColor,
            shaderUniforms->MultiplyColor,
            shaderUniforms->ScreenColor,
            CubismPortableGLIsPremultipliedTag(tag)
        );
    }
    }
}

static inline bool CubismPortableGLBlendPixels4(const CubismPortableGLLaneColor4& src, const pix_t dstPixels[4], unsigned laneMask, CubismPortableGLFastBlendKind blendKind, pix_t outPixels[4])
{
    if (blendKind == CubismPortableGLFastBlendKind_Unsupported) {
        return false;
    }

    pgl_Color srcColors[4] = {};
    CubismPortableGLColorToPackedColors(src, laneMask, srcColors);

    for (int lane = 0; lane < 4; ++lane) {
        if ((laneMask & (1u << lane)) == 0u) {
            continue;
        }

        const pgl_Color srcColor = srcColors[lane];
        if (blendKind == CubismPortableGLFastBlendKind_Opaque) {
            outPixels[lane] = RGBA_TO_PIXEL(srcColor.r, srcColor.g, srcColor.b, srcColor.a);
            continue;
        }

        const pgl_Color dstColor = PIXEL_TO_COLOR(dstPixels[lane]);
        pgl_Color result = srcColor;

        switch (blendKind) {
        case CubismPortableGLFastBlendKind_Normal: {
            const unsigned invAlpha = 255u - srcColor.a;
            result.r = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.r) + (invAlpha * dstColor.r + 127u) / 255u);
            result.g = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.g) + (invAlpha * dstColor.g + 127u) / 255u);
            result.b = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.b) + (invAlpha * dstColor.b + 127u) / 255u);
            result.a = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.a) + (invAlpha * dstColor.a + 127u) / 255u);
            break;
        }
        case CubismPortableGLFastBlendKind_Add:
            result.r = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.r) + dstColor.r);
            result.g = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.g) + dstColor.g);
            result.b = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.b) + dstColor.b);
            result.a = dstColor.a;
            break;
        case CubismPortableGLFastBlendKind_Multiply: {
            const unsigned scale = 255u - srcColor.a;
            result.r = CubismPortableGLMulByte(dstColor.r, CubismPortableGLClampByte(static_cast<unsigned>(srcColor.r) + scale));
            result.g = CubismPortableGLMulByte(dstColor.g, CubismPortableGLClampByte(static_cast<unsigned>(srcColor.g) + scale));
            result.b = CubismPortableGLMulByte(dstColor.b, CubismPortableGLClampByte(static_cast<unsigned>(srcColor.b) + scale));
            result.a = dstColor.a;
            break;
        }
        case CubismPortableGLFastBlendKind_SetupMask:
            result.r = CubismPortableGLMulByte(dstColor.r, static_cast<uint8_t>(255u - srcColor.r));
            result.g = CubismPortableGLMulByte(dstColor.g, static_cast<uint8_t>(255u - srcColor.g));
            result.b = CubismPortableGLMulByte(dstColor.b, static_cast<uint8_t>(255u - srcColor.b));
            result.a = CubismPortableGLMulByte(dstColor.a, static_cast<uint8_t>(255u - srcColor.a));
            break;
        default:
            return false;
        }

        outPixels[lane] = RGBA_TO_PIXEL(result.r, result.g, result.b, result.a);
    }

    return true;
}

static inline bool CubismPortableGLBlendPackedPixels4(const pgl_Color srcColors[4], const pix_t dstPixels[4], unsigned laneMask, CubismPortableGLFastBlendKind blendKind, pix_t outPixels[4])
{
    if (blendKind == CubismPortableGLFastBlendKind_Unsupported) {
        return false;
    }

    for (int lane = 0; lane < 4; ++lane) {
        if ((laneMask & (1u << lane)) == 0u) {
            continue;
        }

        const pgl_Color srcColor = srcColors[lane];
        if (blendKind == CubismPortableGLFastBlendKind_Opaque) {
            outPixels[lane] = RGBA_TO_PIXEL(srcColor.r, srcColor.g, srcColor.b, srcColor.a);
            continue;
        }

        const pgl_Color dstColor = PIXEL_TO_COLOR(dstPixels[lane]);
        pgl_Color result = srcColor;

        switch (blendKind) {
        case CubismPortableGLFastBlendKind_Normal: {
            const unsigned invAlpha = 255u - srcColor.a;
            result.r = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.r) + (invAlpha * dstColor.r + 127u) / 255u);
            result.g = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.g) + (invAlpha * dstColor.g + 127u) / 255u);
            result.b = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.b) + (invAlpha * dstColor.b + 127u) / 255u);
            result.a = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.a) + (invAlpha * dstColor.a + 127u) / 255u);
            break;
        }
        case CubismPortableGLFastBlendKind_Add:
            result.r = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.r) + dstColor.r);
            result.g = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.g) + dstColor.g);
            result.b = CubismPortableGLClampByte(static_cast<unsigned>(srcColor.b) + dstColor.b);
            result.a = dstColor.a;
            break;
        case CubismPortableGLFastBlendKind_Multiply: {
            const unsigned scale = 255u - srcColor.a;
            result.r = CubismPortableGLMulByte(dstColor.r, CubismPortableGLClampByte(static_cast<unsigned>(srcColor.r) + scale));
            result.g = CubismPortableGLMulByte(dstColor.g, CubismPortableGLClampByte(static_cast<unsigned>(srcColor.g) + scale));
            result.b = CubismPortableGLMulByte(dstColor.b, CubismPortableGLClampByte(static_cast<unsigned>(srcColor.b) + scale));
            result.a = dstColor.a;
            break;
        }
        case CubismPortableGLFastBlendKind_SetupMask:
            result.r = CubismPortableGLMulByte(dstColor.r, static_cast<uint8_t>(255u - srcColor.r));
            result.g = CubismPortableGLMulByte(dstColor.g, static_cast<uint8_t>(255u - srcColor.g));
            result.b = CubismPortableGLMulByte(dstColor.b, static_cast<uint8_t>(255u - srcColor.b));
            result.a = CubismPortableGLMulByte(dstColor.a, static_cast<uint8_t>(255u - srcColor.a));
            break;
        default:
            return false;
        }

        outPixels[lane] = RGBA_TO_PIXEL(result.r, result.g, result.b, result.a);
    }

    return true;
}

} // namespace

static GLboolean pgl_cubism_try_draw_triangle_fast(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke)
{
    const GLint tag = c->programs.a[c->cur_program].user_tag;
    void* uniform = c->programs.a[c->cur_program].uniform;
    const CubismPortableGLFastBlendKind blendKind = CubismPortableGLResolveFastBlendKind();
    if (!CubismPortableGLIsSupportedTag(tag) || uniform == NULL) {
        return GL_FALSE;
    }

    if (c->programs.a[c->cur_program].fragdepth_or_discard) {
        return GL_FALSE;
    }

    if (blendKind == CubismPortableGLFastBlendKind_Unsupported || c->logic_ops) {
        return GL_FALSE;
    }

#ifndef PGL_DISABLE_COLOR_MASK
    if (c->color_mask != static_cast<pix_t>(~0u)) {
        return GL_FALSE;
    }
#endif

    switch (static_cast<CubismPortableGLProgramTag>(tag)) {
    case CubismPortableGLProgramTag_Copy: {
        const CubismPortableGLCopyUniforms* shaderUniforms = static_cast<const CubismPortableGLCopyUniforms*>(uniform);
        if (!CubismPortableGLCanUseFastTexture(shaderUniforms->Texture0)) {
            return GL_FALSE;
        }
        break;
    }
    case CubismPortableGLProgramTag_SetupMask: {
        const CubismPortableGLSetupMaskUniforms* shaderUniforms = static_cast<const CubismPortableGLSetupMaskUniforms*>(uniform);
        if (!CubismPortableGLCanUseFastTexture(shaderUniforms->Texture0)) {
            return GL_FALSE;
        }
        break;
    }
    default: {
        if (CubismPortableGLIsMaskedTag(tag)) {
            const CubismPortableGLMaskedDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLMaskedDrawableUniforms*>(uniform);
            if (!CubismPortableGLCanUseFastTexture(shaderUniforms->Texture0) ||
                !CubismPortableGLCanUseFastTexture(shaderUniforms->Texture1)) {
                return GL_FALSE;
            }
        } else {
            const CubismPortableGLDrawableUniforms* shaderUniforms = static_cast<const CubismPortableGLDrawableUniforms*>(uniform);
            if (!CubismPortableGLCanUseFastTexture(shaderUniforms->Texture0)) {
                return GL_FALSE;
            }
        }
        break;
    }
    }

    pgl_vec4 p0 = v0->screen_space;
    pgl_vec4 p1 = v1->screen_space;
    pgl_vec4 p2 = v2->screen_space;

    pgl_vec3 hp0 = v4_to_v3h(p0);
    pgl_vec3 hp1 = v4_to_v3h(p1);
    pgl_vec3 hp2 = v4_to_v3h(p2);

#ifndef PGL_NO_DEPTH_NO_STENCIL
    float polyOffset = 0.0f;
    if (c->poly_offset_fill) {
        polyOffset = calc_poly_offset(hp0, hp1, hp2);
    }
#endif

    float xMin = MIN(hp0.x, hp1.x);
    float xMax = MAX(hp0.x, hp1.x);
    float yMin = MIN(hp0.y, hp1.y);
    float yMax = MAX(hp0.y, hp1.y);

    xMin = MIN(hp2.x, xMin);
    xMax = MAX(hp2.x, xMax);
    yMin = MIN(hp2.y, yMin);
    yMax = MAX(hp2.y, yMax);

    xMin = MAX(c->lx, xMin);
    xMax = MIN(c->ux, xMax);
    yMin = MAX(c->ly, yMin);
    yMax = MIN(c->uy, yMax);

    const int ixBegin = static_cast<int>(xMin);
    const int ixMax = roundf(xMax);
    const int iyBegin = static_cast<int>(yMin);
    const int iyMax = roundf(yMax);

    if (ixBegin >= ixMax || iyBegin >= iyMax) {
        return GL_TRUE;
    }

    pgl_Line l01 = make_Line(hp0.x, hp0.y, hp1.x, hp1.y);
    pgl_Line l12 = make_Line(hp1.x, hp1.y, hp2.x, hp2.y);
    pgl_Line l20 = make_Line(hp2.x, hp2.y, hp0.x, hp0.y);

    const float gammaDen = line_func(&l01, hp2.x, hp2.y);
    const float betaDen = line_func(&l20, hp1.x, hp1.y);
    if (gammaDen == 0.0f || betaDen == 0.0f) {
        return GL_FALSE;
    }

#if defined(CSM_TARGET_ESP_PGL)
    ++pgl_cubism_perf_current.FastTriangleCount;
    const uint64_t fastTriangleStartUs = pgl_cubism_perf_begin_timing();
#endif

    const float dGammaDx = l01.A / gammaDen;
    const float dGammaDy = l01.B / gammaDen;
    const float dBetaDx = l20.A / betaDen;
    const float dBetaDy = l20.B / betaDen;
    const bool alphaEdgeInclusive = line_func(&l12, hp0.x, hp0.y) * line_func(&l12, -1.0f, -2.5f) > 0.0f;
    const bool betaEdgeInclusive = line_func(&l20, hp1.x, hp1.y) * line_func(&l20, -1.0f, -2.5f) > 0.0f;
    const bool gammaEdgeInclusive = line_func(&l01, hp2.x, hp2.y) * line_func(&l01, -1.0f, -2.5f) > 0.0f;

    float perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS * 3];
    float* vsOutput = &c->vs_output.output_buf[0];
    for (int i = 0; i < c->vs_output.size; ++i) {
        perspective[i] = v0->vs_out[i] / p0.w;
        perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] = v1->vs_out[i] / p1.w;
        perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] = v2->vs_out[i] / p2.w;
    }

    const float invW0 = 1.0f / p0.w;
    const float invW1 = 1.0f / p1.w;
    const float invW2 = 1.0f / p2.w;
    float fsInput[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    const pgl_simd_float4 laneOffsets = {{0.0f, 1.0f, 2.0f, 3.0f}};
    const pgl_simd_float4 oneLane = CubismPortableGLLaneSet1(1.0f);
    const pgl_simd_float4 gammaDxLane = CubismPortableGLLaneScale(laneOffsets, dGammaDx);
    const pgl_simd_float4 betaDxLane = CubismPortableGLLaneScale(laneOffsets, dBetaDx);
    float gammaRow = line_func(&l01, static_cast<float>(ixBegin) + 0.5f, static_cast<float>(iyBegin) + 0.5f) / gammaDen;
    float betaRow = line_func(&l20, static_cast<float>(ixBegin) + 0.5f, static_cast<float>(iyBegin) + 0.5f) / betaDen;

    for (int iy = iyBegin; iy < iyMax; ++iy) {
        float gammaBase = gammaRow;
        float betaBase = betaRow;
        pix_t* rowPixels = pgl_backbuffer_pixel_ptr(0, iy);
        if (rowPixels == NULL) {
            gammaRow += dGammaDy;
            betaRow += dBetaDy;
            continue;
        }

        int ix = ixBegin;
        for (; ix + 3 < ixMax; ix += 4) {
            const pgl_simd_float4 gammaLane = CubismPortableGLLaneAdd(CubismPortableGLLaneSet1(gammaBase), gammaDxLane);
            const pgl_simd_float4 betaLane = CubismPortableGLLaneAdd(CubismPortableGLLaneSet1(betaBase), betaDxLane);
            const pgl_simd_float4 alphaLane = CubismPortableGLLaneSub(CubismPortableGLLaneSub(oneLane, betaLane), gammaLane);
            const pgl_simd_float4 invWLane = CubismPortableGLMix3Scalar4(alphaLane, invW0, betaLane, invW1, gammaLane, invW2);
            unsigned activeMask = 0u;

            for (int lane = 0; lane < 4; ++lane) {
                const float gamma = gammaLane.lane[lane];
                const float beta = betaLane.lane[lane];
                const float alpha = alphaLane.lane[lane];

                if (alpha < 0.0f || beta < 0.0f || gamma < 0.0f) {
                    continue;
                }
                if ((alpha <= 0.0f && !alphaEdgeInclusive) ||
                    (beta <= 0.0f && !betaEdgeInclusive) ||
                    (gamma <= 0.0f && !gammaEdgeInclusive)) {
                    continue;
                }

#ifndef PGL_NO_DEPTH_NO_STENCIL
                float z = alpha * hp0.z + beta * hp1.z + gamma * hp2.z;
                z += polyOffset;
                z = rsw_mapf(z, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
                if (!fragment_processing(ix + lane, iy, z)) {
                    continue;
                }
#endif

                activeMask |= (1u << lane);
            }

            if (activeMask != 0u) {
                pgl_Color colorBatch[4] = {};
                const pgl_simd_float4 reciprocalW = CubismPortableGLLaneReciprocal(invWLane);
#if defined(CSM_TARGET_ESP_PGL)
                const uint64_t shadeStartUs = pgl_cubism_perf_begin_timing();
#endif
                if (!CubismPortableGLEvaluateProgramPackedBary4(tag, perspective, alphaLane, betaLane, gammaLane, reciprocalW, uniform, colorBatch)) {
#if defined(CSM_TARGET_ESP_PGL)
                    pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleShadeMilliseconds, shadeStartUs);
                    pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleFastMilliseconds, fastTriangleStartUs);
#endif
                    return GL_FALSE;
                }
#if defined(CSM_TARGET_ESP_PGL)
                pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleShadeMilliseconds, shadeStartUs);
#endif

                pix_t dstPixels[4] = {rowPixels[ix], rowPixels[ix + 1], rowPixels[ix + 2], rowPixels[ix + 3]};
                pix_t srcPixels[4] = {0, 0, 0, 0};
#if defined(CSM_TARGET_ESP_PGL)
                const uint64_t blendStartUs = pgl_cubism_perf_begin_timing();
#endif
                if (!CubismPortableGLBlendPackedPixels4(colorBatch, dstPixels, activeMask, blendKind, srcPixels)) {
#if defined(CSM_TARGET_ESP_PGL)
                    pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleBlendMilliseconds, blendStartUs);
                    pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleFastMilliseconds, fastTriangleStartUs);
#endif
                    return GL_FALSE;
                }
#if defined(CSM_TARGET_ESP_PGL)
                pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleBlendMilliseconds, blendStartUs);
#endif

                for (int lane = 0; lane < 4; ++lane) {
                    if ((activeMask & (1u << lane)) != 0u) {
                        rowPixels[ix + lane] = srcPixels[lane];
                    }
                }
            }

            gammaBase += dGammaDx * 4.0f;
            betaBase += dBetaDx * 4.0f;
        }

        for (; ix < ixMax; ++ix) {
#if defined(CSM_TARGET_ESP_PGL)
            const uint64_t scalarTailStartUs = pgl_cubism_perf_begin_timing();
#endif
            const float gamma = gammaBase;
            const float beta = betaBase;
            const float alpha = 1.0f - beta - gamma;

            gammaBase += dGammaDx;
            betaBase += dBetaDx;

            if (alpha < 0.0f || beta < 0.0f || gamma < 0.0f) {
                continue;
            }
            if ((alpha <= 0.0f && !alphaEdgeInclusive) ||
                (beta <= 0.0f && !betaEdgeInclusive) ||
                (gamma <= 0.0f && !gammaEdgeInclusive)) {
                continue;
            }

            const float invW = alpha * invW0 + beta * invW1 + gamma * invW2;
#ifndef PGL_NO_DEPTH_NO_STENCIL
            float z = alpha * hp0.z + beta * hp1.z + gamma * hp2.z;
            z += polyOffset;
            z = rsw_mapf(z, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
            if (!fragment_processing(ix, iy, z)) {
                continue;
            }
#endif

            for (int component = 0; component < c->vs_output.size; ++component) {
                if (c->vs_output.interpolation[component] == PGL_SMOOTH) {
                    const float value = alpha * perspective[component]
                                      + beta * perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + component]
                                      + gamma * perspective[2 * GL_MAX_VERTEX_OUTPUT_COMPONENTS + component];
                    fsInput[component] = value / invW;
                } else if (c->vs_output.interpolation[component] == PGL_NOPERSPECTIVE) {
                    fsInput[component] = alpha * v0->vs_out[component] + beta * v1->vs_out[component] + gamma * v2->vs_out[component];
                } else {
                    fsInput[component] = vsOutput[provoke * c->vs_output.size + component];
                }
            }

            const pgl_vec4 color = CubismPortableGLEvaluateProgram(tag, fsInput, uniform);
#ifdef PGL_NO_DEPTH_NO_STENCIL
            draw_pixel(color, ix, iy, 0.0f, GL_FALSE);
#else
            draw_pixel(color, ix, iy, z, GL_FALSE);
#endif
#if defined(CSM_TARGET_ESP_PGL)
            pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleScalarTailMilliseconds, scalarTailStartUs);
#endif
        }

        gammaRow += dGammaDy;
        betaRow += dBetaDy;
    }

#if defined(CSM_TARGET_ESP_PGL)
    pgl_cubism_perf_add_ms(&pgl_cubism_perf_current.TriangleFastMilliseconds, fastTriangleStartUs);
#endif
    return GL_TRUE;
}

#if defined(CSM_TARGET_ESP_PGL)
extern "C" uint64_t pgl_perf_now_us(void)
{
    return static_cast<uint64_t>(esp_timer_get_time());
}
#endif

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

namespace {
    CubismPortableGLMainFramebufferState s_mainFramebuffer = {NULL, 0, 0, GL_FALSE};
    GLuint s_currentRenderTarget = 0;
    CubismPortableGLPerfStats s_perfStats = {};

    void ApplyMainFramebufferBinding()
    {
        if (!s_mainFramebuffer.Pixels)
        {
            return;
        }

        pglSetBackBuffer(s_mainFramebuffer.Pixels,
                         static_cast<GLsizei>(s_mainFramebuffer.Width),
                         static_cast<GLsizei>(s_mainFramebuffer.Height),
                         s_mainFramebuffer.UserOwned);

        s_currentRenderTarget = 0;
    }
}

void CubismPortableGLSetMainFramebuffer(pix_t* pixels, csmUint32 width, csmUint32 height, GLboolean userOwned)
{
    s_mainFramebuffer.Pixels = pixels;
    s_mainFramebuffer.Width = width;
    s_mainFramebuffer.Height = height;
    s_mainFramebuffer.UserOwned = userOwned;

    if (pixels)
    {
        ApplyMainFramebufferBinding();
    }
}

void CubismPortableGLRefreshFramebufferState()
{
    if (s_currentRenderTarget == 0 && s_mainFramebuffer.Pixels != NULL)
    {
        ApplyMainFramebufferBinding();
    }
}

void CubismPortableGLBindMainFramebuffer()
{
    if (!s_mainFramebuffer.Pixels)
    {
        return;
    }

    ApplyMainFramebufferBinding();
}

void CubismPortableGLBindRenderTarget(GLuint renderTarget)
{
    if (renderTarget == 0)
    {
        CubismPortableGLBindMainFramebuffer();
        return;
    }

    pglSetTexBackBuffer(renderTarget);
    s_currentRenderTarget = renderTarget;
}

GLuint CubismPortableGLGetCurrentRenderTarget()
{
    return s_currentRenderTarget;
}

const CubismPortableGLMainFramebufferState& CubismPortableGLGetMainFramebufferState()
{
    return s_mainFramebuffer;
}

void CubismPortableGLResetPerfStats()
{
#if defined(CSM_TARGET_ESP_PGL)
    pgl_cubism_perf_reset();
#endif
    s_perfStats = {};
}

const CubismPortableGLPerfStats& CubismPortableGLGetPerfStats()
{
#if defined(CSM_TARGET_ESP_PGL)
    s_perfStats.PipelineMilliseconds = pgl_cubism_perf_current.PipelineMilliseconds;
    s_perfStats.VertexStageMilliseconds = pgl_cubism_perf_current.VertexStageMilliseconds;
    s_perfStats.PrimitiveLoopMilliseconds = pgl_cubism_perf_current.PrimitiveLoopMilliseconds;
    s_perfStats.TriangleFastMilliseconds = pgl_cubism_perf_current.TriangleFastMilliseconds;
    s_perfStats.TriangleSampleMilliseconds = pgl_cubism_perf_current.TriangleSampleMilliseconds;
    s_perfStats.TriangleShadeMilliseconds = pgl_cubism_perf_current.TriangleShadeMilliseconds;
    s_perfStats.TriangleBlendMilliseconds = pgl_cubism_perf_current.TriangleBlendMilliseconds;
    s_perfStats.TriangleScalarMilliseconds = pgl_cubism_perf_current.TriangleScalarMilliseconds;
    s_perfStats.TriangleScalarTailMilliseconds = pgl_cubism_perf_current.TriangleScalarTailMilliseconds;
    s_perfStats.PipelineCallCount = pgl_cubism_perf_current.PipelineCallCount;
    s_perfStats.FastTriangleCount = pgl_cubism_perf_current.FastTriangleCount;
    s_perfStats.ScalarTriangleCount = pgl_cubism_perf_current.ScalarTriangleCount;
    s_perfStats.SampleCallCount = pgl_cubism_perf_current.SampleCallCount;
#endif
    return s_perfStats;
}

}}}}
