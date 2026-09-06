//--------------------------------------------------------------------------------------------------
// source_mdl_anim_to_xray.cpp — движковый адаптер Source-анимации -> X-Ray CMotion.
//--------------------------------------------------------------------------------------------------
#include "stdafx.h"
#include "source_mdl_anim_to_xray.h"
#include "source_mdl_anim_q.h"
#include "../motion.h" // esmCycle/esmFX/esmStopAtEnd ...
#include "../fmesh.h"  // OGF_S_SMPARAMS / OGF_S_MOTIONS / xrOGF_SMParamsVersion
#include <cstring>

namespace SourceMdl
{
bool BuildXRayMotion(const ANIM_TRACK& track, CMotion& motion, bool doRotation,
                     bool doTranslation, bool useT16)
{
    if (track.frames.empty())
        return false;

    const int n = static_cast<int>(track.frames.size());
    motion.set_count(n);

    // ---- флаги ----
    motion.set_flags(0);
    if (!doRotation)
        motion.set_flag(flRKeyAbsent, TRUE);
    if (doTranslation)
    {
        motion.set_flag(flTKeyPresent, TRUE);
        if (useT16)
            motion.set_flag(flTKey16IsBit, TRUE);
    }

    // ---- появление ключей в заданных порядках ----
    std::vector<CKeyQR> rotKeys(n);
    std::vector<CKeyQT16> t16Keys(n);
    std::vector<CKeyQT8> t8Keys(n);

    // Сначала получаем все (неквантизованные) кадры.
    std::vector<AnimQ::Quat4> rots(n);
    std::vector<float> poss(n * 3);
    for (int f = 0; f < n; ++f)
    {
        rots[f] = track.frames[f].rot;
        poss[f * 3 + 0] = track.frames[f].pos.x;
        poss[f * 3 + 1] = track.frames[f].pos.y;
        poss[f * 3 + 2] = track.frames[f].pos.z;
    }

    // ---- квантование поворотов ----
    if (doRotation)
    {
        for (int f = 0; f < n; ++f)
        {
            int16_t q[4];
            AnimQ::QuantizeRot(rots[f], q);
            rotKeys[f].x = q[0]; rotKeys[f].y = q[1]; rotKeys[f].z = q[2]; rotKeys[f].w = q[3];
        }
        motion._keysR.create(static_cast<u32>(n), rotKeys.data());
    }
    else
    {
        // один R-ключ (первый кадр), присутствует всегда
        int16_t q[4];
        AnimQ::QuantizeRot(rots[0], q);
        CKeyQR k;
        k.x = q[0]; k.y = q[1]; k.z = q[2]; k.w = q[3];
        motion._keysR.create(1, &k);
    }

    // ---- квантование смещений ----
    if (doTranslation)
    {
        AnimQ::QuantParams p;
        AnimQ::FitTranslationRange(poss.data(), n, useT16, p);
        motion._sizeT.set(p.size[0], p.size[1], p.size[2]);
        motion._initT.set(p.init[0], p.init[1], p.init[2]);

        if (useT16)
        {
            for (int f = 0; f < n; ++f)
            {
                int16_t t[3];
                AnimQ::QuantizeT(track.frames[f].pos, p, t);
                t16Keys[f].x1 = t[0]; t16Keys[f].y1 = t[1]; t16Keys[f].z1 = t[2];
            }
            motion._keysT16.create(static_cast<u32>(n), t16Keys.data());
        }
        else
        {
            for (int f = 0; f < n; ++f)
            {
                int16_t t[3];
                AnimQ::QuantizeT(track.frames[f].pos, p, t);
                t8Keys[f].x1 = static_cast<s8>(t[0]); t8Keys[f].y1 = static_cast<s8>(t[1]); t8Keys[f].z1 = static_cast<s8>(t[2]);
            }
            motion._keysT8.create(static_cast<u32>(n), t8Keys.data());
        }
    }
    else
    {
        motion._initT.set(track.frames[0].pos.x, track.frames[0].pos.y, track.frames[0].pos.z);
    }

    return true;
}

int BuildXRayMotions(const ANIM_SEQ& seq, std::vector<CMotion>& outMotions, bool useT16)
{
    int built = 0;
    for (const ANIM_TRACK& tr : seq.tracks)
    {
        if (tr.bone >= 0 && tr.bone < static_cast<int>(outMotions.size()))
        {
            if (BuildXRayMotion(tr, outMotions[static_cast<std::size_t>(tr.bone)], true, !tr.frames.empty(), useT16))
                ++built;
        }
    }
    return built;
}

namespace
{
// Построить "hold"-трек кости: N кадров identity-позы (не один!) — потому что в OMF-формате
// у всех костей последовательности ОДИН общий счётчик кадров (len), и у не-анимированной кости
// движение должно иметь ровно N кадров, иначе кость "уедет" в garbage на кадрах >0.
inline ANIM_TRACK MakeHoldTrack(int bone, int nframes)
{
    ANIM_TRACK tr;
    tr.bone = bone;
    tr.delta = false;
    tr.frames.reserve(static_cast<std::size_t>(nframes > 0 ? nframes : 1));
    for (int f = 0; f < (nframes > 0 ? nframes : 1); ++f)
    {
        ANIM_FRAME f0;
        f0.rot = {0.f, 0.f, 0.f, 1.f}; // identity
        f0.pos = {0.f, 0.f, 0.f};
        tr.frames.push_back(f0);
    }
    return tr;
}

// Баговый примитивным записыватель LE-байтов (не в ядре OMF, а для сборки потока).
void W8(std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back(v); }
void W16(std::vector<std::uint8_t>& b, std::uint16_t v)
{
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}
void W32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}
void WFloat(std::vector<std::uint8_t>& b, float f)
{
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    W32(b, u);
}
void WStr(std::vector<std::uint8_t>& b, const char* s)
{
    if (!s)
        s = "";
    while (*s)
        b.push_back(static_cast<std::uint8_t>(*s++));
    b.push_back(0);
}
// Пишет чанк-контейнер id;size;payload (как IWriter::open_chunk).
void WChunk(std::vector<std::uint8_t>& b, std::uint32_t id, std::vector<std::uint8_t>& payload)
{
    W32(b, id);
    W32(b, static_cast<std::uint32_t>(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
}

// Найти трек кости в последовательности (иначе nullptr).
const ANIM_TRACK* FindTrack(const ANIM_SEQ& seq, int bone)
{
    for (const ANIM_TRACK& t : seq.tracks)
        if (t.bone == bone)
            return &t;
    return nullptr;
}
} // namespace

// Сериализует декодированные Source-последовательности в байты движкового OMF (chunks
// OGF_S_SMPARAMS + OGF_S_MOTIONS) — ровно так, как читает motions_value::load(). Поток затем
// отдаётся стандартному загрузчику (shared_motions::create(key, CTempReader(байты), bones)),
// который сам собирает partitions/motion-defs/per-bone CMotion (проверено на round-trip см.
// VERIFICATION.md Раунд 15). Это обходит приватные поля CMotionDef (speed/power/accrue/falloff).
bool BuildXRayMotionsOMF(const std::vector<ANIM_SEQ>& seqs, const vecBones* bones,
                         std::vector<std::uint8_t>& outBytes, bool useT16)
{
    outBytes.clear();
    if (!bones || bones->empty())
    {
        Msg("!! [SourceMotions] no bones to build motion stream from");
        return false;
    }
    const u32 nBones = static_cast<u32>(bones->size());
    const u32 nSeq = static_cast<u32>(seqs.size()); // может быть 0 -> валидный пустой набор (0 движений)

    // === OGF_S_SMPARAMS ===
    std::vector<std::uint8_t> SP;
    W16(SP, static_cast<std::uint16_t>(xrOGF_SMParamsVersion)); // 4
    W16(SP, 1);                                                // part_count
    // part 0 "default" — все кости (identity-remap: m_idx == engine bone index)
    WStr(SP, "default");
    W16(SP, static_cast<std::uint16_t>(nBones));
    for (u32 b = 0; b < nBones; ++b)
    {
        WStr(SP, (*bones)[b]->name.c_str());
        W32(SP, b);
    }
    W16(SP, static_cast<std::uint16_t>(nSeq)); // mot_count
    for (u32 s = 0; s < nSeq; ++s)
    {
        WStr(SP, seqs[s].name.c_str());
        const std::uint32_t dwFlags = seqs[s].loop ? 0u : (esmStopAtEnd);
        W32(SP, dwFlags);
        W16(SP, 0);    // bone_or_part = part 0
        W16(SP, static_cast<std::uint16_t>(s)); // motion id
        WFloat(SP, 1.f); // speed
        WFloat(SP, 1.f); // power
        WFloat(SP, 1.f); // accrue
        WFloat(SP, 1.f); // falloff
        W32(SP, 0u);     // версия>=4: cnt(marks)=0
    }

    // === OGF_S_MOTIONS ===
    std::vector<std::uint8_t> MS;
    W32(MS, nSeq); // dwCNT
    for (u32 s = 0; s < nSeq; ++s)
    {
        WStr(MS, seqs[s].name.c_str());
        // Единое число кадров на последовательность (source numframes; все каналы приведены к нему).
        const std::uint32_t dwLen = seqs[s].numframes > 0 ? static_cast<std::uint32_t>(seqs[s].numframes) : 1u;
        W32(MS, dwLen);
        for (u32 b = 0; b < nBones; ++b)
        {
            ANIM_TRACK hold;
            const ANIM_TRACK* tr = FindTrack(seqs[s], static_cast<int>(b));
            if (!tr || tr->frames.empty())
            {
                hold = MakeHoldTrack(static_cast<int>(b), static_cast<int>(dwLen));
                tr = &hold;
            }
            CMotion M;
            BuildXRayMotion(*tr, M, true, true, useT16);

            const bool rAbsent = M.test_flag(flRKeyAbsent);
            const bool tPresent = M.test_flag(flTKeyPresent);
            const bool t16 = M.test_flag(flTKey16IsBit);
            std::uint8_t flags = 0;
            if (rAbsent) flags |= flRKeyAbsent;
            if (tPresent) flags |= flTKeyPresent;
            if (t16 && tPresent) flags |= flTKey16IsBit;
            W8(MS, flags);

            if (rAbsent)
            {
                W16(MS, M._keysR[0].x); W16(MS, M._keysR[0].y); W16(MS, M._keysR[0].z); W16(MS, M._keysR[0].w);
            }
            else
            {
                W32(MS, 0u); // crc (не проверяется загрузчиком)
                for (std::uint32_t f = 0; f < dwLen; ++f)
                {
                    W16(MS, M._keysR[f].x); W16(MS, M._keysR[f].y); W16(MS, M._keysR[f].z); W16(MS, M._keysR[f].w);
                }
            }

            if (tPresent)
            {
                W32(MS, 0u); // crc
                if (t16)
                    for (std::uint32_t f = 0; f < dwLen; ++f)
                    {
                        W16(MS, M._keysT16[f].x1); W16(MS, M._keysT16[f].y1); W16(MS, M._keysT16[f].z1);
                    }
                else
                    for (std::uint32_t f = 0; f < dwLen; ++f)
                    {
                        W8(MS, static_cast<std::uint8_t>(M._keysT8[f].x1));
                        W8(MS, static_cast<std::uint8_t>(M._keysT8[f].y1));
                        W8(MS, static_cast<std::uint8_t>(M._keysT8[f].z1));
                    }
                WFloat(MS, M._sizeT.x); WFloat(MS, M._sizeT.y); WFloat(MS, M._sizeT.z);
                WFloat(MS, M._initT.x); WFloat(MS, M._initT.y); WFloat(MS, M._initT.z);
            }
            else
            {
                WFloat(MS, M._initT.x); WFloat(MS, M._initT.y); WFloat(MS, M._initT.z);
            }
        }
    }

    WChunk(outBytes, OGF_S_SMPARAMS, SP);
    WChunk(outBytes, OGF_S_MOTIONS, MS);
    return true;
}
} // namespace SourceMdl
