#include "stdafx.h"
//--------------------------------------------------------------------------------------------------
// source_mdl_mesh_read.cpp -- реализация классического inline-читателя меша (см. header).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_mesh_read.h"
#include <cstring>

namespace SourceMdl
{
namespace
{
template <typename T>
bool ReadAt(const std::uint8_t* base, std::size_t size, std::size_t off, T& out)
{
    if (off + sizeof(T) > size)
        return false;
    std::memcpy(&out, base + off, sizeof(T));
    return true;
}

bool Range(std::size_t off, std::size_t cnt, std::size_t size)
{
    return off <= size && cnt <= size - off;
}

} // namespace

const char* ReadMeshResultName(EReadMeshResult r)
{
    switch (r)
    {
    case EReadMeshResult::Ok: return "ok";
    case EReadMeshResult::NotSourceMdl: return "not a Source MDL (bad id)";
    case EReadMeshResult::UnsupportedVersion: return "unsupported version";
    case EReadMeshResult::SplitVertexFile: return "model uses a separate vertex file (split format, not supported this round)";
    case EReadMeshResult::MalformedBuffer: return "buffer out of range";
    case EReadMeshResult::EmptyModel: return "no vertices";
    }
    return "unknown";
}

EReadMeshResult ReadClassicMesh(const void* data, std::size_t size, std::vector<MESH>& outMeshes,
                                const CLASSIC_LAYOUT& L)
{
    outMeshes.clear();
    if (!data || size < 300)
        return EReadMeshResult::MalformedBuffer;

    const std::uint8_t* base = static_cast<const std::uint8_t*>(data);

    std::uint32_t id = 0, version = 0;
    if (!ReadAt(base, size, 0, id) || !ReadAt(base, size, 4, version))
        return EReadMeshResult::MalformedBuffer;

    // 'IDST' = bytes 49 44 53 54, read as little-endian u32 = 0x54534449. Verified on a real
    // Source v49 model.
    if (id != 0x54534449u)
        return EReadMeshResult::NotSourceMdl;
    // Классический INLINE меш существует только в v44-v47 (vertex file embedded). v48+ (Source
    // 2013 / Garry's Mod) — split-формат: вершины в .vvd, индексы в .vtx, что мы сейчас не
    // читаем. Возвращаем SplitVertexFile, а не UnsupportedVersion, чтобы диагностика была честной.
    if (version >= 48)
        return EReadMeshResult::SplitVertexFile;
    if (version < 44)
        return EReadMeshResult::UnsupportedVersion;

    std::int32_t numbodyparts = 0, bodypartindex = 0;
    if (!ReadAt(base, size, static_cast<std::size_t>(L.numbodyparts_off), numbodyparts) ||
        !ReadAt(base, size, static_cast<std::size_t>(L.bodypartindex_off), bodypartindex))
        return EReadMeshResult::MalformedBuffer;

    if (numbodyparts <= 0)
        return EReadMeshResult::EmptyModel;
    if (bodypartindex < 0 || static_cast<std::size_t>(bodypartindex) >= size)
        return EReadMeshResult::MalformedBuffer;

    for (std::int32_t bp = 0; bp < numbodyparts; ++bp)
    {
        const std::size_t bp_off = static_cast<std::size_t>(bodypartindex) + static_cast<std::size_t>(bp) * 16;
        std::int32_t nummodels = 0, modelindex = 0;
        if (!Range(bp_off + L.bp_nummodels_off, 4, size) ||
            !ReadAt(base, size, bp_off + L.bp_nummodels_off, nummodels) ||
            !ReadAt(base, size, bp_off + L.bp_modelindex_off, modelindex))
            return EReadMeshResult::MalformedBuffer;

        if (nummodels <= 0 || modelindex < 0)
            continue;

        for (std::int32_t mdl = 0; mdl < nummodels; ++mdl)
        {
            const std::size_t mdl_off = static_cast<std::size_t>(modelindex) + static_cast<std::size_t>(mdl) * 84;
            std::int32_t numvertices = 0, vertexindex = 0, nummeshes = 0, meshindex = 0;
            if (!Range(mdl_off + 84, 0, size) ||
                !ReadAt(base, size, mdl_off + L.mdl_numvertices_off, numvertices) ||
                !ReadAt(base, size, mdl_off + L.mdl_vertexindex_off, vertexindex) ||
                !ReadAt(base, size, mdl_off + L.mdl_nummeshes_off, nummeshes) ||
                !ReadAt(base, size, mdl_off + L.mdl_meshindex_off, meshindex))
                return EReadMeshResult::MalformedBuffer;

            if (nummeshes <= 0 || vertexindex < 0)
                continue;

            // Позиция/нормаль/UV и веса в массиве вершин модели.
            const std::size_t vert_base = static_cast<std::size_t>(vertexindex);
            if (!Range(vert_base, static_cast<std::size_t>(numvertices) * static_cast<std::size_t>(L.vert_stride), size))
                return EReadMeshResult::MalformedBuffer;

            MESH mesh;
            mesh.vertices.reserve(static_cast<std::size_t>(numvertices));

            // Проверяем, не указывают ли данные вершин в отдельный (split) файл — эвристика по
            // "подозрительному" vertexindex (0 или заведомо вне размера): в split-формате он 0.
            // Это НЕ полноценный детектор; честно помечаем модель как не-классическую.
            if (vertexindex == 0 || numvertices <= 0)
                return EReadMeshResult::SplitVertexFile;

            for (std::int32_t vi = 0; vi < numvertices; ++vi)
            {
                const std::size_t vo = vert_base + static_cast<std::size_t>(vi) * static_cast<std::size_t>(L.vert_stride);
                MESH_VERTEX v{};

                // До трёх влияний.
                const std::uint8_t numbones = base[vo + L.vert_numbones_off];
                int n = numbones;
                if (n > 3)
                    n = 3;
                v.num_weights = n;
                for (int w = 0; w < 3; ++w)
                {
                    v.weight[w] = static_cast<float>(base[vo + L.vert_weight_off + w]) / 255.f;
                    v.bone[w] = static_cast<std::int8_t>(base[vo + L.vert_bone_off + w]);
                    if (v.weight[w] <= 0.f)
                        v.bone[w] = -1; // нулевой вес -> не влияет
                }

                std::memcpy(&v.pos, base + vo + L.vert_pos_off, 12);
                std::memcpy(&v.normal, base + vo + L.vert_normal_off, 12);
                std::memcpy(&v.u, base + vo + L.vert_uv_off, 4);
                std::memcpy(&v.v, base + vo + L.vert_uv_off + 4, 4);
                mesh.vertices.push_back(v);
            }

            // Треугольники.
            for (std::int32_t ms = 0; ms < nummeshes; ++ms)
            {
                const std::size_t ms_off = static_cast<std::size_t>(meshindex) + static_cast<std::size_t>(ms) * 48;
                std::int32_t numtri = 0, triindex = 0;
                if (!Range(ms_off + 0, 4, size) ||
                    !ReadAt(base, size, ms_off + L.mesh_numtri_off, numtri) ||
                    !ReadAt(base, size, ms_off + L.mesh_triindex_off, triindex))
                    return EReadMeshResult::MalformedBuffer;

                std::vector<std::uint32_t> idx;
                if (numtri > 0 && triindex >= 0 &&
                    Range(static_cast<std::size_t>(triindex), static_cast<std::size_t>(numtri) * 3 * 2, size))
                {
                    idx.reserve(static_cast<std::size_t>(numtri) * 3);
                    for (std::int32_t t = 0; t < numtri * 3; ++t)
                    {
                        std::uint16_t v = 0;
                        std::memcpy(&v, base + static_cast<std::size_t>(triindex) + static_cast<std::size_t>(t) * 2, 2);
                        idx.push_back(v);
                    }
                    BuildTriangles(idx.data(), idx.size(), mesh);
                }
            }

            // Если нашли хоть один меш с вершинами — добавляем.
            if (!mesh.vertices.empty())
                outMeshes.push_back(std::move(mesh));
        }
    }

    if (outMeshes.empty())
        return EReadMeshResult::EmptyModel;
    return EReadMeshResult::Ok;
}
} // namespace SourceMdl
