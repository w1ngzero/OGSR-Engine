//--------------------------------------------------------------------------------------------------
// source_mdl_anim_to_xray.cpp — движковый адаптер Source-анимации -> X-Ray CMotion.
//--------------------------------------------------------------------------------------------------
#include "stdafx.h"
#include "source_mdl_anim_to_xray.h"
#include "source_mdl_anim_q.h"

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
} // namespace SourceMdl
