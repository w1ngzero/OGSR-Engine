//--------------------------------------------------------------------------------------------------
// Standalone unit test for the Source->X-Ray importer (compiles with plain C++17, no engine deps).
//   g++ -std=c++17 -o test test_source_mdl.cpp source_mdl_skeleton.cpp \
//       source_mdl_mesh.cpp source_mdl_mesh_read.cpp source_mdl_anim.cpp && ./test
// Covers: skeleton parse (R1), basis axis-convention (R3), mesh pipeline (R3), animation quant (R4).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_skeleton.h"
#include "source_mdl_basis.h"
#include "source_mdl_mesh.h"
#include "source_mdl_mesh_read.h"
#include "source_mdl_anim.h"
#include "source_mdl_anim_q.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>

using namespace SourceMdl;

namespace
{
constexpr std::size_t kStride = 152;
constexpr std::size_t kV47NumbodypartsOff = 232;
constexpr std::size_t kV47BodypartindexOff = 236;
// mstudiomodel_t: name[64] + numvertices@64 + vertexindex@68 + nummeshes@72 + meshindex@76
constexpr std::size_t kModelStride = 84;
constexpr std::size_t kMeshStride = 48;
constexpr std::size_t kVertStride = 40; // mstudiovertex_t (классика inline)

void put32(std::vector<std::uint8_t>& buf, std::size_t off, std::int32_t v) { std::memcpy(&buf[off], &v, 4); }
void putU16(std::vector<std::uint8_t>& buf, std::size_t off, std::uint16_t v) { std::memcpy(&buf[off], &v, 2); }
void putF(std::vector<std::uint8_t>& buf, std::size_t off, float x, float y, float z)
{
    std::memcpy(&buf[off], &x, 4); std::memcpy(&buf[off + 4], &y, 4); std::memcpy(&buf[off + 8], &z, 4);
}
void putF2(std::vector<std::uint8_t>& buf, std::size_t off, float x, float y, float z, float w)
{
    std::memcpy(&buf[off], &x, 4); std::memcpy(&buf[off + 4], &y, 4);
    std::memcpy(&buf[off + 8], &z, 4); std::memcpy(&buf[off + 12], &w, 4);
}

// ---- synthetic 3-bone skeleton (Round 1) ------------------------------------------------
struct Built { std::vector<std::uint8_t> buf; std::size_t boneIndex; };
Built make3Bone()
{
    Built b;
    b.boneIndex = 164;
    struct Rec { const char* name; int parent; float p[3], q[4]; };
    const float c = 0.70710678f;
    Rec recs[3] = {{"root", -1, {0,0,0}, {0,0,0,1}}, {"torso", 0, {0,10,0}, {0,0,c,c}}, {"hand", 1, {0,5,0}, {0,0,0,1}}};
    const std::size_t nameBase = b.boneIndex + 3 * kStride;
    std::size_t total = nameBase;
    for (int i = 0; i < 3; ++i) total += std::strlen(recs[i].name) + 1;
    b.buf.assign(total, 0);
    put32(b.buf, 0, (std::int32_t)0x54534449u); // 'IDST'
    put32(b.buf, 4, 47);
    std::memcpy(&b.buf[12], "test_mdl", 8);
    put32(b.buf, 156, 3);
    put32(b.buf, 160, (std::int32_t)b.boneIndex);
    std::size_t cur = nameBase;
    for (int i = 0; i < 3; ++i)
    {
        std::size_t off = b.boneIndex + (std::size_t)i * kStride;
        put32(b.buf, off + 0, (std::int32_t)(cur - off));
        put32(b.buf, off + 4, recs[i].parent);
        putF(b.buf, off + 32, recs[i].p[0], recs[i].p[1], recs[i].p[2]);
        putF2(b.buf, off + 44, recs[i].q[0], recs[i].q[1], recs[i].q[2], recs[i].q[3]);
        float one = 1.f;
        for (int k = 0; k < 6; ++k) std::memcpy(&b.buf[off + 72 + k * 4], &one, 4);
        std::memcpy(&b.buf[cur], recs[i].name, std::strlen(recs[i].name));
        cur += std::strlen(recs[i].name) + 1;
    }
    return b;
}

// ---- synthetic multi-root skeleton (hands + weapon, viewmodel) ------------------------
Built makeMultiRoot()
{
    Built b;
    b.boneIndex = 164;
    struct Rec { const char* name; int parent; float p[3], q[4]; };
    const float c = 0.70710678f;
    // два независимых под-дерева: руки (origin->arm) и оружие (weapon->blade)
    Rec recs[4] = {
        {"origin", -1, {0,0,0}, {0,0,0,1}},
        {"arm",     0, {0,10,0}, {0,0,c,c}},
        {"weapon", -1, {0,5,0}, {0,0,0,1}},
        {"blade",   2, {0,20,0}, {0,0,0,1}},
    };
    const std::size_t nameBase = b.boneIndex + 4 * kStride;
    std::size_t total = nameBase;
    for (int i = 0; i < 4; ++i) total += std::strlen(recs[i].name) + 1;
    b.buf.assign(total, 0);
    put32(b.buf, 0, (std::int32_t)0x54534449u);
    put32(b.buf, 4, 47);
    std::memcpy(&b.buf[12], "viewmdl", 7);
    put32(b.buf, 156, 4);
    put32(b.buf, 160, (std::int32_t)b.boneIndex);
    std::size_t cur = nameBase;
    for (int i = 0; i < 4; ++i)
    {
        std::size_t off = b.boneIndex + (std::size_t)i * kStride;
        put32(b.buf, off + 0, (std::int32_t)(cur - off));
        put32(b.buf, off + 4, recs[i].parent);
        putF(b.buf, off + 32, recs[i].p[0], recs[i].p[1], recs[i].p[2]);
        putF2(b.buf, off + 44, recs[i].q[0], recs[i].q[1], recs[i].q[2], recs[i].q[3]);
        float one = 1.f;
        for (int k = 0; k < 6; ++k) std::memcpy(&b.buf[off + 72 + k * 4], &one, 4);
        std::memcpy(&b.buf[cur], recs[i].name, std::strlen(recs[i].name));
        cur += std::strlen(recs[i].name) + 1;
    }
    return b;
}

// ---- synthetic inline mesh (Round 3) --------------------------------------------------
// Один bodypart, одна модель, один меш, N вершин, один треугольник.
struct MeshBuilt { std::vector<std::uint8_t> buf; };
MeshBuilt makeInlineMesh(int nverts)
{
    MeshBuilt b;
    const std::size_t headerSize = 300;
    const std::size_t bpOff = headerSize;         // 16 bytes
    const std::size_t mdlOff = bpOff + 16;        // 84 bytes
    const std::size_t meshOff = mdlOff + kModelStride; // 48 bytes
    const std::size_t vertBase = meshOff + kMeshStride;
    const std::size_t triBase = vertBase + (std::size_t)nverts * kVertStride;
    const std::size_t total = triBase + 3 * 2;    // 1 треугольник

    b.buf.assign(total, 0);
    put32(b.buf, 0, (std::int32_t)0x54534449u); // id 'IDST'
    put32(b.buf, 4, 47);
    std::memcpy(&b.buf[12], "test_mesh", 9);
    put32(b.buf, kV47NumbodypartsOff, 1);       // numbodyparts
    put32(b.buf, kV47BodypartindexOff, (std::int32_t)bpOff);

    // bodypart
    put32(b.buf, bpOff + 4, 1);                 // nummodels
    put32(b.buf, bpOff + 12, (std::int32_t)mdlOff); // modelindex

    // model (имя забито нулями — не важно)
    put32(b.buf, mdlOff + 64, nverts);          // numvertices
    put32(b.buf, mdlOff + 68, (std::int32_t)vertBase); // vertexindex (байтовое)
    put32(b.buf, mdlOff + 72, 1);               // nummeshes
    put32(b.buf, mdlOff + 76, (std::int32_t)meshOff); // meshindex

    // mesh
    put32(b.buf, meshOff + 0, 1);               // numtriangles
    put32(b.buf, meshOff + 4, (std::int32_t)triBase); // triangleindex
    put32(b.buf, meshOff + 8, nverts);          // numvertices
    put32(b.buf, meshOff + 12, 0);              // vertexoffset (счётчик)

    // вершины: weight[3], bone[3], numbones, pad1, pos, normal, uv
    for (int i = 0; i < nverts; ++i)
    {
        std::size_t vo = vertBase + (std::size_t)i * kVertStride;
        // веса: все на кость 0
        b.buf[vo + 0] = 255; b.buf[vo + 1] = 0; b.buf[vo + 2] = 0;
        b.buf[vo + 3] = 0;   b.buf[vo + 4] = 0; b.buf[vo + 5] = 0;
        b.buf[vo + 6] = 1;   // numbones
        b.buf[vo + 7] = 0;   // pad
        putF(b.buf, vo + 8, (float)i, 0.f, 0.f);       // pos
        putF(b.buf, vo + 20, 0.f, 1.f, 0.f);           // normal (up Y)
        // little-endian float 1.0f = bytes [0x00, 0x00, 0x80, 0x3F]
        b.buf[vo + 32] = 0x00; b.buf[vo + 33] = 0x00; b.buf[vo + 34] = 0x80; b.buf[vo + 35] = 0x3F;
        b.buf[vo + 36] = 0x00; b.buf[vo + 37] = 0x00; b.buf[vo + 38] = 0x80; b.buf[vo + 39] = 0x3F;
    }
    putU16(b.buf, triBase + 0, 0);
    putU16(b.buf, triBase + 2, 1);
    putU16(b.buf, triBase + 4, 2);
    return b;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("  [FAIL] %s\n", #cond); ++failures; } else std::printf("  [ok]   %s\n", #cond); } while (0)
} // namespace

int main()
{
    // ===== Round 1: skeleton =====
    std::printf("== Round 1: parse 3-bone Source MDL skeleton ==\n");
    Built b = make3Bone();
    CSourceMdlSkeleton sk;
    CHECK(sk.Parse(b.buf.data(), b.buf.size()));
    const auto& bones = sk.GetBones();
    CHECK(bones.size() == 3);
    CHECK(sk.RootIndex() == 0);
    CHECK(bones[1].name == "torso" && bones[1].parent == 0);
    CHECK(near(bones[1].model_bind.m[0][0], 0.f) && near(bones[1].model_bind.m[0][1], -1.f));
    CHECK(near(bones[1].model_bind.m[3][1], 10.f));
    CHECK(near(bones[2].model_bind.m[3][0], 5.f) && near(bones[2].model_bind.m[3][1], 10.f));

    // ===== Round 10: multi-root (viewmodel: руки + оружие) =====
    std::printf("== Round 10: multi-root skeleton (hands + weapon) ==\n");
    {
        Built m = makeMultiRoot();
        CSourceMdlSkeleton mv;
        CHECK(mv.Parse(m.buf.data(), m.buf.size()));          // больше НЕ падает на 2 корнях
        const auto& mb = mv.GetBones();
        CHECK(mb.size() == 4);
        CHECK(mv.GetRootMulti().size() == 2);                 // origin, weapon
        CHECK(mv.RootIndex() == 0);                           // главный корень = первый
        CHECK(mb[2].is_root && mb[2].name == "weapon");

        // Базы независимы: bind обоих корней = их локальные трансформы.
        CHECK(near(mb[0].model_bind.m[3][1], 0.f));
        CHECK(near(mb[2].model_bind.m[3][1], 5.f));           // weapon позиционирован отдельно
        CHECK(near(mb[3].model_bind.m[3][1], 25.f));          // blade под weapon (5+20)

        // Приводим к единому дереву БЕЗ переименования индексов.
        CSourceMdlSkeleton single = mv;
        CHECK(single.BakeSingleRoot());
        const auto& sb = single.GetBones();
        CHECK(sb.size() == 5);                                // +1 синтетический корень
        CHECK(single.RootIndex() == 4);                       // новый корень в конце
        CHECK(sb[4].name == "skeleton_root");
        CHECK(!sb[0].is_root && sb[0].parent == 4);           // старые корни пере-подвешены
        CHECK(!sb[2].is_root && sb[2].parent == 4);
        CHECK(single.GetRootMulti().size() == 1);
        // Bind старых корней не изменился (ребро к новому корню = identity).
        CHECK(near(sb[2].model_bind.m[3][1], 5.f));
        CHECK(near(sb[3].model_bind.m[3][1], 25.f));
        CHECK(near(sb[4].model_bind.m[3][0], 0.f));            // новый корень в начале координат
        CHECK(near(sb[4].model_bind.m[0][0], 1.f));            // identity-поворот
    }

    // ===== Round 3: basis =====
    std::printf("== Round 3: basis Source->X-Ray ==\n");
    Basis3 basis = GetSourceToXRayBasis();
    const float S = kSourceToXRayScale;
    // поворотная часть (детерминант/масштаб) — чистый поворот без отражения:
    CHECK(near(Det3(basis), S * S * S));           // det = S^3 (масштаб+чистый поворот), + => без flip
    CHECK(RequiresWindingOrNormalFlip(basis) == false);
    {
        Vec3f up{0, 1, 0};                         // "вверх" Source (Y) -> xray +Z
        Vec3f r = Transform(basis, up);
        CHECK(near(r.x, 0.f) && near(r.y, 0.f) && near(r.z, S));
        Vec3f fwd{0, 0, 1};                        // "вперёд" Source (Z) -> xray +Y (в экран)
        Vec3f r2 = Transform(basis, fwd);
        CHECK(near(r2.x, 0.f) && near(r2.y, S) && near(r2.z, 0.f));
        Vec3f right{1, 0, 0};                      // X -> -X (поворот, det=+1)
        Vec3f r3 = Transform(basis, right);
        CHECK(near(r3.x, -S) && near(r3.y, 0.f) && near(r3.z, 0.f));
    }

    // ===== Round 3: mesh weight normalization =====
    std::printf("== Round 3: weight normalization ==\n");
    {
        float w[4] = {0.5f, 0.25f, 0.25f, 0.f};
        int bi[4] = {0, 1, 2, -1};
        int n = NormalizeWeights(w, bi, 3);
        CHECK(n == 3);
        CHECK(near(w[0], 0.5f) && near(w[1], 0.25f) && near(w[2], 0.25f)); // сумма уже 1
    }
    {
        float w[4] = {0.6f, 0.2f, 0.2f, 0.1f};
        int bi[4] = {0, 1, 2, 3};
        int n = NormalizeWeights(w, bi, 4);
        CHECK(n == 4);
        CHECK(near(w[0] + w[1] + w[2] + w[3], 1.f));
    }
    {
        float w[4] = {0.f, 0.f, 0.f, 0.f};
        int bi[4] = {2, 2, 2, 2};
        int n = NormalizeWeights(w, bi, 3);
        CHECK(n == 1 && near(w[0], 1.f)); // все веса нулевые -> всё на первую кость
    }

    // ===== Round 3: basis applied to a vertex =====
    std::printf("== Round 3: basis on a vertex (pos & normal) ==\n");
    {
        Vec3f pos{0, 10, 0}, normal{0, 1, 0};
        ApplyBasisToVertex(basis, pos, normal);
        CHECK(near(pos.x, 0.f) && near(pos.y, 0.f) && near(pos.z, 10.f)); // Y-up -> Z-up, позиция
        CHECK(near(normal.x, 0.f) && near(normal.y, 0.f) && near(normal.z, 1.f)); // нормаль тоже
    }

    // ===== Round 3: classic inline mesh reader =====
    std::printf("== Round 3: classic inline mesh reader ==\n");
    MeshBuilt mb = makeInlineMesh(3);
    std::vector<MESH> meshes;
    EReadMeshResult r = ReadClassicMesh(mb.buf.data(), mb.buf.size(), meshes, ClassicInlineLayout());
    CHECK(r == EReadMeshResult::Ok);
    CHECK(meshes.size() == 1);
    if (!meshes.empty())
    {
        const MESH& m = meshes[0];
        CHECK(m.vertices.size() == 3);
        CHECK(m.triangles.size() == 1);
        CHECK(m.triangles[0].a == 0 && m.triangles[0].b == 1 && m.triangles[0].c == 2);
        CHECK(near(m.vertices[0].pos.x, 0.f));
        CHECK(m.vertices[0].num_weights == 1);
        CHECK(near(m.vertices[0].weight[0], 1.f)); // 255/255
        CHECK(near(m.vertices[0].u, 1.f) && near(m.vertices[0].v, 1.f));
    }

    // ===== Round 3: rejection ==
    std::printf("== Round 3: mesh rejection ==\n");
    {
        MeshBuilt bad = makeInlineMesh(3);
        put32(bad.buf, 0, 0x12345678); // повреждённый id
        std::vector<MESH> ms;
        CHECK(ReadClassicMesh(bad.buf.data(), bad.buf.size(), ms) == EReadMeshResult::NotSourceMdl);
    }

    // ===== Round 4: rotation quantization round-trip =====
    std::printf("== Round 4: rotation quantization (matches X-Ray QR2Quat) ==\n");
    {
        using namespace AnimQ;
        // Единичный поворот.
        Quat4 q{0.f, 0.f, 0.f, 1.f};
        int16_t s[4]; QuantizeRot(q, s);
        Quat4 dq = DequantizeRot(s);
        CHECK(near(dq.w, 1.f) && near(dq.x, 0.f) && near(dq.y, 0.f) && near(dq.z, 0.f));
        // Поворот 90° вокруг Z: (0,0,0.7071,0.7071).
        const float c = 0.70710678f;
        Quat4 q2{0.f, 0.f, c, c};
        QuantizeRot(q2, s);
        Quat4 d2 = DequantizeRot(s);
        CHECK(near(d2.z, c, 1e-3f) && near(d2.w, c, 1e-3f));
        // Проверка того, что квантование не выходит за диапазон s16.
        Quat4 q3{0.999f, -0.999f, 0.5f, -0.5f};
        QuantizeRot(q3, s);
        CHECK(s[0] >= -32767 && s[0] <= 32767 && s[1] >= -32767 && s[1] <= 32767);
        // норма не накопила >1 дрожь
        Quat4 d3 = DequantizeRot(s);
        CHECK(std::fabs(d3.x - q3.x) < 1e-4f);
    }

    // ===== Round 4: translation quantization round-trip =====
    std::printf("== Round 4: translation quantization (matches X-Ray QT*_2T) ==\n");
    {
        using namespace AnimQ;
        // Диапазон [0..100] по X, постоянные Y,Z.
        const float positions[] = {
            0.f,  10.f, 5.f,
            25.f, 10.f, 5.f,
            50.f, 10.f, 5.f,
            100.f, 10.f, 5.f,
        };
        QuantParams p;
        FitTranslationRange(positions, 4, /*t16=*/true, p);
        CHECK(p.size[0] > 0.f);
        CHECK(near(p.init[0], 50.f)); // середина диапазона X
        Vec3 t{50.f, 10.f, 5.f};
        int16_t s[3];
        QuantizeT(t, p, s);
        CHECK(s[0] == 0 && s[1] == 0 && s[2] == 0); // ровно по центру -> отсчёт 0
        Vec3 d = DequantizeT(s, p);
        CHECK(near(d.x, 50.f, 1e-2f) && near(d.y, 10.f, 1e-2f) && near(d.z, 5.f, 1e-2f));
        // Край: max X -> отсчёт максимально положительный.
        Vec3 tmax{100.f, 10.f, 5.f};
        QuantizeT(tmax, p, s);
        d = DequantizeT(s, p);
        CHECK(near(d.x, 100.f, 1e-2f));
    }

    // ===== Round 4: source-anim reader rejection / no-sequences =====
    std::printf("== Round 4: source-anim reader header cases ==\n");
    {
        // Базовый буфер: только заголовок (верный id/version), numlocalseq = 0.
        std::vector<std::uint8_t> hdr(300, 0);
        put32(hdr, 0, (std::int32_t)0x54534449u);
        put32(hdr, 4, 47);
        put32(hdr, 224, 0); // numlocalseq = 0
        std::vector<ANIM_SEQ> seqs;
        CHECK(ReadSourceAnims(hdr.data(), hdr.size(), seqs) == EAnimResult::NoSequences);
        seqs.clear();
        put32(hdr, 0, 0x12345678);
        CHECK(ReadSourceAnims(hdr.data(), hdr.size(), seqs) == EAnimResult::NotSourceMdl);
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
