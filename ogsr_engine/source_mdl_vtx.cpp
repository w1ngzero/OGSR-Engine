#include "stdafx.h"
//--------------------------------------------------------------------------------------------------
// source_mdl_split_mesh.cpp — сборка скин-меша из .vvd + .vtx (split-формат Source).
// Чистый модуль (без X-Ray), результат — MESH в Source-конвенции.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_split_mesh.h"

namespace SourceMdl
{
bool BuildSplitMesh(const std::vector<VVD_VERTEX>& vvdVerts,
                    const std::vector<VTX_MESH>& vtxMeshes,
                    std::vector<MESH>& outMeshes)
{
    outMeshes.clear();

    // Проверка: сумма вершин всех мешей .vtx должна равняться числу вершин .vvd.
    std::size_t totalVtx = 0;
    for (const auto& vm : vtxMeshes)
        totalVtx += vm.verts.size();
    if (vvdVerts.empty() || vtxMeshes.empty() || totalVtx != vvdVerts.size())
        return false;

    std::size_t base = 0; // накопленный сдвиг текущего меша в .vvd
    for (const auto& vm : vtxMeshes)
    {
        MESH out;
        out.vertices.reserve(vm.verts.size());
        out.triangles.reserve(vm.triangles.size());

        for (const auto& vtxVer : vm.verts)
        {
            // vvd-индекс = base + origMeshVertID (проверенный маппинг split-формата).
            const std::size_t vvdIdx = base + static_cast<std::size_t>(vtxVer.origMeshVertID);
            if (vvdIdx >= vvdVerts.size())
                return false; // деформированные данные — не собирать мусор
            const VVD_VERTEX& vd = vvdVerts[vvdIdx];

            MESH_VERTEX mv;
            mv.pos = vd.pos;
            mv.normal = vd.normal;
            mv.u = vd.u;
            mv.v = vd.v;

            // Веса: boneID — ГЛАВАЛЬНЫЙ индекс (HWSKINNED), доля = 1/numBones.
            const int nb = vtxVer.numBones > 0 ? vtxVer.numBones : 1;
            int n = 0;
            for (int i = 0; i < 3 && i < nb; ++i)
            {
                mv.bone[i] = vtxVer.boneID[i];
                mv.weight[i] = 1.0f / static_cast<float>(nb);
                ++n;
            }
            mv.num_weights = n;
            out.vertices.push_back(mv);
        }

        for (const auto& t : vm.triangles)
            out.triangles.push_back(MESH_TRIANGLE{t.a, t.b, t.c});

        outMeshes.push_back(std::move(out));
        base += vm.verts.size();
    }

    return true;
}
} // namespace SourceMdl
