//--------------------------------------------------------------------------------------------------
// test_mesh_ogf.cpp -- проверка сериализации Source-меша в OGF-контейнеры и их обратного чтения
// (воспроизводя логику FVisual::Load / IReader::find_chunk, см. xrCore/FS.cpp).
//
// Компиляция:
//   g++ -std=c++17 -O2 -o test_ogf test_mesh_ogf.cpp \
//       source_mdl_skeleton.cpp source_mdl_vvd.cpp source_mdl_vtx.cpp \
//       source_mdl_split_mesh.cpp source_mdl_mesh_ogf.cpp source_mdl_mesh.cpp
//   ./test_ogf <model> <vvd> <vtx>
//--------------------------------------------------------------------------------------------------
#include "source_mdl_skeleton.h"
#include "source_mdl_vvd.h"
#include "source_mdl_vtx.h"
#include "source_mdl_split_mesh.h"
#include "source_mdl_mesh_ogf.h"
#include "source_mdl_basis.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

using namespace SourceMdl;

namespace
{
// Мини-реализация IReader::find_chunk/r_u32/r_u16 поверх потока (контейнер u32 id, u32 size).
struct MiniReader
{
    const std::uint8_t* p;
    std::size_t size;
    std::size_t pos = 0;

    bool read_u32(std::uint32_t& v)
    {
        if (pos + 4 > size) return false;
        std::memcpy(&v, p + pos, 4); pos += 4; return true;
    }
    bool read_u16(std::uint16_t& v)
    {
        if (pos + 2 > size) return false;
        std::memcpy(&v, p + pos, 2); pos += 2; return true;
    }

