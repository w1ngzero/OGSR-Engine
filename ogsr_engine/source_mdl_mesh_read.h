#include "stdafx.h"
//--------------------------------------------------------------------------------------------------
// source_mdl_mesh.cpp -- реализация чистой части меш-модуля (см. source_mdl_mesh.h).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_mesh.h"

namespace SourceMdl
{
int NormalizeWeights(float weight[4], int bone[4], int in_count)
{
    if (in_count < 1)
        return 0;

    if (in_count > 4)
        in_count = 4;

    float sum = 0.f;
    for (int i = 0; i < in_count; ++i)
        sum += weight[i];

    if (sum <= 0.f)
    {
        // Нет корректных весов — привязываем всё к первой кости.
        for (int i = 0; i < in_count; ++i)
        {
            weight[i] = (i == 0) ? 1.f : 0.f;
            if (bone[i] < 0)
                bone[i] = 0;
        }
        return 1;
    }

    for (int i = 0; i < in_count; ++i)
        weight[i] /= sum;

    return in_count;
}

void ApplyBasisToVertex(const Basis3& basis, Vec3f& pos, Vec3f& normal)
{
    pos = Transform(basis, pos);
    normal = Transform(basis, normal);
    // det == +1 -> не нужно менять знак нормали; при переходе на рефлексию (det<0)
    // здесь пришлось бы делать normal *= -1 и реверсить треугольники.
}

bool BuildTriangles(const std::uint32_t* indices, std::size_t count, MESH& out)
{
    if (!indices || count % 3 != 0)
        return false;
    out.triangles.reserve(out.triangles.size() + count / 3);
    for (std::size_t i = 0; i < count; i += 3)
    {
        const std::uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        out.triangles.push_back({a, b, c});
    }
    return true;
}
} // namespace SourceMdl
