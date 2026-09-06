#pragma once
//--------------------------------------------------------------------------------------------------
// Source engine (Valve Source SDK, as used by e.g. Garry's Mod) skeleton reader for X-Ray/OGSR.
//
// ROUND 1 -- "reader + validation layer".
//
// Goal of the series: let OGSR load a skeletal model that was authored for the *Source* engine
// (a Valve .MDL of a NPC/player/physics character, e.g. from Garry's Mod) and drive X-Ray
// skinning with it.
//
// The .MDL (StudioMDL) format is a binary container described by the `studiohdr_t` layout below.
// This file implements ONLY the skeleton part we need:
//    * studiohdr_t				-- header (id, version, bone count, bone offset, ...)
//    * mstudiobone_t			-- one bone (name, parent index, position, quaternion, flags)
//
// Everything is exposed in plain-standard types so this unit can be compiler-independent and
// unit-tested without pulling in the whole X-Ray build.
//
// The output is a bone hierarchy + bind pose, ready to be converted into OGSR CBoneData/vecBones
// by source_mdl_to_xray.cpp, which then plugs into CKinematics (see SkeletonCustom.cpp Load()).
//
// NOTE ON CONVENTIONS / coordinate systems:
//    Source models are left-handed, Y-up. X-Ray/OGSR is right-handed Z-up. The raw local
//    bone transforms parsed here are correct *within the Source frame*. A configurable axis
//    conversion stage lives in the adapter (source_mdl_to_xray.cpp) and is documented there.
//    This reader stays "frame-agnostic" on purpose: it reproduces the data faithfully.
//--------------------------------------------------------------------------------------------------

#include <string>
#include <vector>
#include <cstdint>

namespace SourceMdl
{
// ------------------------------------------------------------------------------------------------
// Minimal linear-algebra types (float, row-major for matrices in the 4x4 case).
// Kept local so the reader has zero engine dependencies and is unit-testable.
// ------------------------------------------------------------------------------------------------
struct vec3
{
    float x, y, z;
};

struct quat
{
    float x, y, z, w;
};

struct mat4
{
    float m[4][4]; // row-major
};

// ------------------------------------------------------------------------------------------------
// The result of parsing one bone.
// ------------------------------------------------------------------------------------------------
struct BONE
{
    std::string name;       // lowercase, as X-Ray expects bone names
    int parent;             // index of parent bone, or -1 for root
    bool is_root;           // true if parent == -1

    // local-to-parent transform, exactly as stored in the Source file
    vec3 pos;               // bone position relative to parent
    quat rotation;          // bone rotation (quaternion) relative to parent
    vec3 pos_scale;         // bone position scale (usually 1,1,1)
    vec3 rot_scale;         // bone rotation scale (usually 1,1,1)
    std::uint32_t flags;    // mstudiobone_t::flags

    // computed: the bone's own frame in model space (accumulated down the hierarchy).
    //   model_bind = parent_model_bind * local(pos, quat)   (row-major -> "bone->model" style
    //   composition; see ComputeBindMatrices for the exact ordering so it matches the engine
    //   convention used by GetBoneBindMatrix()).
    mat4 model_bind;
};

//! Studio header magic IDs and the version range we accept.
struct Constants
{
    // 'IDST' read as little-endian u32. Verified against a real Source v49 model: bytes 49 44 53 54
    // = 'I','D','S','T', little-endian u32 = 0x54534449.
    static constexpr std::uint32_t kMDLID = 0x54534449;
    static constexpr std::uint32_t kMDLVERSION_MIN = 44;
    static constexpr std::uint32_t kMDLVERSION_MAX = 49;
};

// ------------------------------------------------------------------------------------------------
// Parser.
// ------------------------------------------------------------------------------------------------
class CSourceMdlSkeleton
{
public:
    //! Parse the studiohdr_t + bone array from a raw memory buffer.
    //! Returns false and leaves a diagnostic in GetLastError() on malformed data.
    bool Parse(const void* data, std::size_t size);

    const std::vector<BONE>& GetBones() const { return m_bones; }
    int RootIndex() const { return m_root; }
    const std::string& GetLastError() const { return m_error; }

    //! Rebuild m_bones[i].model_bind from the local pos/quat + parent hierarchy.
    //! (Idempotent; called automatically at the end of Parse().)
    void ComputeBindMatrices();

    //! Sanity helper used by tests / console dumps: print a bone dump to stdout.
    void Dump() const;

private:
    std::vector<BONE> m_bones;
    int m_root = -1;
    std::string m_error;

    bool ReadName(const std::uint8_t* base, std::uint32_t offset, std::string& out,
                  std::size_t size) const;
};

// ------------------------------------------------------------------------------------------------
// The engine-facing conversion is implemented in source_mdl_to_xray.cpp. It consumes a
// CSourceMdlSkeleton and produces X-Ray's CBoneData tree / vecBones.
// ------------------------------------------------------------------------------------------------
} // namespace SourceMdl
