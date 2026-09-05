//--------------------------------------------------------------------------------------------------
// source_mdl_mesh_to_xray.cpp -- движковый упаковщик Source-вершин в X-Ray vertex.
//--------------------------------------------------------------------------------------------------
#include "stdafx.h"
#include "source_mdl_mesh_to_xray.h"
#include <cmath>

namespace SourceMdl
{
namespace
{
// Устаревание: gейте примитивную ортонормальную базу (T/B) из нормали, когда меш Source
// отдаёт только позицию/нормаль/UV (в классическом inline-формате касательных нет).
void BuildTangentBasis(const Fvector& N, Fvector& T, Fvector& B)
{
    Fvector up(0.f, 1.f, 0.f);
    if (std::fabs(N.dotproduct(up)) > 0.99f)
        up.set(1.f, 0.f, 0.f);
    T.crossproduct(up, N);
    T.normalize_safe();
    B.crossproduct(N, T);
    B.normalize_safe();
}
} // namespace

bool BuildXRayMesh(const MESH& src, const int* boneMap, const Fmatrix& basis,
                   std::vector<vertBoned4W>& outVerts, std::vector<u16>& outIndices)
{
    outVerts.clear();
    outIndices.clear();
    if (src.vertices.empty())
        return false;

    outVerts.reserve(src.vertices.size());
    outIndices.reserve(src.triangles.size() * 3);

    for (const MESH_VERTEX& sv : src.vertices)
    {
        vertBoned4W dv{};

        // Позиция/нормаль через базис (det == +1, нормаль не инвертируем).
        dv.P.set(sv.pos.x, sv.pos.y, sv.pos.z);
        basis.transform_tiny(dv.P);
        dv.N.set(sv.normal.x, sv.normal.y, sv.normal.z);
        basis.transform_dir(dv.N);
        BuildTangentBasis(dv.N, dv.T, dv.B);
        dv.u = sv.u;
        dv.v = sv.v;

        // Нормализовать веса (до 4 влияний) и переиндексировать кости.
        // sv.bone[i] — ГЛАВАЛЬНЫЙ индекс кости Source (0..numbones-1, как в .mdl/.vtx).
        // boneMap — переход Source-индекс -> движковый (OGSR) индекс: тот же порядок, что
        // в vecBones/векторах из BuildEngineSkeleton; -1 => кость не найдена.
        float wt[4];
        int bi[4];
        for (int i = 0; i < 4; ++i)
        {
            wt[i] = (i < 3 && i < sv.num_weights) ? sv.weight[i] : 0.f;
            bi[i] = -1;
            if (i < 3 && i < sv.num_weights && sv.bone[i] >= 0)
            {
                const int mapped = boneMap ? boneMap[sv.bone[i]] : sv.bone[i];
                bi[i] = mapped;
            }
        }
        const int n = NormalizeWeights(wt, bi, sv.num_weights);

        // По умолчанию "ничего" -> кость 0.
        for (int i = 0; i < 4; ++i)
        {
            if (i >= n || bi[i] < 0)
            {
                dv.m[i] = 0;
                dv.w[i] = (i == 0) ? 1.f : 0.f;
                bi[i] = 0;
            }
            else
            {
                dv.m[i] = static_cast<u16>(bi[i]);
                dv.w[i] = wt[i];
            }
        }
        outVerts.push_back(dv);
    }

    for (const MESH_TRIANGLE& t : src.triangles)
    {
        if (t.a < outVerts.size() && t.b < outVerts.size() && t.c < outVerts.size())
        {
            outIndices.push_back(static_cast<u16>(t.a));
            outIndices.push_back(static_cast<u16>(t.b));
            outIndices.push_back(static_cast<u16>(t.c));
        }
    }

    return !outVerts.empty() && !outIndices.empty();
}
} // namespace SourceMdl
