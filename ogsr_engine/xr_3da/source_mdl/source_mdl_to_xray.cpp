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
// Build engine CBoneData records (caller owns the returned pointers; true on success).
//
// The engine consumes the tree via CBoneData::CalculateM2B, which accumulates
//   acc[child] = mul_43(acc[parent], bind[child])   (Fmatrix::mul_43(A,B) == B*A, see _matrix.h)
// and then m2b = inverse(acc). So bind_transform MUST be the LOCAL (parent-relative) transform,
// and the whole hierarchy MUST be a single tree (CalculateM2B recurses from one root).
//
// We therefore:
//   * compute each bone's model-space frame in the engine frame:  mx[i] = model_bind_source[i] * basis;
//   * UNIFY multi-root skeletons into a single tree rooted at the primary bone (index 0 when it is a
//     root, else the smallest root index) by re-parenting the other roots under it, preserving each
//     bone's model-space frame via exact inverse composition:
//         bind[primary] = mx[primary];
//         bind[child]   = mx[child] * inverse(mx[parent]);   // local, parent-relative
//   * let the engine's CalculateM2B recover the correct model frame for EVERY bone.
//
// No new bones are added and existing bone indices are NOT renumbered, so the vertex weights
// (global Source bone indices from .vtx) stay valid with the identity boneMap.
bool BuildEngineSkeleton(const CSourceMdlSkeleton& src, vecBones& outBones, int& outRoot,
                         const Fmatrix& basis)
{
    outBones.clear();
    outRoot = -1;

    const auto& bones = src.GetBones();
    const auto& roots = src.GetRootMulti();
    const std::size_t n = bones.size();
    if (n == 0)
        return false;

    // Primary (engine) root: prefer index 0 (the engine's Spawn() calls LL_SetBoneRoot(0)),
    // otherwise the smallest root index.
    int primary = -1;
    for (int r : roots)
        if (primary < 0 || r < primary)
            primary = r;
    if (primary < 0)
        primary = 0;
    if (roots.size() >= 1 && roots[0] == 0) // if bone 0 is a root, it's the engine root
        primary = 0;

    // Allocate bones.
    outBones.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        outBones.push_back(xr_new<CBoneData>(static_cast<u16>(i)));

    // model-space frame in the engine (row-vector) system: mx[i] = model_bind_source[i] * basis.
    std::vector<Fmatrix> mx(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        Fmatrix model;
        CopyBind(bones[i].model_bind, model);
        mx[i].mul_43(basis, model); // == model * basis
    }

    // Fill names, hierarchy and (LOCAL) bind transforms.
    for (std::size_t i = 0; i < n; ++i)
    {
        const BONE& s = bones[i];
        CBoneData* B = outBones[i];

        B->name = s.name.c_str();

        // New parent in the unified single-root tree.
        const int parent = (s.is_root) ? ((static_cast<int>(i) == primary) ? -1 : primary)
                                       : s.parent;

        if (parent >= 0)
        {
            Fmatrix inv;
            Fmatrix tmp;
            if (tmp.invert_b(mx[static_cast<std::size_t>(parent)]))
                inv.set(tmp);
            else
                inv.set(Fidentity); // деградация для вырожденной матрицы (не должно случаться)
            // bind = mx[child] * inv(mx[parent])  ->  mul_43(inv, mx[child]).
            B->bind_transform.mul_43(inv, mx[i]);

            B->SetParentID(static_cast<u16>(parent));
            outBones[static_cast<std::size_t>(parent)]->children.push_back(B);
        }
        else
        {
            B->bind_transform.set(mx[i]); // единственный корень: локальная = модельная
            B->SetParentID(BI_NONE);
        }

        // Local euler/linear pose (now genuinely local to the parent).
        B->bind_transform.getXYZi(B->rotation);
        B->bind_transform.getXYZi(B->position);

        // Harmless-but-valid defaults for the rest of CBoneData.
        B->shape.Reset();
        B->IK_data.Reset();
        B->mass = 1.f;
        B->center_of_mass.set(s.pos.x, s.pos.y, s.pos.z);
        B->game_mtl_name = "default_object";
        B->game_mtl_idx = 0;
    }

    // Model->bone (m2b) transforms after the whole single-root tree exists.
    if (primary >= 0 && static_cast<std::size_t>(primary) < n)
        outBones[static_cast<std::size_t>(primary)]->CalculateM2B(Fidentity);

    outRoot = primary;
    return true;
}
} // namespace SourceMdl
