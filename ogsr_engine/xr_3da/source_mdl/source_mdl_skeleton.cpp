#include "stdafx.h"
//--------------------------------------------------------------------------------------------------
// source_mdl_skeleton.cpp -- implements the Source .MDL skeleton reader declared in the header.
// See source_mdl_skeleton.h for the design notes of this Round-1 milestone.
//
// Layout facts relied upon (offsets are stable across the major public Source .MDL versions,
// i.e. HL2-era up to current GMod/Source SDK 2013 "v48/v49"):
//
//   studiohdr_t:
//     + 0    int  id          (kMDLID)
//     + 4    int  version
//     + 8    long checksum
//     + 12   char name[64]
//     + 76   int  length
//     + 80   Vector eyeposition
//     + 92   Vector illumposition
//     +104   Vector hull_min
//     +116   Vector hull_max
//     +128   Vector view_bbmin
//     +140   Vector view_bbmax
//     +152   int  flags
//     +156   int  numbones      <-- we read this
//     +160   int  boneindex     <-- we read this (offset to mstudiobone_t array)
//
//   mstudiobone_t (stride = kMstudioBoneStride, see notes):
//     + 0    int  sznameindex   (name offset, relative to THIS bone record)
//     + 4    int  parent
//     + 8    int  bonecontroller[6]
//     + 32   Vector pos
//     + 44   Quaternion quat
//     + 60   Vector rot
//     + 72   Vector posscale
//     + 84   Vector rotscale
//     + 96   byte poseToBone[6][4]
//     + 120  Quaternion qAlignment
//     + 136  int  flags
//
// The fields we need (0..139, i.e. everything up to `flags`) have not moved between versions;
// only the record *stride* (sizeof mstudiobone_t) differs, so it is a parameter.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_skeleton.h"

#include <cstdio>
#include <cstring>
#include <cmath>

