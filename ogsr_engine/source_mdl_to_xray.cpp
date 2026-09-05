#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_mesh_to_xray.h -- движковый упаковщик: Source-вершины -> X-Ray vertex (vertBoned4W).
// Применяет общий базис (source_mdl_basis) и переиндексирует кости Source -> движковые.
// Требует X-Ray (bone.h), поэтому отдельно от чистого source_mdl_mesh.{h,cpp}.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_mesh.h"
#include "../bone.h"

namespace SourceMdl
{
// Преобразует разобранный меш в массивы движковых вершин (до 4 влияний) и индексов.
//   src         -- разобранный меш (позиции/нормали/UV/веса в Source-конвенции)
//   boneMap     -- преобразование индекса кости Source -> индекс кости OGSR (тот же порядок,
//                  что в vecBones/векторах из BuildEngineSkeleton); -1 если кость не найдена
//   basis       -- базис Source->X-Ray (Fmatrix), применяется к позициям/нормалям
//   outVerts    -- заполняется vertBoned4W
//   outIndices  -- заполняется треугольниками (u16)
// Возвращает false при отсутствии вершин или невозможности найти хотя бы одну кость.
bool BuildXRayMesh(const MESH& src, const int* boneMap, const Fmatrix& basis,
                   std::vector<vertBoned4W>& outVerts, std::vector<u16>& outIndices);
} // namespace SourceMdl
