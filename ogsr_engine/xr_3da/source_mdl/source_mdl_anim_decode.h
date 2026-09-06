#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_anim_decode.h — распаковка постоянных RAW-каналов Source-анимации в float.
// ЧИСТЫЙ модуль (без движка), проверяемый юнит-тестом.
//
// В Source «RAW»-каналы (STUDIO_ANIM_RAWPOS/RAWROT/RAWROT2) описывают НЕВАРЬИРУЕМУЮ во времени
// часть анимации: в файле лежит ОДНО значение (поворот/сдвиг), общее для всех кадров
// последовательности. «Сжатые» каналы (ANIMPOS/ANIMROT, RLE) — варьируемая часть, они в этом
// модуле не раскрываются (у реального ассета их нет; см. VERIFICATION.md).
//
// Форматы:
//   Quaternion64 (RAWROT2, 8 байт): три 21-битных компонента x,y,z со смещением 2^20 + 1 бит
//     знака w. Распаковка (см. реализацию): x=(v-2^20)/2^20.5; w = sqrt(1-x^2-y^2-z^2),
//     знак w — по старшему биту. ПРОВЕРЕНО на реальном gfl2_asteria_arms.mdl: даёт единичный
//     кватернион.
//   Quaternion48 (RAWROT, 6 байт): три 16-битных x,y,z; w вычисляется из условия нормы=1.
//   Vector48 (RAWPOS, 6 байт): три 16-битных x,y,z (сдвиг, масштаб 1/32768).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_anim_q.h"
#include <cstdint>
#include <cstddef>
#include <vector>

namespace SourceMdl
{
// Распаковать RAWROT2 (Quaternion64, 8 байт) в кватернион.
bool DecodeQuaternion64(const std::uint8_t* raw8, AnimQ::Quat4& out);

// Распаковать RAWROT (Quaternion48, 3 x int16) в кватернион.
bool DecodeQuaternion48(const std::int16_t raw3[3], AnimQ::Quat4& out);

// Распаковать RAWPOS (Vector48, 3 x int16) в сдвиг.
AnimQ::Vec3 DecodeVector48(const std::int16_t raw3[3]);

// Распаковать RLE-поток mstudioanimvalue_t в per-frame значения SAMPLE (int16).
// Формат (подтверждён по SourceIO / мстрелке Valve; см. source_mdl_anim_decode.cpp):
//   последовательность байтовых пар (valid, total). Для каждой пары:
//     - valid: сколько НОВЫХ значений (int16) читается подряд (первые кадры сегмента);
//     - total-valid: сколько следующих кадров ПОВТОРЯЮТ последнее прочитанное значение.
//   Признак конца — frameOffset >= frameCount.
//   raw8/raw16 — указатель на начало RLE-потока; frameCount — число кадров.
// Возвращает false, если поток раньше времени закончился (мальформированный).
bool DecodeRLEShorts(const std::uint8_t* rle, std::size_t rleSize, int frameCount,
                     std::vector<std::int16_t>& outFrames);

// Распаковать RLE-поток в per-frame SCALED значения (float) — умножение на scale.
bool DecodeAnimValues(const std::uint8_t* rle, std::size_t rleSize, int frameCount, float scale,
                      std::vector<float>& outFrames);
} // namespace SourceMdl
