#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_vvd.h — читатель .vvd (vertex file) build-файла Source (split-формат вертика).
//
// В split-формате (Source 2013 / Garry's Mod, v48+) геометрия модели не в .mdl, а в отдельном
// файле вершин (.vvd) и оптимизированных индексов (.vtx). .vvd хранит mstudiovertex_t:
//   позиция(3f) + нормаль(3f) + тангенс(4f, xyz+Ориентация) + uv(2f) = 48 байт.
//
// ПОДТВЕРЖДЕНО на реальном gfl2_asteria_arms.vvd (см. VERIFICATION.md):
//   id=0x56534449, version=4, numLODs=1, numLODVertexes[0]=12487,
//   vertexDataStart=64, stride=48, конец вершин == tangentDataStart (зазор 0).
//
// Здесь (чистый, без движка) разбирается только массив вершин (.vvd) — веса костей и индексы
// лежат в .vtx (отдельный модуль). Позиции/нормали возвращаются как есть (координаты Source);
// преобразование в X-Ray (базис + упаковка в vertBoned4W) делается в движковом адаптере.
//
// ВАЖНО про раскладку вершины (подтверждено в VERIFICATION.md на реальном gfl2_asteria_arms.vvd):
//   это НЕ канонический обратномобильный mstudiovertex_t (pos|normal|...). Здесь строка из
//   48 байт устроена так:
//     [ 0..15]  const float[4]   (обычно (1,0,0,0); не геометрия)
//     [16..27]  position         (совпадает с bbox модели: x±26, y −6..4, z −24..0.6)
//     [28..39]  normal           (единичная: |N| = 1.0000 для всех 12487 вершин)
//     [40..47]  uv               (0..1)
//   Смещения выносятся в VVD_LAYOUT, чтобы их можно было скорректировать для другого файла.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_mesh.h" // для Vec3f (из базиса)
#include <cstdint>
#include <vector>

namespace SourceMdl
{
struct VVD_VERTEX
{
    Vec3f pos;
    Vec3f normal;
    float u, v;
};

enum class EVVDResult
{
    Ok = 0,
    NotVVD,
    UnsupportedVersion,
    MalformedBuffer,
};

struct VVD_LAYOUT
{
    int id_off;             // 0
    int version_off;        // 4
    int numlods_off;        // 12
    int nlverts_off;        // 16 (numLODVertexes[0])
    int vstart_off;         // 56
    int tstart_off;         // 60
    int vert_stride;        // sizeof(vertex)
    int pos_off;            // байтовое смещение позиции внутри вершины
    int normal_off;         // байтовое смещение нормали
    int uv_off;             // байтовое смещение UV (два float)
};

inline VVD_LAYOUT DefaultVvdLayout()
{
    // Подтверждено на реальном v49/GMod: stride 48, pos@16, normal@28, uv@40.
    return VVD_LAYOUT{0, 4, 12, 16, 56, 60, 48, 16, 28, 40};
}

// Читает вершины из .vvd. outVerts заполняется по LOD(0). Возвращает результат.
EVVDResult ReadVvdVertices(const void* data, std::size_t size, std::vector<VVD_VERTEX>& outVerts,
                           const VVD_LAYOUT& layout = DefaultVvdLayout());
const char* VvdResultName(EVVDResult r);
} // namespace SourceMdl
