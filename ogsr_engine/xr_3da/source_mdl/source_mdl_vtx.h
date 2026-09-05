#include "stdafx.h"
//--------------------------------------------------------------------------------------------------
// source_mdl_vtx.cpp — читатель .vtx (оптимизированный индексный файл Source/GMod).
//
// Иерархия (подтверждена на реальном gfl2_asteria_arms.dx90.vtx):
//
//   FileHeader_t
//     bodyPartOffset[] -> BodyPartHeader_t
//       modelOffset[]      -> ModelHeader_t
//         lodOffset[]         -> ModelLODHeader_t
//           meshOffset[]          -> MeshHeader_t
//             stripGroupHeaderOffset -> StripGroupHeader_t
//               vertOffset -> Vertex_t[numVerts]
//               indexOffset-> uint16[numIndices]
//               stripOffset -> StripHeader_t[numStrips]
//
// Смещения в ВСЕХ вложенных заголовках — ОТНОСИТЕЛЬНО начала их родительского заголовка
// (это ключевое отличие от .mdl, но в данном файле каждый потомок лежит сразу после
// родителя, поэтому базой всегда служит начало текущего заголовка). Проверено:
//   sg = meshBase + stripGroupHeaderOffset; vstart = sg + vertOffset; ibase = sg + indexOffset.
//
// Размеры полей (порядок и типы — по SDK 2013 "optimize.h" и подтверждены по байтам файла):
//   Vertex_t  = 3*boneWeightIndex + 1*numBones + 2*origMeshVertID + 3*boneID = 9 байт.
//
// Источник семантики весов: в Source оптимизированный формат хранит только индексы костей,
// а доля веса восстанавливается как 1/numBones (в hw-skinned ветке Source 2013 веса
// дополнительно нормализуются по сумме). В gfl2_asteria_arms это подтверждается: numBones∈1..3,
// boneWeightIndex∈0..2. Адаптацию весов и привязку к нашему скелету выполняет
// source_mdl_to_xray.cpp (см. Round 4), здесь — только извлечение.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_vtx.h"

#include <cstring>
#include <stdexcept>

namespace SourceMdl
{
namespace
{
std::uint16_t rd_u16(const std::uint8_t* base, std::size_t size, std::size_t off, const char* what)
{
    if (off + 2 > size)
        throw std::runtime_error(std::string("vtx: u16 out of range (") + what + ")");
    std::uint16_t v;
    std::memcpy(&v, base + off, 2);
    return v;
}

std::uint32_t rd_u32(const std::uint8_t* base, std::size_t size, std::size_t off, const char* what)
{
    if (off + 4 > size)
        throw std::runtime_error(std::string("vtx: u32 out of range (") + what + ")");
    std::uint32_t v;
    std::memcpy(&v, base + off, 4);
    return v;
}

std::int32_t rd_i32(const std::uint8_t* base, std::size_t size, std::size_t off, const char* what)
{
    return static_cast<std::int32_t>(rd_u32(base, size, off, what));
}
} // namespace

const char* VtxResultName(EVTXResult r)
{
    switch (r)
    {
    case EVTXResult::Ok: return "ok";
    case EVTXResult::NotVTX: return "not a .vtx file";
    case EVTXResult::MalformedBuffer: return "malformed .vtx buffer";
    }
    return "unknown";
}

EVTXResult ReadVtxMeshes(const void* data, std::size_t size, std::vector<VTX_MESH>& outMeshes,
                         const VTX_LAYOUT& L)
{
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);

    if (size < static_cast<std::size_t>(L.header_size))
        return EVTXResult::MalformedBuffer;

    // Проверка сигнатуры: первые 4 байта — version. Для split-формата .vtx magic = IDSV?
    // В .vtx заголовок начинается с int version. Проверяем на известное значение 7.
    const std::int32_t version = rd_i32(p, size, L.version_off, "version");
    if (version != 7)
        return EVTXResult::NotVTX;

    const std::int32_t numBodyParts = rd_i32(p, size, L.nbod_off, "numBodyParts");
    const std::int32_t bodyPartOffset = rd_i32(p, size, L.boff_off, "bodyPartOffset");

