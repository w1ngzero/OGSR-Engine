//--------------------------------------------------------------------------------------------------
// source_mdl_to_xray.cpp
//
// Adapter: Source .MDL skeleton  ->  OGSR/X-Ray CBoneData tree (vecBones).
//
// This is the layer that actually "hands" a Source skeleton to the engine. It allocates CBoneData
// for every Source bone, preserves the parent/children hierarchy, and computes each bone's
// bind_transform in the ENGINE's convention.
//
// COORDINATE-SYSTEM NOTE (important):
//   Source models are left-handed, Y-up. OGSR/X-Ray is right-handed, Z-up. A pure rotation can NOT
//   turn a left-handed frame into a right-handed one -- you need a reflection (mirror one axis),
//   which also flips triangle winding and normals.
//
//   We therefore let the caller inject a `basis` (a Fmatrix mapping Source model-space points into
//   X-Ray model-space points). Because it is a single rigid change-of-basis it is applied ON THE
//   LEFT of every bone's model-space matrix:
//       bind_xray = basis * model_bind_source
//   (both store translation in the LAST ROW / row index 3, so we can copy straight through).
//   This keeps the whole skeleton coherent. The SAME `basis` must later be applied to the mesh
//   vertices by the geometry importer -- we deliberately live in one shared place so the two can
//   never disagree.
//
//   With the default `basis == identity`, the Source frame is interpreted "as if" it were the
//   X-Ray frame: enough to get a skeleton into memory and pose it, but a real model needs the
//   SAME basis on the mesh. This is a Round-3 milestone.
//--------------------------------------------------------------------------------------------------
#include "stdafx.h"
#include "source_mdl_skeleton.h"
#include "../bone.h"

namespace SourceMdl
{
// Copy the reader's model-space bind matrix (row-major, translation in row 3) into an X-Ray Fmatrix
// -- the two share the same convention (X-Ray is also row-vector with translation in the last row).
static void CopyBind(const mat4& src, Fmatrix& dst)
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            dst.m[r][c] = src.m[r][c];
}

// Build engine CBoneData records (caller owns the returned pointers; true on success).
//   src       -- parsed Source skeleton (see source_mdl_skeleton.h)
//   outBones  -- filled with new CBoneData* in the SAME index order as src.GetBones()
//   outRoot   -- index of the root bone, or -1
//   basis     -- change-of-basis from Source model space to X-Ray model space (see note above)
bool BuildEngineSkeleton(const CSourceMdlSkeleton& src, vecBones& outBones, int& outRoot,
                         const Fmatrix& basis)
{
    outBones.clear();
    outRoot = -1;

    const auto& bones = src.GetBones();
    if (bones.empty())
        return false;

    // Allocate engine bones.
    outBones.reserve(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i)
        outBones.push_back(xr_new<CBoneData>(static_cast<u16>(i)));

    // Fill names, bind transforms and hierarchy. bind_transform already carries the Source
    // model-space frame + change-of-basis, so it is consistent for the whole skeleton.
    for (std::size_t i = 0; i < bones.size(); ++i)
    {
        const BONE& s = bones[i];
        CBoneData* B = outBones[i];

        B->name = s.name.c_str();

        // bind_xray = basis * model_bind_source  (translation row-3 in both).
        Fmatrix model;
        CopyBind(s.model_bind, model);
        B->bind_transform.mul_43(basis, model);

        // Hierarchy.
        if (s.is_root)
        {
            outRoot = static_cast<int>(i);
            B->SetParentID(BI_NONE);
        }
        else
        {
            CBoneData* P = outBones[static_cast<std::size_t>(s.parent)];
            B->SetParentID(static_cast<u16>(s.parent));
            P->children.push_back(B);
        }

        // Decompose the rest pose into the classic X-Ray euler/linear form too, so both the
        // "raw matrix" consumers and the "classic params" consumers agree on the same pose.
        B->bind_transform.getXYZi(B->rotation);
        B->bind_transform.getXYZi(B->position);

        // Harmless-but-valid defaults for the rest of CBoneData.
        B->shape.Reset();
        B->IK_data.Reset();
        B->mass = 1.f;
        B->center_of_mass.set(s.pos.x, s.pos.y, s.pos.z);
        B->game_mtl_name = "default";
        B->game_mtl_idx = 0;
    }

    // Model->bone (m2b) transforms after the whole tree exists.
    if (outRoot >= 0)
        outBones[static_cast<std::size_t>(outRoot)]->CalculateM2B(Fidentity);

    return true;
}
} // namespace SourceMdl
