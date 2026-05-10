/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "Live2DCubismCore.h"

/**
 * Header to include the Live2D Cubism Core in the `Live2D::Cubism::Core` namespace.
 *
 * This project uses a recovered Core header that only exposes a subset of the
 * official SDK surface. The Framework still expects the full namespaced API, so
 * this wrapper bridges the recovered C API to the C++ namespace the Framework
 * uses and fills the small compatibility gaps that Linux/PortableGL build needs.
 */
namespace Live2D { namespace Cubism { namespace Core {

using ::csmByte;
using ::csmMocVersion;
using ::csmUint32;
using ::csmUint64;
using ::csmSizeInt;
using ::csmInt32;
using ::csmVersion;
using ::csmLogFunction;
using ::csmVector2;
using ::csmMoc;
using ::csmModel;
using ::csmDebugMocMemoryInfo;
using ::csmDebugModelMemoryInfo;

using csmBool = bool;
using csmUint8 = uint8_t;
using csmUint16 = uint16_t;
using csmFloat32 = float;
using csmFlags = csmByte;
using csmParameterType = csmInt32;

struct csmVector4
{
    csmFloat32 X;
    csmFloat32 Y;
    csmFloat32 Z;
    csmFloat32 W;
};

static constexpr csmUint32 csmAlignofMoc = 64u;
static constexpr csmUint32 csmAlignofModel = 16u;

enum : csmInt32
{
    csmColorBlendType_Normal = 0,
    csmColorBlendType_Add = 1,
    csmColorBlendType_AddGlow = 2,
    csmColorBlendType_Darken = 3,
    csmColorBlendType_Multiply = 4,
    csmColorBlendType_ColorBurn = 5,
    csmColorBlendType_LinearBurn = 6,
    csmColorBlendType_Lighten = 7,
    csmColorBlendType_Screen = 8,
    csmColorBlendType_ColorDodge = 9,
    csmColorBlendType_Overlay = 10,
    csmColorBlendType_SoftLight = 11,
    csmColorBlendType_HardLight = 12,
    csmColorBlendType_LinearLight = 13,
    csmColorBlendType_Hue = 14,
    csmColorBlendType_Color = 15,
    csmColorBlendType_AddCompatible = 16,
    csmColorBlendType_MultiplyCompatible = 17
};

enum : csmInt32
{
    csmAlphaBlendType_Over = 0,
    csmAlphaBlendType_Atop = 1,
    csmAlphaBlendType_Out = 2,
    csmAlphaBlendType_ConjointOver = 3,
    csmAlphaBlendType_DisjointOver = 4
};

enum : csmFlags
{
    csmBlendAdditive = 0x01,
    csmBlendMultiplicative = 0x02,
    csmIsDoubleSided = 0x04,
    csmIsInvertedMask = 0x08,
    csmIsVisible = 0x01,
    csmVisibilityDidChange = 0x02,
    csmOpacityDidChange = 0x04,
    csmDrawOrderDidChange = 0x08,
    csmRenderOrderDidChange = 0x10,
    csmVertexPositionsDidChange = 0x20,
    csmBlendColorDidChange = 0x40
};

using ::csmGetVersion;
using ::csmGetLatestMocVersion;
using ::csmGetLogFunction;
using ::csmSetLogFunction;
using ::csmGetSizeofModel;
using ::csmInitializeModelInPlace;
using ::csmUpdateModel;
using ::csmResetDrawableDynamicFlags;
using ::csmReadCanvasInfo;
using ::csmGetParameterCount;
using ::csmGetParameterIds;
using ::csmGetParameterTypes;
using ::csmGetParameterMinimumValues;
using ::csmGetParameterMaximumValues;
using ::csmGetParameterDefaultValues;
using ::csmGetParameterRepeats;
using ::csmGetParameterKeyCounts;
using ::csmGetParameterKeyValues;
using ::csmGetPartCount;
using ::csmGetPartIds;
using ::csmGetPartParentPartIndices;
using ::csmGetDrawableCount;
using ::csmGetDrawableIds;
using ::csmGetDrawableConstantFlags;
using ::csmGetDrawableDynamicFlags;
using ::csmGetDrawableTextureIndices;
using ::csmGetDrawableDrawOrders;
using ::csmGetDrawableRenderOrders;
using ::csmGetDrawableOpacities;
using ::csmGetDrawableMaskCounts;
using ::csmGetDrawableMasks;
using ::csmGetDrawableVertexCounts;
using ::csmGetDrawableIndexCounts;
using ::csmGetDrawableParentPartIndices;
using ::csmDebugGetMocMemoryInfo;
using ::csmDebugGetModelMocMemoryInfo;
using ::csmDebugGetModelMemoryInfo;

csmMocVersion csmGetMocVersion(const void* address, csmSizeInt size);
csmBool csmHasMocConsistency(const void* address, csmSizeInt size);
csmMoc* csmReviveMocInPlace(void* address, csmSizeInt size);
csmFloat32* csmGetParameterValues(csmModel* model);
csmFloat32* csmGetPartOpacities(csmModel* model);

const csmVector2* const* csmGetDrawableVertexPositions(const csmModel* model);
const csmVector2* const* csmGetDrawableVertexUvs(const csmModel* model);
const csmUint16* const* csmGetDrawableIndices(const csmModel* model);
const csmVector4* csmGetDrawableMultiplyColors(const csmModel* model);
const csmVector4* csmGetDrawableScreenColors(const csmModel* model);
const csmInt32* csmGetDrawableBlendModes(const csmModel* model);

const csmInt32* csmGetRenderOrders(const csmModel* model);
const csmInt32* csmGetPartOffscreenIndices(const csmModel* model);

csmInt32 csmGetOffscreenCount(const csmModel* model);
const csmInt32* const* csmGetOffscreenMasks(const csmModel* model);
const csmInt32* csmGetOffscreenMaskCounts(const csmModel* model);
const csmInt32* csmGetOffscreenOwnerIndices(const csmModel* model);
const csmVector4* csmGetOffscreenMultiplyColors(const csmModel* model);
const csmVector4* csmGetOffscreenScreenColors(const csmModel* model);
const csmFloat32* csmGetOffscreenOpacities(const csmModel* model);
const csmByte* csmGetOffscreenConstantFlags(const csmModel* model);
const csmInt32* csmGetOffscreenBlendModes(const csmModel* model);

}}}
