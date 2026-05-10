/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "Live2DCubismCore.hpp"

#include <vector>

namespace
{
using namespace Live2D::Cubism::Core;

csmInt32 PackBlendMode(const csmInt32 colorBlend, const csmInt32 alphaBlend = csmAlphaBlendType_Over)
{
    return colorBlend | (alphaBlend << 8);
}

template<typename T>
std::size_t GetSafeCount(T count)
{
    return count > 0 ? static_cast<std::size_t>(count) : 0u;
}

const csmVector4 kDefaultMultiplyColor = { 1.0f, 1.0f, 1.0f, 1.0f };
const csmVector4 kDefaultScreenColor = { 0.0f, 0.0f, 0.0f, 1.0f };

thread_local std::vector<csmVector4> s_drawableMultiplyColors;
thread_local std::vector<csmVector4> s_drawableScreenColors;
thread_local std::vector<csmInt32> s_drawableBlendModes;
thread_local std::vector<csmInt32> s_partOffscreenIndices;

const csmInt32 kNoOffscreenIndex = -1;
const csmInt32* kNullMask = nullptr;
const csmInt32* const kDummyMasks[] = { kNullMask };
const csmInt32 kDummyMaskCounts[] = { 0 };
const csmInt32 kDummyOwnerIndices[] = { -1 };
const csmFloat32 kDummyOpacities[] = { 1.0f };
const csmByte kDummyConstantFlags[] = { 0 };
const csmInt32 kDummyBlendModes[] = { PackBlendMode(csmColorBlendType_Normal) };
const csmVector4 kDummyMultiplyColors[] = { kDefaultMultiplyColor };
const csmVector4 kDummyScreenColors[] = { kDefaultScreenColor };

void FillColors(std::vector<csmVector4>& destination,
                const float* const* source,
                const csmInt32 count,
                const csmVector4& fallback)
{
    const std::size_t size = GetSafeCount(count);
    destination.resize(size);

    for (std::size_t i = 0; i < size; ++i)
    {
        const float* color = (source && source[i]) ? source[i] : nullptr;
        destination[i].X = color ? color[0] : fallback.X;
        destination[i].Y = color ? color[1] : fallback.Y;
        destination[i].Z = color ? color[2] : fallback.Z;
        destination[i].W = color ? color[3] : fallback.W;
    }
}
}

namespace Live2D { namespace Cubism { namespace Core {

csmMocVersion csmGetMocVersion(const void* address, csmSizeInt size)
{
    (void)size;
    return ::csmGetMocVersion(reinterpret_cast<const csmByte*>(address));
}

csmBool csmHasMocConsistency(const void* address, csmSizeInt size)
{
    return ::csmHasMocConsistency(reinterpret_cast<const csmByte*>(address), size);
}

csmMoc* csmReviveMocInPlace(void* address, csmSizeInt size)
{
    return ::csmReviveMocInPlace(reinterpret_cast<csmByte*>(address), size);
}

csmFloat32* csmGetParameterValues(csmModel* model)
{
    return const_cast<csmFloat32*>(::csmGetParameterValues(model));
}

csmFloat32* csmGetPartOpacities(csmModel* model)
{
    return const_cast<csmFloat32*>(::csmGetPartOpacities(model));
}

const csmVector2* const* csmGetDrawableVertexPositions(const csmModel* model)
{
    return reinterpret_cast<const csmVector2* const*>(::csmGetDrawableVertexPositions(model));
}

const csmVector2* const* csmGetDrawableVertexUvs(const csmModel* model)
{
    return reinterpret_cast<const csmVector2* const*>(::csmGetDrawableVertexUvs(model));
}

const csmUint16* const* csmGetDrawableIndices(const csmModel* model)
{
    return reinterpret_cast<const csmUint16* const*>(::csmGetDrawableIndices(model));
}

const csmVector4* csmGetDrawableMultiplyColors(const csmModel* model)
{
    FillColors(s_drawableMultiplyColors, ::csmGetDrawableMultiplyColors(model), ::csmGetDrawableCount(model), kDefaultMultiplyColor);
    return s_drawableMultiplyColors.empty() ? nullptr : s_drawableMultiplyColors.data();
}

const csmVector4* csmGetDrawableScreenColors(const csmModel* model)
{
    FillColors(s_drawableScreenColors, ::csmGetDrawableScreenColors(model), ::csmGetDrawableCount(model), kDefaultScreenColor);
    return s_drawableScreenColors.empty() ? nullptr : s_drawableScreenColors.data();
}

const csmInt32* csmGetDrawableBlendModes(const csmModel* model)
{
    const csmInt32 drawableCount = ::csmGetDrawableCount(model);
    const std::size_t count = GetSafeCount(drawableCount);
    s_drawableBlendModes.resize(count);

    const csmByte* const constantFlags = ::csmGetDrawableConstantFlags(model);

    for (std::size_t i = 0; i < count; ++i)
    {
        const csmByte flags = constantFlags ? constantFlags[i] : 0;

        if ((flags & csmBlendAdditive) != 0)
        {
            s_drawableBlendModes[i] = PackBlendMode(csmColorBlendType_AddCompatible);
        }
        else if ((flags & csmBlendMultiplicative) != 0)
        {
            s_drawableBlendModes[i] = PackBlendMode(csmColorBlendType_MultiplyCompatible);
        }
        else
        {
            s_drawableBlendModes[i] = PackBlendMode(csmColorBlendType_Normal);
        }
    }

    return s_drawableBlendModes.empty() ? nullptr : s_drawableBlendModes.data();
}

const csmInt32* csmGetRenderOrders(const csmModel* model)
{
    return ::csmGetDrawableRenderOrders(model);
}

const csmInt32* csmGetPartOffscreenIndices(const csmModel* model)
{
    const csmInt32 partCount = ::csmGetPartCount(model);
    s_partOffscreenIndices.assign(GetSafeCount(partCount), kNoOffscreenIndex);
    return s_partOffscreenIndices.empty() ? &kNoOffscreenIndex : s_partOffscreenIndices.data();
}

csmInt32 csmGetOffscreenCount(const csmModel* model)
{
    (void)model;
    return 0;
}

const csmInt32* const* csmGetOffscreenMasks(const csmModel* model)
{
    (void)model;
    return kDummyMasks;
}

const csmInt32* csmGetOffscreenMaskCounts(const csmModel* model)
{
    (void)model;
    return kDummyMaskCounts;
}

const csmInt32* csmGetOffscreenOwnerIndices(const csmModel* model)
{
    (void)model;
    return kDummyOwnerIndices;
}

const csmVector4* csmGetOffscreenMultiplyColors(const csmModel* model)
{
    (void)model;
    return kDummyMultiplyColors;
}

const csmVector4* csmGetOffscreenScreenColors(const csmModel* model)
{
    (void)model;
    return kDummyScreenColors;
}

const csmFloat32* csmGetOffscreenOpacities(const csmModel* model)
{
    (void)model;
    return kDummyOpacities;
}

const csmByte* csmGetOffscreenConstantFlags(const csmModel* model)
{
    (void)model;
    return kDummyConstantFlags;
}

const csmInt32* csmGetOffscreenBlendModes(const csmModel* model)
{
    (void)model;
    return kDummyBlendModes;
}

}}}
