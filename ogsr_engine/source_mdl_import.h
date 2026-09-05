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
    // Из чистого базиса:  xray.x = src.x;  xray.y = -src.z;  xray.z = src.y.
    // X-Ray Fmatrix хранит i/j/k как СТРОКИ (row-major), c — строку трансляции.
    // set(R,N,D,C): R=i, N=j, D=k, C=c.
    //   i = (1, 0, 0)
    //   j = (0, 0, -1)
    //   k = (0, 1, 0)
    //   c = (0, 0, 0)
    Fmatrix m;
    m.set(Fvector3().set(1.f, 0.f, 0.f),
          Fvector3().set(0.f, 0.f, -1.f),
          Fvector3().set(0.f, 1.f, 0.f),
          Fvector3().set(0.f, 0.f, 0.f));
    return m;
}
} // namespace SourceMdl
