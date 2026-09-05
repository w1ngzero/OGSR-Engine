//--------------------------------------------------------------------------------------------------
// test_real_mdl.cpp — СВЕРКА ИМПОРТЁРА НА РЕАЛЬНОМ .MDL из Garry's Mod (руки, split-формат v49).
//
// Читает настоящий бинарный файл gfl2_asteria_arms.mdl через исправленный ридер скелета и
// выводит результат. Это доказывает, что после правок (id=0x54534449, stride кости=216 для v49)
// импортёре реально парсит скелет этой модели, а не только синтетический тест.
//
// Компиляция (бинарный файл кладётся рядом или путь передаётся аргументом):
//   g++ -std=c++17 -O2 -o test_real \
//       test_real_mdl.cpp source_mdl_skeleton.cpp source_mdl_mesh.cpp source_mdl_mesh_read.cpp
//   ./test_real /home/user/hands/gfl2_asteria_arms.mdl
//--------------------------------------------------------------------------------------------------
#include "source_mdl_skeleton.h"
#include "source_mdl_mesh_read.h"
#include "source_mdl_mesh.h"
#include "source_mdl_vvd.h"
#include "source_mdl_vtx.h"
#include "source_mdl_split_mesh.h"
#include "source_mdl_basis.h"
#include "source_mdl_anim.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