namespace SourceMdl
{
namespace
{
constexpr std::uint32_t kStudioVersion = 0x02; // unused, kept for clarity

// Record stride (sizeof mstudiobone_t). VERIFIED against a real Source v49 model (GMod
// "c_hands"): the bone struct is 216 bytes (pos@32, quat@44, full matrix3x4_t poseToBone,
// flags pushed out past +136). Older v44-47 builds were smaller. We select by header version.
std::size_t BoneStrideByVersion(std::uint32_t version)
{
    // v48+ (Source 2013 / Garry's Mod): verified 216 bytes.
    if (version >= 48)
        return 216;
    // v44-47 (HL2-era): classic smaller struct; best-known default (to be verified on a real file).
    return 152;
}
constexpr std::size_t kMinBoneRecord = 148; // minimum bytes we must read per bone (pos/quat/scale)
} // namespace

// --- tiny row-major helpers ---------------------------------------------------------------

static mat4 QuatToMat4(const quat& q)
{
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    // Standard rotation matrix from a unit quaternion.
    mat4 r{};
    r.m[0][0] = 1.f - 2.f * (y * y + z * z);
    r.m[0][1] = 2.f * (x * y - z * w);
    r.m[0][2] = 2.f * (x * z + y * w);

    r.m[1][0] = 2.f * (x * y + z * w);
    r.m[1][1] = 1.f - 2.f * (x * x + z * z);
    r.m[1][2] = 2.f * (y * z - x * w);

    r.m[2][0] = 2.f * (x * z - y * w);
    r.m[2][1] = 2.f * (y * z + x * w);
    r.m[2][2] = 1.f - 2.f * (x * x + y * y);

    r.m[0][3] = r.m[1][3] = r.m[2][3] = 0.f;
    r.m[3][0] = r.m[3][1] = r.m[3][2] = 0.f;
    r.m[3][3] = 1.f;
    return r;
}

static mat4 Mul(const mat4& a, const mat4& b)
{
    mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            float s = 0.f;
            for (int k = 0; k < 4; ++k)
                s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

static mat4 Compose(const vec3& pos, const quat& q)
{
    mat4 r = QuatToMat4(q);
    r.m[3][0] = pos.x;
    r.m[3][1] = pos.y;
    r.m[3][2] = pos.z;
    return r;
}

// --- CSourceMdlSkeleton -----------------------------------------------------------------

bool CSourceMdlSkeleton::Parse(const void* data, std::size_t size)
{
    m_bones.clear();
    m_root = -1;
    m_error.clear();

    if (!data || size < 164)
    {
        m_error = "buffer too small / null";
        return false;
    }

    const std::uint8_t* base = static_cast<const std::uint8_t*>(data);

    // Header fixed fields.
    std::uint32_t id = 0, version = 0;
    std::memcpy(&id, base + 0, 4);
    std::memcpy(&version, base + 4, 4);

    if (id != Constants::kMDLID)
    {
        m_error = "not a Source MDL (bad id 0x" + std::to_string(id) + ")";
        return false;
    }
    if (version < Constants::kMDLVERSION_MIN || version > Constants::kMDLVERSION_MAX)
    {
        m_error = "unsupported StudioMDL version " + std::to_string(version);
        return false;
    }

    std::int32_t numBones = 0, boneIndex = 0;
    std::memcpy(&numBones, base + 156, 4);
    std::memcpy(&boneIndex, base + 160, 4);

    if (numBones <= 0 || numBones > 256)
    {
        m_error = "implausible bone count " + std::to_string(numBones);
        return false;
    }

    const std::size_t stride = BoneStrideByVersion(version);
    if (stride < kMinBoneRecord)
    {
        m_error = "bone stride too small";
        return false;
    }

    m_bones.resize(static_cast<std::size_t>(numBones));

    for (std::int32_t i = 0; i < numBones; ++i)
    {
        const std::size_t off = static_cast<std::size_t>(boneIndex) + static_cast<std::size_t>(i) * stride + 0;
        if (off + kMinBoneRecord > size)
        {
            m_error = "bone record out of range";
            return false;
        }

        const std::uint8_t* b = base + off;
        BONE& bone = m_bones[static_cast<std::size_t>(i)];

        std::int32_t sznameindex = 0, parent = 0, flags = 0;
        std::memcpy(&sznameindex, b + 0, 4);
        std::memcpy(&parent, b + 4, 4);
        std::memcpy(&flags, b + 136, 4);

        // name is relative to the START of this bone record
        std::int32_t nameOff = static_cast<std::int32_t>(off) + sznameindex;
        if (nameOff < 0 || nameOff + 1 > static_cast<std::int32_t>(size))
        {
            m_error = "bone name offset out of range";
            return false;
        }
        const char* name = reinterpret_cast<const char*>(base + nameOff);
        // figure off the fixed part of the name buffer
        std::size_t nameLen = strnlen(name, size - nameOff);
        if (nameLen == 0)
        {
            m_error = "empty bone name";
            return false;
        }
        bone.name.assign(name, nameLen);
        for (auto& c : bone.name)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        bone.parent = parent;
        bone.is_root = (parent == -1);
        if (bone.is_root)
        {
            if (m_root != -1)
            {
                m_error = "multiple root bones";
                return false;
            }
            m_root = i;
        }
        else if (parent < 0 || parent >= numBones)
        {
            m_error = "bone parent out of range";
            return false;
        }

        std::memcpy(&bone.pos, b + 32, 12);
        std::memcpy(&bone.rotation, b + 44, 16);
        std::memcpy(&bone.pos_scale, b + 72, 12);
        std::memcpy(&bone.rot_scale, b + 84, 12);
        bone.flags = static_cast<std::uint32_t>(flags);

        std::memset(&bone.model_bind, 0, sizeof(bone.model_bind));
    }

    if (m_root == -1)
    {
        m_error = "no root bone found";
        return false;
    }

    ComputeBindMatrices();
    return true;
}

bool CSourceMdlSkeleton::ReadName(const std::uint8_t* base, std::uint32_t offset,
                                  std::string& out, std::size_t size) const
{
    // not used by the main loop (names are resolved inline); kept for completeness / tests
    if (offset >= size)
        return false;
    out.assign(reinterpret_cast<const char*>(base + offset), strnlen(reinterpret_cast<const char*>(base + offset), size - offset));
    return true;
}

void CSourceMdlSkeleton::ComputeBindMatrices()
{
    // Order-independent: walk parents first by exploiting that a valid skeleton is a tree and
    // that children always appear after their parent in the Source array is NOT guaranteed, so
    // we resolve iteratively until all bones have a computed bind.
    std::vector<bool> done(m_bones.size(), false);
    int remaining = static_cast<int>(m_bones.size());

    while (remaining > 0)
    {
        int progressed = 0;
        for (std::size_t i = 0; i < m_bones.size(); ++i)
        {
            if (done[i])
                continue;
            const BONE& b = m_bones[i];
            const mat4 local = Compose(b.pos, b.rotation);
            if (b.is_root)
            {
                m_bones[i].model_bind = local;
                done[i] = true;
                ++progressed;
            }
            else if (done[static_cast<std::size_t>(b.parent)])
            {
                // With row-vector points (p' = p * M) and translation in the LAST ROW, a local
                // (child-to-parent) transform and the parent's (parent-to-model) transform must be
                // composed as:  model_bind[child] = local * model_bind[parent].
                // That yields the correct "child-local -> model" frame (verified by the unit test,
                // where a +Y child of a +90deg-Z / +Y parent lands at (5,10,0)).
                m_bones[i].model_bind = Mul(local, m_bones[static_cast<std::size_t>(b.parent)].model_bind);
                done[i] = true;
                ++progressed;
            }
        }
        if (!progressed)
            break; // would be a cycle; leave unresolvable entries identity in adapter
        remaining = 0;
        for (const bool d : done)
            if (!d)
                ++remaining;
    }
}

void CSourceMdlSkeleton::Dump() const
{
    std::printf("Source MDL skeleton: %d bone(s), root=%d\n", (int)m_bones.size(), m_root);
    for (std::size_t i = 0; i < m_bones.size(); ++i)
    {
        const BONE& b = m_bones[i];
        std::printf("  [%d] %-24s parent=%d flags=%u  pos=(%.3f %.3f %.3f) quat=(%.3f %.3f %.3f %.3f)\n",
                    (int)i, b.name.c_str(), b.parent, b.flags,
                    b.pos.x, b.pos.y, b.pos.z, b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w);
    }
}
} // namespace SourceMdl
