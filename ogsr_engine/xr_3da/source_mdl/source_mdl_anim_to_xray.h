#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_anim_to_xray.h — движковый адаптер: Source-анимация -> X-Ray CMotion.
//
// Заполняет движковый CMotion (см. xr_3da/SkeletonMotions.h) из уже разобранного ANIM_TRACK
// (source_mdl_anim.h), с квантованием через source_mdl_anim_q.h. Соответствие полей:
//   * rotation  -> CKeyQR (через AnimQ::QuantizeRot)
//   * pos       -> CKeyQT16/CKeyQT8 (через AnimQ::FitTranslationRange + QuantizeT)
//   * count     -> numframes, _sizeT/_initT -> AnimQ::QuantParams
//
// Обратите внимание: файл требует движок (CMotion, CKeyQR, ref_smem, g_pSharedMemoryContainer),
// поэтому здесь он не компилируется standalone; он компилируется в составе XR_3DA.
//--------------------------------------------------------------------------------------------------
#include "../SkeletonMotions.h"
#include "source_mdl_anim.h"

namespace SourceMdl
{
// Заполняет один CMotion из по-костного трека.
//   track        -- трек (Source-кадры, координаты Source)
//   motion       -- целевой CMotion (уже выделенный)
//   doRotation   -- писать повороты (если false -- R-ключ отсутствует)
//   doTranslation-- писать смещения (если false -- только _initT из первого кадра)
//   useT16       -- 16-битная квантизация смещений (иначе 8-битная)
bool BuildXRayMotion(const ANIM_TRACK& track, CMotion& motion, bool doRotation = true,
                     bool doTranslation = true, bool useT16 = true);

// Заполняет vec-векторов CMotion для последовательности. Возвращает количество заполненных.
//   outMotions   -- вектор CMotion (индекс == индекс кости); ожидается, что уже resized
//   seq          -- разобранная последовательность
int BuildXRayMotions(const ANIM_SEQ& seq, std::vector<CMotion>& outMotions, bool useT16 = true);

// Сериализует декодированные Source-последовательности в байты движкового OMF
// (chunks OGF_S_SMPARAMS + OGF_S_MOTIONS, формат motions_value::load()).
// Поток отдаётся стандартному загрузчику shared_motions::create(key, CTempReader(байты), bones),
// который сам собирает partitions/CMotionDef/per-bone CMotion. Это обходит приватные поля
// CMotionDef и reuses проверенную загрузку.
//   seqs       -- декодированные последовательности (source_mdl_anim.h)
//   bones      -- движковые кости (для имён/сопоставления индексов)
//   outBytes   -- получаемые байты OMF
//   useT16     -- 16-битная квантизация смещений
//   ВАЖНО: движковая интеграция; итоговое положение костей/поток кадров проверяется on-screen.
bool BuildXRayMotionsOMF(const std::vector<ANIM_SEQ>& seqs, const vecBones* bones,
                         std::vector<std::uint8_t>& outBytes, bool useT16 = true);
} // namespace SourceMdl
