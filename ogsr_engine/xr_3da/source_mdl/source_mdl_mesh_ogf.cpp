#include "stdafx.h"
//--------------------------------------------------------------------------------------------------
// source_mdl_mesh_ogf.cpp -- сериализация скин-меша в OGF-контейнеры (чистый модуль, без DX).
//
// ФОРМАТ КОНТЕЙНЕРА (точная копия IWriter::open_chunk/close_chunk и IReader::find_chunk,
// см. xrCore/FS.cpp):
//     u32 id;      // тип чанка (OGF_VERTICES / OGF_INDICES)
//     u32 size;    // размер payload в байтах
//     <payload>
// Содержимое:
//   OGF_VERTICES: u32 fvf; u32 vCount;  vertBoned4W[vCount];
//   OGF_INDICES : u32 count;            u16 index[count];
//
// vertBoned4W (см. xr_3da/bone.h): { u16 m[4]; Fvector P; Fvector N; Fvector T; Fvector B;
//                                    float w[3]; float u, v; }  = 8 + 12*4 + 12 + 8 = 76 байт.
// Порядок и размеры полей воспроизводятся ровно так, чтобы FVisual::Load() (который читает
// fvf/vCount, затем вершины по FVF::ComputeVertexSize(fvf)) увидел корректный поток.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_mesh_ogf.h"

#include <cstring>

namespace SourceMdl
{
namespace
{
void Put32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}
void Put16(std::vector<std::uint8_t>& b, std::uint16_t v)
{
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}
void PutF32(std::vector<std::uint8_t>& b, float v)
{
    std::uint32_t t;
    std::memcpy(&t, &v, 4);
    Put32(b, t);
}

void WriteVertex(std::vector<std::uint8_t>& b, const MeshOGFVertex& v)
{
    // m[4] (u16)
    Put16(b, v.m[0]); Put16(b, v.m[1]); Put16(b, v.m[2]); Put16(b, v.m[3]);
    // P
    PutF32(b, v.x); PutF32(b, v.y); PutF32(b, v.z);
    // N
    PutF32(b, v.nx); PutF32(b, v.ny); PutF32(b, v.nz);
    // T
    PutF32(b, v.tx); PutF32(b, v.ty); PutF32(b, v.tz);
    // B
    PutF32(b, v.bx); PutF32(b, v.by); PutF32(b, v.bz);
    // w[3]
    PutF32(b, v.w[0]); PutF32(b, v.w[1]); PutF32(b, v.w[2]);
    // u, v
    PutF32(b, v.u); PutF32(b, v.v);
}
} // namespace

bool BuildSourceMeshOGF(const std::vector<MeshOGFVertex>& verts,
                        const std::vector<std::uint16_t>& indices,
                        std::uint32_t fvf, std::vector<std::uint8_t>& outBuffer)
{
    outBuffer.clear();
    if (verts.empty() || indices.size() < 3)
        return false;

    // --- OGF_VERTICES ---
    std::vector<std::uint8_t> vertsPayload;
    Put32(vertsPayload, fvf);
    Put32(vertsPayload, static_cast<std::uint32_t>(verts.size()));
    for (const auto& v : verts)
        WriteVertex(vertsPayload, v);

    // --- OGF_INDICES ---
    std::vector<std::uint8_t> idxPayload;
    Put32(idxPayload, static_cast<std::uint32_t>(indices.size()));
    for (const auto i : indices)
        Put16(idxPayload, i);

    // --- упаковка ---
    outBuffer.reserve(8 + vertsPayload.size() + 8 + idxPayload.size());
    Put32(outBuffer, static_cast<std::uint32_t>(kOGF_VERTICES));
    Put32(outBuffer, static_cast<std::uint32_t>(vertsPayload.size()));
    outBuffer.insert(outBuffer.end(), vertsPayload.begin(), vertsPayload.end());

    Put32(outBuffer, static_cast<std::uint32_t>(kOGF_INDICES));
    Put32(outBuffer, static_cast<std::uint32_t>(idxPayload.size()));
    outBuffer.insert(outBuffer.end(), idxPayload.begin(), idxPayload.end());

    return true;
}
} // namespace SourceMdl
