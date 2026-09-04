//--------------------------------------------------------------------------------------------------
// source_mdl_vvd.cpp — реализация читателя .vvd (см. source_mdl_vvd.h).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_vvd.h"
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
bool InRange(std::size_t off, std::size_t n, std::size_t size)
{
    return off <= size && n <= size - off;
}
} // namespace

const char* VvdResultName(EVVDResult r)
{
    switch (r)
    {
    case EVVDResult::Ok: return "ok";
    case EVVDResult::NotVVD: return "not a .vvd (bad id)";
    case EVVDResult::UnsupportedVersion: return "unsupported version";
    case EVVDResult::MalformedBuffer: return "buffer out of range";
    }
    return "?";
}

EVVDResult ReadVvdVertices(const void* data, std::size_t size, std::vector<VVD_VERTEX>& outVerts,
                           const VVD_LAYOUT& L)
{
    outVerts.clear();
    if (!data || size < 64)
        return EVVDResult::MalformedBuffer;

    const std::uint8_t* base = static_cast<const std::uint8_t*>(data);

    std::uint32_t id = 0, version = 0;
    if (!ReadAt(base, size, static_cast<std::size_t>(L.id_off), id) ||
        !ReadAt(base, size, static_cast<std::size_t>(L.version_off), version))
        return EVVDResult::MalformedBuffer;

    // MODEL_VERTEX_FILE_ID = 'IDVS' (байты 49 44 56 53) as u32 = 0x53564449/0x... ;
    // на реальном файле прочитано 0x56534449 ('VSID'... уточнено). В VERIFICATION.md:
    // id = 0x56534449.
    if (id != 0x56534449u)
        return EVVDResult::NotVVD;
    if (version != 4)
        return EVVDResult::UnsupportedVersion;

    std::int32_t numlods = 0, nlverts0 = 0, vstart = 0;
    if (!ReadAt(base, size, static_cast<std::size_t>(L.numlods_off), numlods) ||
        !ReadAt(base, size, static_cast<std::size_t>(L.nlverts_off), nlverts0) ||
        !ReadAt(base, size, static_cast<std::size_t>(L.vstart_off), vstart))
        return EVVDResult::MalformedBuffer;

    if (nlverts0 <= 0 || vstart < 0)
        return EVVDResult::MalformedBuffer;

    const std::size_t stride = static_cast<std::size_t>(L.vert_stride);
    const std::size_t n = static_cast<std::size_t>(nlverts0);
    if (!InRange(static_cast<std::size_t>(vstart), n * stride, size))
        return EVVDResult::MalformedBuffer;

    outVerts.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t vo = static_cast<std::size_t>(vstart) + i * stride;
        VVD_VERTEX v{};
        // Раскладка подтверждена на реальном файле: pos@L.pos_off, normal@L.normal_off, uv@L.uv_off
        // (первые 16 байт — константа, не геометрия). Оффсеты параметризуемы через VVD_LAYOUT.
        std::memcpy(&v.pos, base + vo + static_cast<std::size_t>(L.pos_off), 12);
        std::memcpy(&v.normal, base + vo + static_cast<std::size_t>(L.normal_off), 12);
        std::memcpy(&v.u, base + vo + static_cast<std::size_t>(L.uv_off), 4);
        std::memcpy(&v.v, base + vo + static_cast<std::size_t>(L.uv_off) + 4, 4);
        outVerts.push_back(v);
    }
    return EVVDResult::Ok;
}
} // namespace SourceMdl
