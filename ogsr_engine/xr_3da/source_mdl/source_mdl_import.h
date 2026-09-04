#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_import.h -- Engine-side entry point for importing a Source .MDL skeleton into OGSR.
//
// This is the "small, engine-level" API that CKinematics::Load() calls. It:
//   * locates a companion "<model>.mdl" next to the requested model in game_meshes,
//   * parses it with the reader (source_mdl_skeleton.h),
//   * converts it to X-Ray CBoneData via the adapter (source_mdl_to_xray.cpp),
//   * and reports success/failure so the caller can fall back gracefully.
//
// A global switch `psSourceSkeletonMode` (console command: rs_source_skeleton) turns the feature on;
// it is defined here so both xr_3da and Layers/xrRender can see it.
//--------------------------------------------------------------------------------------------------
#include "..\\bone.h"
#include <vector>
#include <cstdint>

namespace SourceMdl
{
// 0 = feature off (default, vanilla behaviour), 1 = attempt Source skeleton import when the OGF
// has no X-Ray bone names chunk. Declared here, defined in source_mdl_import.cpp.
extern ENGINE_API int psSourceSkeletonMode;

// 0 = off (default), 1 = also attach the Source skinned mesh as the geometry child of the imported
// skeleton (in addition to the skeleton). Declared here, defined in source_mdl_import.cpp.
extern ENGINE_API int psSourceMeshMode;

//! Try to load a Source .MDL skeleton for `modelName` and convert it into engine bones.
//! Fills outBones (index order matches the .mdl) and outRoot (or -1).
//! Returns false (with a log message) if there is no companion .mdl or it failed to parse.
bool TryImportSourceSkeleton(const char* modelName, vecBones& outBones, int& outRoot);

// ---- Round-5: engine-side mesh import (split-format .vvd + .vtx) ----
// Результат импорта геометрии Source-модели в движковый формат. Вершины/индексы уже в конвенции
// X-Ray: применён базис, веса нормализованы, кости переиндексированы в вектора движка через
// boneMap. Готов к созданию DX-меша (vertBoned4W + u16-индексы).
struct ENGINE_API SourceMeshImport
{
    std::vector<vertBoned4W> verts; // скин-вершины (до 4 влияний), X-Ray-конвенция
    std::vector<u16> indices;       // треугольники (по 3 индекса)
    std::vector<int> boneMap;       // Source bone index -> engine bone index (обычно identity)
    Fsphere sphere;                 // общий охват (X-Ray-конвенция)
    int root = -1;                  // индекс корневой кости (как в скелете)
    bool loaded = false;
    u32 numTriangles = 0;
};

//! Try to load the skinned mesh for `modelName` from companion .vvd + .vtx (+ .mdl skeleton for
//! the root/bone count). Fills out. Applies the shared Source->X-Ray basis. Returns true on success.
bool TryImportSourceMesh(const char* modelName, SourceMeshImport& out);

//! Serialize an imported skinned mesh into a full OGF stream that a CSkeletonX_ST child visual can
//! load (see FSkinned.cpp CSkeletonX_ST::Load). Chunks: OGF_HEADER (type = MT_SKELETON_GEOMDEF_ST),
//! OGF_TEXTURE (shader names, for a renderable material), OGF_VERTICES (fvf=OGF_VERTEXFORMAT_FVF_4L,
//! vCount, vertBoned4W[] — raw layout) and OGF_INDICES (iCount, u16[]).
//!   textureName / shaderName — material targets for OGF_TEXTURE. NOTE: the chunk payload is written
//!   in the engine's read order (textureName\0 then shaderName\0), matching dxRender_Visual::Load's
//!   r_stringZ(fnT)/r_stringZ(fnS). May be empty (then the child visual gets no material -> use with
//!   caution, a null shader can break rendering). Returns false on empty geometry.
bool BuildSourceMeshOGFStream(const SourceMeshImport& imp, const char* textureName, const char* shaderName,
                              std::vector<std::uint8_t>& outBytes);
} // namespace SourceMdl