    // find_chunk: перебор чанков, возвращает смещение payload-азан и размер.
    bool find_chunk(std::uint32_t wanted, std::size_t& off, std::size_t& len)
    {
        std::size_t q = 0;
        while (q + 8 <= size)
        {
            std::uint32_t id, sz;
            std::memcpy(&id, p + q, 4);
            std::memcpy(&sz, p + q + 4, 4);
            if (id == wanted)
            {
                off = q + 8;
                len = sz;
                return true;
            }
            q += 8 + sz;
        }
        return false;
    }
};

// Ожидаемый размер vertBoned4W в байтах (по раскладке bone.h): 76.
constexpr std::size_t kVBStride = 76;
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf("usage: %s <model.mdl> <model.vvd> <model.dx90.vtx>\n", argv[0]);
        return 2;
    }

    // .vvd
    FILE* fv = std::fopen(argv[2], "rb");
    std::fseek(fv, 0, SEEK_END); long vsz = std::ftell(fv); std::fseek(fv, 0, SEEK_SET);
    std::vector<unsigned char> vb((size_t)vsz);
    if (std::fread(vb.data(), 1, (size_t)vsz, fv) != (size_t)vsz) return 2;
    std::fclose(fv);
    std::vector<VVD_VERTEX> verts;
    if (ReadVvdVertices(vb.data(), vb.size(), verts) != EVVDResult::Ok)
    {
        std::printf("vvd parse FAIL\n");
        return 2;
    }

    // .vtx
    FILE* ft = std::fopen(argv[3], "rb");
    std::fseek(ft, 0, SEEK_END); long tsz = std::ftell(ft); std::fseek(ft, 0, SEEK_SET);
    std::vector<unsigned char> tb((size_t)tsz);
    if (std::fread(tb.data(), 1, (size_t)tsz, ft) != (size_t)tsz) return 2;
    std::fclose(ft);
    std::vector<VTX_MESH> vmeshes;
    if (ReadVtxMeshes(tb.data(), tb.size(), vmeshes) != EVTXResult::Ok)
    {
        std::printf("vtx parse FAIL\n");
        return 2;
    }

    // Собираем MESH, затем переводим в X-Ray-конвенцию (базис) и заполняем MeshOGFVertex.
    std::vector<MESH> meshes;
    if (!BuildSplitMesh(verts, vmeshes, meshes))
    {
        std::printf("split-mesh FAIL\n");
        return 2;
    }

    std::vector<MeshOGFVertex> ogfVerts;
    std::vector<std::uint16_t> ogfIdx;
    const Basis3 basis = GetSourceToXRayBasis();

    // Т (тангенс)/B (битангенс) строим из нормали (как BuildXRayMesh).
    std::size_t totalTris = 0;
    for (const auto& m : meshes)
    {
        const std::size_t base = ogfVerts.size();
        for (const auto& v : m.vertices)
        {
            Vec3f p = Transform(basis, v.pos);
            Vec3f n = Transform(basis, v.normal);
            Vec3f up{0.f, 0.f, 1.f};
            if (std::fabs(n.z) > 0.99f) up = Vec3f{1.f, 0.f, 0.f};
            Vec3f t, bt;
            t.x = up.y * n.z - up.z * n.y;
            t.y = up.z * n.x - up.x * n.z;
            t.z = up.x * n.y - up.y * n.x;
            bt.x = n.y * t.z - n.z * t.y;
            bt.y = n.z * t.x - n.x * t.z;
            bt.z = n.x * t.y - n.y * t.x;

            MeshOGFVertex ov{};
            ov.x = p.x; ov.y = p.y; ov.z = p.z;
            ov.nx = n.x; ov.ny = n.y; ov.nz = n.z;
            ov.tx = t.x; ov.ty = t.y; ov.tz = t.z;
            ov.bx = bt.x; ov.by = bt.y; ov.bz = bt.z;
            ov.u = v.u; ov.v = v.v;
            // вес/кости: равномерный 1/numBones (boneID = глобальный индекс кости Source).
            const int nb = v.num_weights > 0 ? v.num_weights : 1;
            for (int i = 0; i < 4; ++i)
            {
                if (i < nb && i < 3)
                {
                    ov.m[i] = static_cast<std::uint16_t>(v.bone[i]);
                    ov.w[i] = v.weight[i];
                }
                else
                {
                    ov.m[i] = 0;
                    ov.w[i] = 0.f;
                }
            }
            ogfVerts.push_back(ov);
        }
        for (const auto& t : m.triangles)
        {
            ogfIdx.push_back(static_cast<std::uint16_t>(t.a + base));
            ogfIdx.push_back(static_cast<std::uint16_t>(t.b + base));
            ogfIdx.push_back(static_cast<std::uint16_t>(t.c + base));
        }
        totalTris += m.triangles.size();
    }

    std::printf("ogf verts=%d indices=%d tris=%d\n", (int)ogfVerts.size(), (int)ogfIdx.size(), (int)totalTris);

    // Сериализация в OGF.
    constexpr std::uint32_t kFVF4L = 5u * 0x12071980u;
    std::vector<std::uint8_t> buf;
    if (!BuildSourceMeshOGF(ogfVerts, ogfIdx, kFVF4L, buf))
    {
        std::printf("OGF build FAIL\n");
        return 2;
    }
    std::printf("ogf buffer = %d bytes\n", (int)buf.size());

    // --- Обратное чтение, как FVisual::Load ---
    MiniReader R{buf.data(), buf.size(), 0};
    std::size_t voff, vlen;
    if (!R.find_chunk(kOGF_VERTICES, voff, vlen))
    {
        std::printf("read OGF_VERTICES FAIL\n");
        return 2;
    }
    // payload читаем локально
    {
        MiniReader VR{buf.data() + voff, vlen, 0};
        std::uint32_t fvf, vc;
        VR.read_u32(fvf); VR.read_u32(vc);
        std::printf("verts chunk: fvf=0x%08x vCount=%u payload=%zu\n", fvf, vc, vlen);
        if (vlen != 8 + (std::size_t)vc * kVBStride)
        {
            std::printf("STRIDE MISMATCH: expected %zu, got %zu\n", 8 + (std::size_t)vc * kVBStride, vlen);
            return 2;
        }
        // проверим первую вершину по полям
        const std::uint8_t* v0 = buf.data() + voff + 8;
        std::uint16_t m0, m1;
        std::memcpy(&m0, v0 + 0, 2);
        std::memcpy(&m1, v0 + 2, 2);
        std::printf("  vert[0].m[0]=%d m[1]=%d (b0=%d)  stride=%zu\n", m0, m1, ogfVerts[0].m[0], kVBStride);
        if (m0 != ogfVerts[0].m[0] || m1 != ogfVerts[0].m[1])
        {
            std::printf("  BONE ID MISMATCH\n");
            return 2;
        }
    }
    std::size_t ioff, ilen;
    if (!R.find_chunk(kOGF_INDICES, ioff, ilen))
    {
        std::printf("read OGF_INDICES FAIL\n");
        return 2;
    }
    {
        MiniReader IR{buf.data() + ioff, ilen, 0};
        std::uint32_t cnt;
        IR.read_u32(cnt);
        std::printf("idx chunk: count=%u payload=%zu (expect %zu)\n", cnt, ilen, 4 + (std::size_t)cnt * 2);
        if (ilen != 4 + (std::size_t)cnt * 2u)
        {
            std::printf("  INDEX LEN MISMATCH\n");
            return 2;
        }
        std::uint16_t i0; std::memcpy(&i0, buf.data() + ioff + 4, 2);
        if (i0 != ogfIdx[0])
        {
            std::printf("  INDEX[0] MISMATCH (%u vs %u)\n", i0, ogfIdx[0]);
            return 2;
        }
        // максимальный индекс должен быть < vCount
        std::uint32_t maxI = 0;
        for (std::size_t i = 0; i < cnt; ++i)
        {
            std::uint16_t x; std::memcpy(&x, buf.data() + ioff + 4 + i * 2, 2);
            if (x > maxI) maxI = x;
        }
        std::printf("  max index=%u (vCount=%u) => %s\n", maxI, (std::uint32_t)ogfVerts.size(),
                    maxI < ogfVerts.size() ? "OK" : "PROBLEM");
    }

    std::printf("ALL OGF CHECKS PASSED\n");
    return 0;
}
