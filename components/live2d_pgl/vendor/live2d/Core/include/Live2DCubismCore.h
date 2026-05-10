#ifndef LIVE2D_CUBISM_CORE_H
#define LIVE2D_CUBISM_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t csmByte;
typedef uint8_t csmMocVersion;
typedef unsigned int csmUint32;
typedef uint64_t csmUint64;
typedef unsigned int csmSizeInt;
typedef signed int csmInt32;
typedef unsigned int csmVersion;

typedef void (*csmLogFunction)(const char *message);

typedef struct csmVector2 {
    float X;
    float Y;
} csmVector2;

typedef struct csmMoc csmMoc;
typedef struct csmModel csmModel;

typedef struct csmDebugMocMemoryInfo {
    csmUint64 mocBytes;
    csmInt32 partCount;
    csmInt32 drawableCount;
    csmInt32 parameterCount;
    csmUint64 vertexFloatCount;
    csmUint64 indexCount;
    csmUint64 maskCount;
    csmUint64 parameterKeyValueCountRaw;
    csmUint64 parameterKeyValueCountClamped;
    csmUint32 parameterKeyCountReadableEntries;
    csmUint32 parameterKeyCountOverlapEntries;
    csmUint64 modelStructBytes;
    csmUint64 modelArenaBytesPreScaleRaw;
    csmUint64 modelArenaBytesPreScaleClamped;
    csmUint64 modelArenaBytesReturned;
    csmUint64 modelArenaBytesReturnedClamped;
    bool hasParameterKeyCountOverlap;
} csmDebugMocMemoryInfo;

typedef struct csmDebugModelMemoryInfo {
    csmUint64 mocBytes;
    csmUint64 arenaBytesRequested;
    csmUint64 arenaBytesUsed;
    csmUint64 heapSortBytes;
    csmUint64 heapParamEvalBytes;
    csmUint64 heapEvalBytes;
    csmUint64 heapGroupBytes;
    csmUint64 heapRotationBytes;
    csmUint64 heapDrawableBytes;
    csmUint64 heapDeformerBytes;
    csmUint64 heapLateBytes;
    csmUint64 heapPairBlendBytes;
    csmUint64 heapDebugBytes;
    csmUint64 heapOtherBytes;
    csmUint64 heapTotalBytes;
} csmDebugModelMemoryInfo;

csmVersion csmGetVersion(void);
csmMocVersion csmGetLatestMocVersion(void);
csmMocVersion csmGetMocVersion(const csmByte *address);
bool csmHasMocConsistency(const csmByte *address, csmSizeInt size);
csmMoc *csmReviveMocInPlace(csmByte *address, csmSizeInt size);

csmLogFunction csmGetLogFunction(void);
void csmSetLogFunction(csmLogFunction logFunction);

csmSizeInt csmGetSizeofModel(const csmMoc *moc);
csmModel *csmInitializeModelInPlace(const csmMoc *moc, void *address, csmSizeInt size);
void csmUpdateModel(csmModel *model);
void csmResetDrawableDynamicFlags(csmModel *model);
void csmReadCanvasInfo(const csmModel *model, csmVector2 *sizeInPixels, csmVector2 *originInPixels, float *pixelsPerUnit);

csmInt32 csmGetParameterCount(const csmModel *model);
const char *const *csmGetParameterIds(const csmModel *model);
const csmInt32 *csmGetParameterTypes(const csmModel *model);
const float *csmGetParameterMinimumValues(const csmModel *model);
const float *csmGetParameterMaximumValues(const csmModel *model);
const float *csmGetParameterDefaultValues(const csmModel *model);
const float *csmGetParameterValues(const csmModel *model);
const csmByte *csmGetParameterRepeats(const csmModel *model);
const csmInt32 *csmGetParameterKeyCounts(const csmModel *model);
const float *const *csmGetParameterKeyValues(const csmModel *model);

csmInt32 csmGetPartCount(const csmModel *model);
const char *const *csmGetPartIds(const csmModel *model);
const float *csmGetPartOpacities(const csmModel *model);
const csmInt32 *csmGetPartParentPartIndices(const csmModel *model);

csmInt32 csmGetDrawableCount(const csmModel *model);
const char *const *csmGetDrawableIds(const csmModel *model);
const csmByte *csmGetDrawableConstantFlags(const csmModel *model);
const csmByte *csmGetDrawableDynamicFlags(const csmModel *model);
const csmInt32 *csmGetDrawableTextureIndices(const csmModel *model);
const csmInt32 *csmGetDrawableDrawOrders(const csmModel *model);
const csmInt32 *csmGetDrawableRenderOrders(const csmModel *model);
const float *csmGetDrawableOpacities(const csmModel *model);
const csmInt32 *csmGetDrawableMaskCounts(const csmModel *model);
const csmInt32 *const *csmGetDrawableMasks(const csmModel *model);
const csmInt32 *csmGetDrawableVertexCounts(const csmModel *model);
const float *const *csmGetDrawableVertexPositions(const csmModel *model);
const float *const *csmGetDrawableVertexUvs(const csmModel *model);
const csmInt32 *csmGetDrawableIndexCounts(const csmModel *model);
const uint16_t *const *csmGetDrawableIndices(const csmModel *model);
const float *const *csmGetDrawableMultiplyColors(const csmModel *model);
const float *const *csmGetDrawableScreenColors(const csmModel *model);
const csmInt32 *csmGetDrawableParentPartIndices(const csmModel *model);

bool csmDebugGetMocMemoryInfo(const csmMoc *moc, csmDebugMocMemoryInfo *outInfo);
bool csmDebugGetModelMocMemoryInfo(const csmModel *model, csmDebugMocMemoryInfo *outInfo);
bool csmDebugGetModelMemoryInfo(const csmModel *model, csmDebugModelMemoryInfo *outInfo);

#ifdef __cplusplus
}
#endif

#endif
