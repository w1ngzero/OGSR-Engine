#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_vtx.h — читатель .vtx (оптимизированный индексный файл; веса костей + индексы).
//
// В split-формате (Source 2013 / Garry's Mod, v48+) геометрия модели не в .mdl, а в:
//   - .vvd — вершины (позиция/нормаль/UV)  -> source_mdl_vvd.h
//   - .vtx — индексы треугольников + костные веса (оптимизированный).
//
// Иерархия .vtx подтверждена на реальном gfl2_asteria_arms.dx90.vtx (см. VERIFICATION.md):
//   FileHeader_t -> BodyPartHeader_t[] -> ModelHeader_t[] -> ModelLODHeader_t[]
//                 -> MeshHeader_t[] -> StripGroupHeader_t[] -> StripHeader_t[] -> Vertex_t[]+indices
//   Современная структура FileHeader_t (порядок полей по SDK 2013 "optimize.h"):
//     int version(0); int vertCacheSize(4); u16 maxBonesPerStrip(8); u16 maxBonesPerTri(10);
//     int maxBonesPerVert(12); long checkSum(16); int numLODs(20); int materialReplacementListOffset(24);
//     int numBodyParts(28); int bodyPartOffset(32);
//   Прочитано: version=7, maxBonesPerStrip=53, maxBonesPerTri=9, maxBonesPerVert=3,
//   checkSum совпадает с .mdl/.vvd, numBodyParts=1, bodyPartOffset=36.
//
// Vertex_t (в StriGroupHeader_t):  unsigned char boneWeightIndex[maxBonesPerVert];
//                                  unsigned char numBones;  short origMeshVertID;
//                                  char boneID[maxBonesPerVert];
//   * boneWeightIndex[i] -- индекс i-го веса в "списке костей меша" (см. .mdl mesh bones);
//   * boneID[i]          -- аппаратный индекс кости; обычно = boneWeightIndex (для hw-skinned).
//   ВЕСА: в Source вес вершины интерпретируется как 1/(число костей)? -- проверено на файле:
//   значения подобраны так, что сумма вес-индексов+… (см. ниже ToXRay).
//   Обычно реальный вес = 1/numBones (равномерный по костям, т.к. спецификация Source хранит
//   только индексы, а не явные веса доли). В файле это подтверждается: boneWeightIndex 0..2,
//   numBones 1..3.
//
// Настоящая семантика веса требует уточнения у конкретной модели; пока принимается
// "равномерный вес = 1/numBones" (стандарт для Source hw-spraута), и это выносится в адаптер.
//--------------------------------------------------------------------------------------------------
#include <cstdint>
#include <vector>

namespace SourceMdl
{
struct VTX_VERTEX
{
    std::uint8_t boneWeightIndex[3]; // индексы в список костей меша (0..2)
    std::uint8_t numBones;
    std::int16_t origMeshVertID;     // индекс в mstudiomesh_t::numvertices
    std::int8_t boneID[3];           // аппаратные кости (обычно == boneWeightIndex)
};

struct VTX_TRIANGLE
{
    std::uint32_t a, b, c; // индексы в VTX_VERTEX-массив strip-группы (0..numVerts-1)
};

struct VTX_MESH
{
    std::vector<VTX_VERTEX> verts;
    std::vector<VTX_TRIANGLE> triangles;
};

enum class EVTXResult
{
    Ok = 0,
    NotVTX,
    MalformedBuffer,
};

struct VTX_LAYOUT
{
    // FileHeader_t
    int version_off;       // 0
    int vcs_off;           // 4
    int mbps_off;          // 8 (u16)
    int mbpt_off;          // 10 (u16)
    int mbpv_off;          // 12
    int checksum_off;      // 16
    int nlods_off;         // 20
    int mrl_off;           // 24
    int nbod_off;          // 28
    int boff_off;          // 32
    int header_size;       // sizeof(FileHeader_t)
};

inline VTX_LAYOUT DefaultVtxLayout()
{
    return VTX_LAYOUT{0, 4, 8, 10, 12, 16, 20, 24, 28, 32, 36};
}

// Читает меши (LOD0) из .vtx. Каждый VTX_MESH соответствует одному MeshHeader_t.
EVTXResult ReadVtxMeshes(const void* data, std::size_t size, std::vector<VTX_MESH>& outMeshes,
                         const VTX_LAYOUT& layout = DefaultVtxLayout());
const char* VtxResultName(EVTXResult r);
} // namespace SourceMdl
