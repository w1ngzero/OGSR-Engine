#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_basis_xray.h -- движковая часть базиса Source->X-Ray. Зависит от X-Ray (Fmatrix),
// поэтому вынесена отдельно от чистой source_mdl_basis.h (которую можно тестировать без движка).
// См. source_mdl_basis.h для описания самого базиса и важного примечания про конвенцию Fmatrix.
//--------------------------------------------------------------------------------------------------
// X-Ray типы:

namespace SourceMdl
{
// Тот же базис, что GetSourceToXRayBasis(), но в виде X-Ray Fmatrix (конвенция X-Ray: row-major,
// трансляции в строке 3; компоненты читаются как в чистых формулах выше).
// Используется и для скелета (bind_xray = basis * model), и для меша (basis.transform_tiny(pos)),
// чтобы не было расхождений.
// (Реализована в source_mdl_basis.cpp.)
Fmatrix GetSourceToXRayBasisFmatrix();
} // namespace SourceMdl