using namespace SourceMdl;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <model.mdl>\n", argv[0]);
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f)
    {
        std::printf("cannot open %s\n", argv[1]);
        return 2;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> buf((size_t)sz);
    if (fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz)
    {
        std::printf("short read\n");
        return 2;
    }
    std::fclose(f);

    std::printf("=== real model: %s (%ld bytes) ===\n", argv[1], sz);

    // Скелет.
    CSourceMdlSkeleton sk;
    bool ok = sk.Parse(buf.data(), buf.size());
    std::printf("skeleton parse: %s", ok ? "OK" : "FAIL");
    if (!sk.GetLastError().empty())
        std::printf(" (%s)", sk.GetLastError().c_str());
    std::printf("\n");
    if (ok)
    {
        const auto& bones = sk.GetBones();
        std::printf("bones: %d, root=%d\n", (int)bones.size(), sk.RootIndex());
        for (size_t i = 0; i < bones.size() && i < 8; ++i)
        {
            const BONE& b = bones[i];
            std::printf("  [%d] %-32s parent=%d  pos=(%.2f,%.2f,%.2f) quat=(%.2f,%.2f,%.2f,%.2f)\n",
                        (int)i, b.name.c_str(), b.parent, b.pos.x, b.pos.y, b.pos.z,
                        b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w);
        }
        if (bones.size() > 8)
            std::printf("  ... ещё %d костей\n", (int)bones.size() - 8);
        // проверка иерархии
        int bad = 0;
        for (const auto& b : bones)
            if (!b.is_root && (b.parent < 0 || b.parent >= (int)bones.size()))
                ++bad;
        std::printf("hierarchy validity: %s\n", bad ? "PROBLEM" : "OK");
    }

    // Меш (ожидаем split-format, т.к. вершины в .vvd). Подтверждаем диагностику.
    std::vector<MESH> meshes;
    EReadMeshResult r = ReadClassicMesh(buf.data(), buf.size(), meshes, ClassicInlineLayout());
    std::printf("mesh (inline) reader: %s  (%s)\n", ReadMeshResultName(r), r == EReadMeshResult::SplitVertexFile ? "=> split-format: геометрия в .vvd/.vtx, ожидаемо" : "");

    // Дополнительно: если передан путь к .vvd — считаем вершины и проверяем .vvd-модуль.
    if (argc > 2)
    {
        std::string vvd_path = argv[2];
        if (vvd_path.size() >= 4 && vvd_path.substr(vvd_path.size() - 4) == ".vvd")
        {
            FILE* fv = std::fopen(vvd_path.c_str(), "rb");
            if (!fv)
            {
                std::printf("cannot open %s\n", vvd_path.c_str());
                return 2;
            }
            std::fseek(fv, 0, SEEK_END);
            long vsz = std::ftell(fv);
            std::fseek(fv, 0, SEEK_SET);
            std::vector<unsigned char> vb((size_t)vsz);
            if (fread(vb.data(), 1, (size_t)vsz, fv) != (size_t)vsz)
            {
                std::printf("short read %s\n", vvd_path.c_str());
                std::fclose(fv);
                return 2;
            }
            std::fclose(fv);
            std::vector<VVD_VERTEX> verts;
            EVVDResult vr = ReadVvdVertices(vb.data(), vb.size(), verts);
            std::printf("vvd reader: %s  (%d verts)\n", VvdResultName(vr), (int)verts.size());
            if (vr == EVVDResult::Ok && !verts.empty())
                std::printf("  vert[0] pos=(%.3f,%.3f,%.3f) normal=(%.3f,%.3f,%.3f) uv=(%.3f,%.3f)\n",
                            verts[0].pos.x, verts[0].pos.y, verts[0].pos.z,
                            verts[0].normal.x, verts[0].normal.y, verts[0].normal.z,
                            verts[0].u, verts[0].v);

            // Дополнительно: если передан путь к .vtx — считаем индексы + веса и сверяем.
            if (argc > 3)
            {
                std::string vtx_path = argv[3];
                FILE* ft = std::fopen(vtx_path.c_str(), "rb");
                if (!ft)
                {
                    std::printf("cannot open %s\n", vtx_path.c_str());
                    return 2;
                }
                std::fseek(ft, 0, SEEK_END);
                long tsz = std::ftell(ft);
                std::fseek(ft, 0, SEEK_SET);
                std::vector<unsigned char> tb((size_t)tsz);
                if (fread(tb.data(), 1, (size_t)tsz, ft) != (size_t)tsz)
                {
                    std::printf("short read %s\n", vtx_path.c_str());
                    std::fclose(ft);
                    return 2;
                }
                std::fclose(ft);

                std::vector<VTX_MESH> vmeshes;
                EVTXResult tr = ReadVtxMeshes(tb.data(), tb.size(), vmeshes);
                std::printf("vtx reader: %s  (%d meshes)\n", VtxResultName(tr), (int)vmeshes.size());
                if (tr == EVTXResult::Ok)
                {
                    bool rangeok = true;
                    std::size_t totVerts = 0, totTris = 0, totBounds = 0;
                    for (std::size_t mi = 0; mi < vmeshes.size(); ++mi)
                    {
                        const auto& vm = vmeshes[mi];
                        totVerts += vm.verts.size();
                        totTris += vm.triangles.size();
                        for (const auto& tri : vm.triangles)
                        {
                            if (tri.a >= vm.verts.size() || tri.b >= vm.verts.size() ||
                                tri.c >= vm.verts.size())
                                ++totBounds;
                        }
                        // Сводка первой группы меша 0.
                        if (mi == 0)
                        {
                            float maxw = 0.f;
                            for (const auto& vtx : vm.verts)
                                if (vtx.numBones > 0)
                                {
                                    // равномерный вес (см. комментарий в .h) — сумма = 1.0
                                    float w = 1.0f / (float)vtx.numBones;
                                    if (w > maxw)
                                        maxw = w;
                                }
                            std::printf(
                                "  mesh0: %d verts, %d tris, boneWeightIndex0=%d..%d numBones0=%d "
                                "origMeshVertID0=%d, maxUniformWeight=%.2f\n",
                                (int)vm.verts.size(), (int)vm.triangles.size(),
                                vm.verts.empty() ? -1 : (int)vm.verts[0].boneWeightIndex[0],
                                vm.verts.empty() ? -1 : (int)vm.verts[0].boneWeightIndex[2],
                                vm.verts.empty() ? -1 : (int)vm.verts[0].numBones,
                                vm.verts.empty() ? -1 : (int)vm.verts[0].origMeshVertID,
                                maxw);
                        }
                    }
                    std::printf("  totals: %d verts, %d tris, out-of-range indices: %d\n",
                                (int)totVerts, (int)totTris, (int)totBounds);
                    if (totBounds > 0)
                        rangeok = false;
                    std::printf("  index range: %s\n", rangeok ? "OK" : "PROBLEM");

                    // Сборка скин-меша из .vvd + .vtx (Source-конвенция: позиция из .vvd,
                    // веса/кости из .vtx через origMeshVertID).
                    if (totVerts == verts.size())
                    {
                        std::vector<MESH> meshes;
                        bool sm = BuildSplitMesh(verts, vmeshes, meshes);
                        std::printf("split-mesh build: %s  (%d meshes)\n", sm ? "OK" : "FAIL",
                                    (int)meshes.size());
                        if (sm)
                        {
                            std::size_t sv = 0, st = 0;
                            int maxBone = -1;
                            for (const auto& me : meshes)
                            {
                                sv += me.vertices.size();
                                st += me.triangles.size();
                                for (const auto& mvb : me.vertices)
                                    for (int i = 0; i < mvb.num_weights; ++i)
                                        if (mvb.bone[i] > maxBone)
                                            maxBone = mvb.bone[i];
                            }
                            const auto& first = meshes[0].vertices[0];
                            std::printf("  totals: %d verts, %d tris; mesh0.vert0 pos=(%.2f,%.2f,%.2f) "
                                        "bone0=%d w0=%.2f\n",
                                        (int)sv, (int)st, first.pos.x, first.pos.y, first.pos.z,
                                        first.bone[0], first.weight[0]);
                            std::printf("  max global boneID = %d\n", maxBone);

                            // Применяем чистый базис Source->X-Ray (det==+1) к позициям и
                            // считаем bbox в X-Ray-системе (Z-вверх). Дубли-вершины из strip
                            // могут слегка разойтись — не страшно, оцениваем охват.
                            const Basis3 b = GetSourceToXRayBasis();
                            float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
                            bool nonUnit = false;
                            for (const auto& me : meshes)
                            {
                                for (const auto& mvb : me.vertices)
                                {
                                    Vec3f p = Transform(b, mvb.pos);
                                    for (int c = 0; c < 3; ++c)
                                    {
                                        float v = (c == 0) ? p.x : (c == 1) ? p.y : p.z;
                                        if (v < mn[c]) mn[c] = v;
                                        if (v > mx[c]) mx[c] = v;
                                    }
                                    float len = mvb.normal.x * mvb.normal.x +
                                                mvb.normal.y * mvb.normal.y +
                                                mvb.normal.z * mvb.normal.z;
                                    if (std::fabs(len - 1.0f) > 0.02f)
                                        nonUnit = true;
                                }
                            }
                            std::printf("  basis(XRay, Z-up) bbox: x[%.1f..%.1f] y[%.1f..%.1f] "
                                        "z[%.1f..%.1f]\n", mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
                            std::printf("  normals unit (det==+1, no flip): %s\n",
                                        nonUnit ? "PROBLEM" : "OK");
                        }
                    }
                }
            }
        }
    }

    // Анимации (v49 раскладка, подтверждена на реальном файле).
    {
        std::vector<ANIM_SEQ> seqs;
        EAnimResult ar = ReadSourceAnims(buf.data(), buf.size(), seqs, V49AnimLayout(), (int)sk.GetBones().size());
        std::printf("anim reader (v49 layout): %s  (%d seqs)\n", AnimResultName(ar), (int)seqs.size());
        if (ar == EAnimResult::Ok || ar == EAnimResult::NoSequences)
        {
            for (const auto& q : seqs)
            {
                std::printf("  seq '%s': numframes=%d fps=%.0f tracks=%d", q.name.c_str(),
                            q.numframes, q.fps, (int)q.tracks.size());
                for (const auto& t : q.tracks)
                    if (!t.frames.empty())
                        std::printf("  bone%d:%zdf", t.bone, t.frames.size());
                std::printf("\n");
            }
        }
    }

    return ok ? 0 : 1;
}
