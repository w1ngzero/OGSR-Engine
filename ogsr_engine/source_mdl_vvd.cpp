#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_split_mesh.h — сборка скин-меша из split-формата (.vvd + .vtx) в Source-конвенцию.
//
// В split-формате (Source 2013 / Garry's Mod, v48+) геометрия разнесена:
//   - .vvd — позиции/нормали/UV (плоско, все меши модели подряд);
//   - .vtx — индексы треугольников + костные веса (в кэш-оптимизированном порядке).
//
// СВЯЗКА vtx <-> vvd (подтверждено на реальном gfl2_asteria_arms, см. VERIFICATION.md):
//   Каждый MeshHeader_vtx соответствует мешу модели в том же порядке. Внутри strip-группы
//   вершины лежат в порядке, оптимизированном под кэш, и НЕ совпадают с ".vvd-порядком".
//   Поле Vertex_t::origMeshVertID — это индекс вершины ВНУТРИ своего меша (0..numverts-1),
//   а vvd-индекс = meshBase + origMeshVertID, где:
//        meshBase = СУММА numverts всех предшествующих мешей модели.
//   Для gfl2_asteria_arms: meshBase = {0, 1174, 6708} (0; +5534; +5779 -> сумма 12487),
//   что в точности совпадает с .mdl mstudiomesh_t::vertexindex (0/1174/6708) и с числом
//   вершин .vvd (12487). Проверено: origMeshVertID мешей = 0..1173 / 0..5533 / 0..5778.
//
// ВЕСА (HWSKINNED): strip-group flags==2 => boneID[3] — ГЛАВАЛЬНЫЙ индекс кости скелета
// (используется напрямую), а boneWeightIndex — индекс в списке костей меша (для весов,
// когда они не аппаратные). Доля веса = 1/numBones (равномерная; Source хранит только индексы).
//
// Этот модуль ЧИСТЫЙ (без X-Ray): кладёт результат в MESH (см. source_mdl_mesh.h) в Source-
// конвенции, чтобы ниже по конвейеру тот же BuildXRayMesh() (basis + vertBoned4W + boneMap)
// собрал движковый меш. Порядок вершин в MESH == порядок в strip-группе (vtx), поэтому
// треугольники из .vtx напрямую совпадают индексами с outMESH.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_mesh.h"
#include "source_mdl_vvd.h"
#include "source_mdl_vtx.h"
#include <vector>

namespace SourceMdl
{
// Собирает скин-меши из split-формата. Каждый VTX_MESH -> один MESH (Source-конвенция,
// позиция/нормаль/UV из .vvd + веса/кости из .vtx, кости как ГЛАВАЛЬНЫЕ индексы скелета).
//   vvdVerts  -- вершины .vvd (строго в порядке модели: все меши подряд);
//   vtxMeshes -- меши .vtx (в том же порядке, что и меши модели).
//   outMeshes -- заполняется; .vertices[i] соответствует vtx-вершине i (с .vvd-позицией),
//                .triangles — копии треугольников из .vtx.
// Возвращает false при несоответствии количества/данных (напр., сумма numverts != vvd).
bool BuildSplitMesh(const std::vector<VVD_VERTEX>& vvdVerts,
                    const std::vector<VTX_MESH>& vtxMeshes,
                    std::vector<MESH>& outMeshes);
} // namespace SourceMdl
