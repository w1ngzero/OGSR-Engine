//--------------------------------------------------------------------------------------------------
// source_mdl_basis.cpp -- движковая (X-Ray) реализация базиса Source->X-Ray.
//--------------------------------------------------------------------------------------------------
#include "stdafx.h"
#include "source_mdl_basis_xray.h"
#include "source_mdl_basis.h"

namespace SourceMdl
{
Fmatrix GetSourceToXRayBasisFmatrix()
{
    // Тот же базис, что GetSourceToXRayBasis() (масштаб + ориентация), но в виде X-Ray Fmatrix.
    // Из чистого базиса:  xray.x = -S*src.x;  xray.y = +S*src.z;  xray.z = +S*src.y.
    // X-Ray Fmatrix хранит i/j/k как СТРОКИ (row-major), c — строку трансляции.
    // set(R,N,D,C): R=i (выходная X), N=j (выходная Y), D=k (выходная Z), C=c.
    //   i = (-S, 0, 0)   -> out.x = -S*in.x
    //   j = ( 0, 0, S)   -> out.y = +S*in.z
    //   k = ( 0, S, 0)   -> out.z = +S*in.y
    const float S = SourceMdl::kSourceToXRayScale;
    Fmatrix m;
    m.set(Fvector3().set(-S, 0.f, 0.f),
          Fvector3().set(0.f, 0.f, S),
          Fvector3().set(0.f, S, 0.f),
          Fvector3().set(0.f, 0.f, 0.f));
    return m;
}
} // namespace SourceMdl
