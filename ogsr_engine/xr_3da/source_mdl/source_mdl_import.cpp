//--------------------------------------------------------------------------------------------------
// source_mdl_import.cpp -- engine-side Source .MDL skeleton importer.
//--------------------------------------------------------------------------------------------------
#include "stdafx.h"
#include "source_mdl_import.h"
#include "source_mdl_skeleton.h"
#include "source_mdl_to_xray.h"
#include "source_mdl_basis_xray.h"
#include "source_mdl_vvd.h"
#include "source_mdl_vtx.h"
#include "source_mdl_split_mesh.h"
#include "source_mdl_mesh_to_xray.h"
#include "source_mdl_mesh.h"
#include "source_mdl_anim.h" // ReadSourceAnims / ANIM_SEQ
#include "source_mdl_anim_to_xray.h" // BuildXRayMotionsOMF
#include "../fmesh.h" // OGF_* / ogf_header / MT_SKELETON_GEOMDEF_ST / OGF_VERTEXFORMAT_FVF_4L

namespace SourceMdl
{
ENGINE_API int psSourceSkeletonMode = 0;
ENGINE_API int psSourceMeshMode = 0;

namespace
{
// Читает целиком файл из game_meshes в буфер; возвращает true при успехе.
// Если extPrimary не найден, пробует extFallback (например ".vtx" -> ".dx90.vtx",
// т.к. vtx-файлы Source на диске почти всегда носят имя "<name>.dx90.vtx").
bool ReadFileToBuffer(const char* baseName, const char* extPrimary, xr_string& bufOut)
{
    const char* const fallbacks[] = { ".dx90.vtx", ".sw.vtx", nullptr };
    const char* candidates[4] = { nullptr };
    int n = 0;
    candidates[n++] = extPrimary;
    for (int i = 0; fallbacks[i] && n < 3; i++)
        if (xr_strcmp(extPrimary, fallbacks[i]) != 0)
            candidates[n++] = fallbacks[i];

    for (int i = 0; i < n; i++)
    {
        string_path path, fp;
        xr_strcpy(path, sizeof(path), baseName);
        if (strext(path))
            *strext(path) = 0;
        xr_strcat(path, candidates[i]);

        if (!FS.exist(fp, fsgame::game_meshes, path))
            continue;

        IReader* R = FS.r_open(fsgame::game_meshes, path);
        if (!R)
            continue;
        bufOut.assign(reinterpret_cast<const char*>(R->pointer()), R->length());
        FS.r_close(R);
        return true;
    }
    return false;
}

} // namespace

bool TryImportSourceMesh(const char* modelName, SourceMeshImport& out)
{
    out = SourceMeshImport{};

    if (!psSourceSkeletonMode)
        return false;

    // Избавляемся от расширения базового имени (чтобы разыскать .mdl/.vvd/.vtx).
    string_path base;
    xr_strcpy(base, sizeof(base), modelName);
    if (strext(base))
        *strext(base) = 0;

    // Нужны и скелет (для корня/числа костей), и геометрия. Скелет даёт boneMap (identity:
    // движковые кости создаются в том же порядке, что в .mdl — см. source_mdl_to_xray.cpp).
    vecBones bones;
    int root = -1;
    if (!TryImportSourceSkeleton(base, bones, root))
        return false;

    // Читаем .vvd и .vtx.
    xr_string vvdBuf, vtxBuf;
    if (!ReadFileToBuffer(base, ".vvd", vvdBuf) || !ReadFileToBuffer(base, ".vtx", vtxBuf))
    {
        Msg("!! [SourceMesh] missing .vvd/.vtx for '%s'", base);
        return false;
    }

    std::vector<VVD_VERTEX> vvdVerts;
    if (ReadVvdVertices(vvdBuf.data(), vvdBuf.size(), vvdVerts) != EVVDResult::Ok || vvdVerts.empty())
    {
        Msg("!! [SourceMesh] failed to parse .vvd for '%s'", base);
        return false;
    }

    std::vector<VTX_MESH> vtxMeshes;
    if (ReadVtxMeshes(vtxBuf.data(), vtxBuf.size(), vtxMeshes) != EVTXResult::Ok || vtxMeshes.empty())
    {
        Msg("!! [SourceMesh] failed to parse .vtx for '%s'", base);
        return false;
    }

    // Собираем MESH в Source-конвенции (позиция из .vvd, веса/кости из .vtx).
    std::vector<MESH> meshes;
    if (!BuildSplitMesh(vvdVerts, vtxMeshes, meshes))
    {
        Msg("!! [SourceMesh] BuildSplitMesh failed for '%s'", base);
        return false;
    }

    // Один общий меш (все 3 части в один vertex/index буфер). boneMap = identity.
    MESH combined;
    for (const auto& m : meshes)
    {
        const u32 baseCount = static_cast<u32>(combined.vertices.size());
        combined.vertices.insert(combined.vertices.end(), m.vertices.begin(), m.vertices.end());
        for (const auto& t : m.triangles)
            combined.triangles.push_back(MESH_TRIANGLE{t.a + baseCount, t.b + baseCount, t.c + baseCount});
    }

    // Индексы костей движка совпадают с порядком .mdl (см. BuildEngineSkeleton), поэтому
    // boneMap[i] = i. При желании можно сопоставить по имени, но для импортируемого скелета
    // порядок уже согласован.
    out.boneMap.resize(bones.size());
    for (std::size_t i = 0; i < out.boneMap.size(); ++i)
        out.boneMap[i] = static_cast<int>(i);

    // Применяем базис + переиндексируем кости -> видимый движковый меш (vertBoned4W).
    const Fmatrix& basis = GetSourceToXRayBasisFmatrix();
    if (!BuildXRayMesh(combined, out.boneMap.data(), basis, out.verts, out.indices))
    {
        Msg("!! [SourceMesh] BuildXRayMesh failed for '%s'", base);
        return false;
    }

    out.root = root;
    out.numTriangles = static_cast<u32>(out.indices.size() / 3);
    out.loaded = true;

    // Внешняя сфера из вершин (X-Ray-конвенция) — для vis-данных.
    Fvector mn, mx;
    mn.set(1e30f, 1e30f, 1e30f);
    mx.set(-1e30f, -1e30f, -1e30f);
    for (const auto& v : out.verts)
    {
        mn.x = _min(mn.x, v.P.x); mn.y = _min(mn.y, v.P.y); mn.z = _min(mn.z, v.P.z);
        mx.x = _max(mx.x, v.P.x); mx.y = _max(mx.y, v.P.y); mx.z = _max(mx.z, v.P.z);
    }
    Fvector center;
    center.x = (mn.x + mx.x) * 0.5f; center.y = (mn.y + mx.y) * 0.5f; center.z = (mn.z + mx.z) * 0.5f;
    const float dx = mx.x - mn.x, dy = mx.y - mn.y, dz = mx.z - mn.z;
    const float radius = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f;
    out.sphere.set(center, radius);

    Msg("[SourceMesh] imported %d verts / %d tris from '%s' (root=%d, boneMap=%d)",
        (int)out.verts.size(), (int)out.numTriangles, base, root, (int)out.boneMap.size());
    return true;
}

bool TryImportSourceSkeleton(const char* modelName, vecBones& outBones, int& outRoot)
{
    outBones.clear();
    outRoot = -1;

    if (!psSourceSkeletonMode)
        return false;

    // Build the companion path: strip any existing extension, then use "<name>.mdl".
    string_path mdl_path;
    xr_strcpy(mdl_path, sizeof(mdl_path), modelName);
    if (strext(mdl_path))
        *strext(mdl_path) = 0;
    xr_strcat(mdl_path, ".mdl");

    string_path mdl_fs_path;
    if (!FS.exist(mdl_fs_path, fsgame::game_meshes, mdl_path))
    {
        Msg("!! [SourceSkeleton] no companion .mdl for '%s' (looked for '%s')", modelName, mdl_path);
        return false;
    }

    IReader* R = FS.r_open(fsgame::game_meshes, mdl_path);
    if (!R)
        return false;

    // Parse the .MDL skeleton from the raw buffer.
    CSourceMdlSkeleton parser;
    const bool parsed = parser.Parse(R->pointer(), R->length());
    if (!parsed)
    {
        Msg("!! [SourceSkeleton] failed to parse '%s': %s", mdl_path, parser.GetLastError().c_str());
        FS.r_close(R);
        return false;
    }

    // Единый базис Source -> X-Ray (см. source_mdl_basis.h). Тот же самый применяется к мешу
    // (source_mdl_mesh_to_xray.cpp), чтобы скелет и геометрия жили в одной системе координат.
    const Fmatrix& basis = GetSourceToXRayBasisFmatrix();
    const bool built = BuildEngineSkeleton(parser, outBones, outRoot, basis);

    if (built)
        Msg("[SourceSkeleton] imported %d bone(s) from '%s' (root=%d)", (int)outBones.size(), mdl_path, outRoot);
    else
        Msg("!! [SourceSkeleton] could not build engine skeleton from '%s'", mdl_path);

    FS.r_close(R);
    return built;
}

namespace
{
void Put32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}
// Пишет чанк `id` с содержимым `payload` (упаковка u32 id; u32 size; payload — как IWriter).
void PutChunk(std::vector<std::uint8_t>& b, std::uint32_t id, const std::vector<std::uint8_t>& payload)
{
    Put32(b, id);
    Put32(b, static_cast<std::uint32_t>(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
}
void PutCString(std::vector<std::uint8_t>& b, const char* s)
{
    if (!s)
        s = "";
    while (*s)
        b.push_back(static_cast<std::uint8_t>(*s++));
    b.push_back(0);
}
} // namespace

namespace
{
// Преобразует 4-костную вершину (vertBoned4W) в 2-костную (vertBoned2W).
// Берёт две КОСТИ с наибольшим весом, отбрасывает остальные и РЕНОРМАЛИЗУЕТ.
// Зачем: движковые HUD/руки стоковых моделей используют 1-2 кости (skin=1/2),
// и готовых skinning-вариантов шейдера для 4 костей нет (см. ResourceManager_Resources).
// 2-костный формат соответствует существующему шейдеру models\weapons_2 /
// models\model_2, что и требуется для работы скиннинга (bonesArray не NULL).
// ВАЖНО про vertBoned4W: у него w[3] веса 4-й кости НЕТ, движок считает его
// сам как 1-w0-w1-w2 (см. SkeletonX.cpp). Поэтому собираем до 4 влияний:
// (m0,w0),(m1,w1),(m2,w2),(m3,1-w0-w1-w2) и выбираем два самых больших.
void Convert4WTo2W(const vertBoned4W& s, vertBoned2W& d)
{
    // --- подсчёт весов (включая неявный 4-й) ---
    float bw[4];
    u16 bm[4];
    for (int i = 0; i < 3; ++i)
    {
        bw[i] = s.w[i];
        bm[i] = s.m[i];
    }
    bw[3] = 1.f - s.w[0] - s.w[1] - s.w[2]; // неявный вес 4-й кости
    bm[3] = s.m[3];

    // --- 2 самых больших ---
    int i0 = 0, i1 = 1;
    if (bw[1] > bw[0])
    {
        // ручной свап (без std::swap, чтобы не зависеть от <utility>)
        int tmp = i0;
        i0 = i1;
        i1 = tmp;
    }
    for (int i = 2; i < 4; ++i)
    {
        if (bw[i] > bw[i0])
        {
            i1 = i0;
            i0 = i;
        }
        else if (bw[i] > bw[i1])
        {
            i1 = i;
        }
    }

    float wsum = bw[i0] + bw[i1];
    if (wsum <= 0.f)
    {
        // нет весов вообще -> прибить к кости 0.
        d.matrix0 = 0;
        d.matrix1 = 0;
        d.w = 1.f;
    }
    else
    {
        d.matrix0 = bm[i0];
        d.matrix1 = bm[i1];
        d.w = bw[i1] / wsum; // vertBoned2W.w = вес второй кости, w0 = 1-w (см. SkeletonX.cpp)
    }
    d.P = s.P;
    d.N = s.N;
    d.T = s.T;
    d.B = s.B;
    d.u = s.u;
    d.v = s.v;
}
} // namespace

bool BuildSourceMeshOGFStream(const SourceMeshImport& imp, const char* textureName, const char* shaderName,
                              std::vector<std::uint8_t>& outBytes)
{
    outBytes.clear();
    if (!imp.loaded || imp.verts.empty() || imp.indices.size() < 3)
        return false;

    // --- OGF_HEADER ---
    ogf_header hdr{};
    hdr.format_version = xrOGF_FormatVersion;
    hdr.type = MT_SKELETON_GEOMDEF_ST; // CSkeletonX_ST (скин-геометрия, одиночный LOD)
    hdr.shader_id = 0;                 // shader задаётся через OGF_TEXTURE ниже
    // bb из вершин
    Fvector mn, mx;
    mn.set(1e30f, 1e30f, 1e30f);
    mx.set(-1e30f, -1e30f, -1e30f);
    for (const auto& v : imp.verts)
    {
        mn.x = _min(mn.x, v.P.x); mn.y = _min(mn.y, v.P.y); mn.z = _min(mn.z, v.P.z);
        mx.x = _max(mx.x, v.P.x); mx.y = _max(mx.y, v.P.y); mx.z = _max(mx.z, v.P.z);
    }
    hdr.bb.min = mn;
    hdr.bb.max = mx;
    // Fsphere (P,R) и ogf_bsphere (c,r) — разные типы: копируем поля явно.
    hdr.bs.c = imp.sphere.P;
    hdr.bs.r = imp.sphere.R;

    std::vector<std::uint8_t> hdrPayload(reinterpret_cast<const std::uint8_t*>(&hdr),
                                         reinterpret_cast<const std::uint8_t*>(&hdr) + sizeof(hdr));

    // --- OGF_TEXTURE ---
    std::vector<std::uint8_t> texPayload;
    // Порядок чтения в dxRender_Visual::Load: сначала fnT(текстура), затем fnS(шейдер).
    PutCString(texPayload, textureName);
    PutCString(texPayload, shaderName);

    // --- OGF_VERTICES ---
    // ВАЖНО: эмитируем 2-костный формат (FVF_2L), а не 4-костный.
    // 4-костному (FVF_4L) не соответствует ни один существующий skinning-вариант
    // шейдера (default_4 / models\weapons_4), из-за чего bonesArray==NULL и анимация
    // не применяется. Стоковые HUD-модели используют skin=1/2, и шейдеры models\weapons_1/2
    // существуют (доказано рабочей wpn_pm_hud). Поэтому пишем 2 кости/вершину.
    constexpr std::uint32_t kFVF2L = OGF_VERTEXFORMAT_FVF_2L; // из fmesh.h, точное значение
    constexpr std::uint32_t kFVF4L = OGF_VERTEXFORMAT_FVF_4L; // сохранено для справки
    (void)kFVF4L;
    std::vector<std::uint8_t> vertsPayload;
    Put32(vertsPayload, kFVF2L);
    Put32(vertsPayload, static_cast<std::uint32_t>(imp.verts.size()));
    for (const auto& v : imp.verts)
    {
        // 4-костный vert по конвейеру -> 2-костный в OGF.
        vertBoned2W twoW{};
        Convert4WTo2W(v, twoW);
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(&twoW);
        vertsPayload.insert(vertsPayload.end(), p, p + sizeof(vertBoned2W));
    }

    // --- OGF_INDICES ---
    std::vector<std::uint8_t> idxPayload;
    Put32(idxPayload, static_cast<std::uint32_t>(imp.indices.size()));
    idxPayload.insert(idxPayload.end(),
                      reinterpret_cast<const std::uint8_t*>(imp.indices.data()),
                      reinterpret_cast<const std::uint8_t*>(imp.indices.data()) + imp.indices.size() * sizeof(u16));

    // --- сборка ---
    PutChunk(outBytes, OGF_HEADER, hdrPayload);
    PutChunk(outBytes, OGF_TEXTURE, texPayload);
    PutChunk(outBytes, OGF_VERTICES, vertsPayload);
    PutChunk(outBytes, OGF_INDICES, idxPayload);
    return true;
}

bool TryAutoBuildSkeletonOGF(const char* N, std::vector<std::uint8_t>& outBytes)
{
    outBytes.clear();
    if (!psSourceSkeletonMode)
        return false;

    // Материал для Source-меша: те же значения, что в LoadSourceMeshGeometry.
    static const char* const sSourceMeshTexture = "models\\hands\\c_hands";
    static const char* const sSourceMeshShader = "models\\weapons";

    // 1) Геометрия (нужна и для bb/bs, и как child-визуал).
    SourceMdl::SourceMeshImport imp;
    if (!TryImportSourceMesh(N, imp) || !imp.loaded || imp.verts.empty())
    {
        Msg("!! [SourceModel] auto-build: no skinned mesh in companion .mdl for '%s'", N);
        return false;
    }

    // 2) Mesh-OGF (CSkeletonX_ST child) — готовый поток с header/verts/indices.
    std::vector<std::uint8_t> meshOg;
    if (!BuildSourceMeshOGFStream(imp, sSourceMeshTexture, sSourceMeshShader, meshOg) || meshOg.empty())
    {
        Msg("!! [SourceModel] auto-build: could not serialize mesh OGF for '%s'", N);
        return false;
    }

    // 3) Top-level skeleton-anim header (bb/bs из вершин меша).
    ogf_header hdr{};
    hdr.format_version = xrOGF_FormatVersion;
    hdr.type = MT_SKELETON_ANIM;
    hdr.shader_id = 0;
    {
        Fvector mn, mx;
        mn.set(1e30f, 1e30f, 1e30f);
        mx.set(-1e30f, -1e30f, -1e30f);
        for (const auto& v : imp.verts)
        {
            mn.x = _min(mn.x, v.P.x); mn.y = _min(mn.y, v.P.y); mn.z = _min(mn.z, v.P.z);
            mx.x = _max(mx.x, v.P.x); mx.y = _max(mx.y, v.P.y); mx.z = _max(mx.z, v.P.z);
        }
        hdr.bb.min = mn;
        hdr.bb.max = mx;
        hdr.bs.c = imp.sphere.P;
        hdr.bs.r = imp.sphere.R;
    }
    std::vector<std::uint8_t> hdrPayload(reinterpret_cast<const std::uint8_t*>(&hdr),
                                         reinterpret_cast<const std::uint8_t*>(&hdr) + sizeof(hdr));

    // 4) OGF_CHILDREN: sub-chunk 0 = the mesh OGF (single child).
    //    FHierrarhyVisual::Load требует непустой OGF_CHILDREN, иначе FATAL("Invalid visual").
    std::vector<std::uint8_t> childrenPayload;
    PutChunk(childrenPayload, 0, meshOg);

    std::vector<std::uint8_t> out;
    PutChunk(out, OGF_HEADER, hdrPayload);
    PutChunk(out, OGF_CHILDREN, childrenPayload);

    outBytes.swap(out);
    return true;
}

bool TryImportSourceAnimations(const char* modelName, const vecBones* bones,
                               std::vector<std::uint8_t>& outOmfBytes)
{
    outOmfBytes.clear();

    if (!psSourceSkeletonMode || !bones || bones->empty())
        return false;

    // Companion path: "<model>.mdl" (тот же, что и для скелета).
    string_path mdl_path;
    xr_strcpy(mdl_path, sizeof(mdl_path), modelName);
    if (strext(mdl_path))
        *strext(mdl_path) = 0;
    xr_strcat(mdl_path, ".mdl");

    string_path mdl_fs_path;
    if (!FS.exist(mdl_fs_path, fsgame::game_meshes, mdl_path))
    {
        Msg("!! [SourceAnimations] no companion .mdl for '%s' (looked for '%s')", modelName, mdl_path);
        return false;
    }

    IReader* R = FS.r_open(fsgame::game_meshes, mdl_path);
    if (!R)
        return false;

    std::vector<ANIM_SEQ> seqs;
    EAnimResult r = ReadSourceAnims(R->pointer(), R->length(), seqs, V49AnimLayout100(),
                                    static_cast<int>(bones->size()));
    FS.r_close(R);

    if (r != EAnimResult::Ok)
    {
        Msg("!! [SourceAnimations] couldn't read animations from '%s': %s", mdl_path, AnimResultName(r));
        return false;
    }

    if (!BuildXRayMotionsOMF(seqs, bones, outOmfBytes))
    {
        Msg("!! [SourceAnimations] could not serialize motion stream for '%s'", mdl_path);
        return false;
    }

    Msg("[SourceAnimations] imported %zu sequence(s) from '%s' (%zu bytes)", seqs.size(), mdl_path, outOmfBytes.size());
    return true;
}
} // namespace SourceMdl