    try
    {
        // BodyPartHeader_t { int numModels; int modelOffset; } -> 8 байт, base = начало bodypart.
        for (int bp = 0; bp < numBodyParts; ++bp)
        {
            const std::size_t bpBase = static_cast<std::size_t>(bodyPartOffset) + bp * 8;
            const std::int32_t numModels = rd_i32(p, size, bpBase + 0, "bp.numModels");
            const std::int32_t modelOffset = rd_i32(p, size, bpBase + 4, "bp.modelOffset");

            // ModelHeader_t { int numLODs; int lodOffset; } -> 8 байт, base = начало bodypart.
            for (int m = 0; m < numModels; ++m)
            {
                const std::size_t modelBase = bpBase + static_cast<std::size_t>(modelOffset) + m * 8;
                const std::int32_t numLODs = rd_i32(p, size, modelBase + 0, "model.numLODs");
                const std::int32_t lodOffset = rd_i32(p, size, modelBase + 4, "model.lodOffset");

                // ModelLODHeader_t { int numMeshes; int meshOffset; int switchPoint; } -> 12 байт.
                for (int lod = 0; lod < numLODs; ++lod)
                {
                    const std::size_t lodBase = modelBase + static_cast<std::size_t>(lodOffset) + lod * 12;
                    const std::int32_t numMeshes = rd_i32(p, size, lodBase + 0, "lod.numMeshes");
                    const std::int32_t meshOffset = rd_i32(p, size, lodBase + 4, "lod.meshOffset");

                    // MeshHeader_t { int numStripGroups; int stripGroupHeaderOffset; u8 flags; }
                    // = 9 байт (НЕ выровнен, без padding). Проверено на реальном файле: меши
                    // расположены друг за другом со сдвигом 9; при сдвиге 8 это ломается.
                    for (int mesh = 0; mesh < numMeshes; ++mesh)
                    {
                        const std::size_t meshBase = lodBase + static_cast<std::size_t>(meshOffset) + static_cast<std::size_t>(mesh) * 9;
                        const std::int32_t numStripGroups = rd_i32(p, size, meshBase + 0, "mesh.numStripGroups");
                        const std::int32_t sgOffset = rd_i32(p, size, meshBase + 4, "mesh.sgOffset");
                        // flags @ +8 (u8) — не нужен для извлечения.

                        VTX_MESH vm;
                        for (int sg = 0; sg < numStripGroups; ++sg)
                        {
                            const std::size_t sgBase =
                                meshBase + static_cast<std::size_t>(sgOffset) + sg * 28;
                            const std::int32_t numVerts = rd_i32(p, size, sgBase + 0, "sg.numVerts");
                            const std::int32_t vertOffset = rd_i32(p, size, sgBase + 4, "sg.vertOffset");
                            const std::int32_t numIndices = rd_i32(p, size, sgBase + 8, "sg.numIndices");
                            const std::int32_t indexOffset = rd_i32(p, size, sgBase + 12, "sg.indexOffset");
                            // numStrips @ +16, stripOffset @ +20, flags @ +24.

                            const std::size_t vstart = sgBase + static_cast<std::size_t>(vertOffset);
                            const std::size_t ibase = sgBase + static_cast<std::size_t>(indexOffset);
                            // Vertex_t stride = 9.
                            constexpr std::size_t kVertexStride = 9;

                            vm.verts.reserve(static_cast<std::size_t>(numVerts));
                            for (int i = 0; i < numVerts; ++i)
                            {
                                const std::size_t o = vstart + static_cast<std::size_t>(i) * kVertexStride;
                                VTX_VERTEX vt;
                                vt.boneWeightIndex[0] = p[o + 0];
                                vt.boneWeightIndex[1] = p[o + 1];
                                vt.boneWeightIndex[2] = p[o + 2];
                                vt.numBones = p[o + 3];
                                std::int16_t omv;
                                std::memcpy(&omv, p + o + 4, 2);
                                vt.origMeshVertID = omv;
                                vt.boneID[0] = static_cast<std::int8_t>(p[o + 6]);
                                vt.boneID[1] = static_cast<std::int8_t>(p[o + 7]);
                                vt.boneID[2] = static_cast<std::int8_t>(p[o + 8]);
                                vm.verts.push_back(vt);
                            }

                            vm.triangles.reserve(static_cast<std::size_t>(numIndices) / 3);
                            for (int i = 0; i + 2 < numIndices; i += 3)
                            {
                                VTX_TRIANGLE t;
                                t.a = rd_u16(p, size, ibase + static_cast<std::size_t>(i) * 2 + 0, "idx.a");
                                t.b = rd_u16(p, size, ibase + static_cast<std::size_t>(i) * 2 + 2, "idx.b");
                                t.c = rd_u16(p, size, ibase + static_cast<std::size_t>(i) * 2 + 4, "idx.c");
                                vm.triangles.push_back(t);
                            }
                        }
                        outMeshes.push_back(std::move(vm));
                    }
                }
            }
        }
    }
    catch (const std::exception&)
    {
        return EVTXResult::MalformedBuffer;
    }

    return EVTXResult::Ok;
}
} // namespace SourceMdl
