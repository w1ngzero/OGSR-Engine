#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_anim_q.h — КВАНТОВАНИЕ анимации в X-Ray (чистое, без движка, тестируемое).
//
// X-Ray хранит скелетную анимацию в виде квантованных ключей (см. Layers/xrRender/
// AnimationKeyCalculate.h и xr_3da/SkeletonMotions.h):
//   * поворот:  CKeyQR = 4 x s16 (x,y,z,w).  Декодирование:  Q.c = s16 / KEY_Quant (=32767).
//   * сдвиг:    CKeyQT8 (3 x s8) или CKeyQT16 (3 x s16).
//               Декодирование:  T.c = s16 * _sizeT.c + _initT.c.
//   * SAMPLE_FPS = 30  =>  кадры идут с шагом 1/30 с.
//
// Этот модуль — ровно обратная (квантующая) сторона той же математики, поэтому проверяется
// на round-trip (квантовать -> декодировать == исходному). Используется адаптером
// source_mdl_anim_to_xray.cpp для заполнения CMotion (через типы int16, которые совпадают
// с CKeyQR / CKeyQT8 / CKeyQT16).
//--------------------------------------------------------------------------------------------------
#include <cstdint>
#include <cmath>
#include <vector>

namespace SourceMdl
{
namespace AnimQ
{
constexpr float kKeyQuant = 32767.f;         // == X-Ray KEY_Quant
constexpr float kKeyQuantI = 1.f / kKeyQuant; // == X-Ray KEY_QuantI
constexpr float kSampleFPS = 30.f;           // == X-Ray SAMPLE_FPS

struct Quat4 { float x, y, z, w; };
struct Vec3 { float x, y, z; };

// Квантовать поворот (компоненты уже в порядке x,y,z,w). -> 4 x s16 (как CKeyQR).
inline void QuantizeRot(const Quat4& q, int16_t out[4])
{
    out[0] = static_cast<int16_t>(std::lround(q.x * kKeyQuant));
    out[1] = static_cast<int16_t>(std::lround(q.y * kKeyQuant));
    out[2] = static_cast<int16_t>(std::lround(q.z * kKeyQuant));
    out[3] = static_cast<int16_t>(std::lround(q.w * kKeyQuant));
}

// Декодировать поворот (соответствует X-Ray QR2Quat).
inline Quat4 DequantizeRot(const int16_t in[4])
{
    return {in[0] * kKeyQuantI, in[1] * kKeyQuantI, in[2] * kKeyQuantI, in[3] * kKeyQuantI};
}

// Параметры квантования сдвига (== CMotion::_sizeT / _initT).
struct QuantParams
{
    float size[3]; // шаг на одну единицу отсчёта
    float init[3]; // значение при отсчёте == 0
};

// Вычислить size/init по диапазону положений (сдвиг). t16: true -> 16-бит, false -> 8-бит.
inline void FitTranslationRange(const float* positions, int count, bool t16, QuantParams& out)
{
    // count -- число кадров; positions -- массив xyz по кадрам.
    const float half = t16 ? 32767.f : 127.f;
    if (count <= 0 || !positions)
    {
        for (int c = 0; c < 3; ++c) { out.size[c] = 1.f; out.init[c] = 0.f; }
        return;
    }
    float mn[3], mx[3];
    for (int c = 0; c < 3; ++c) { mn[c] = 1e30f; mx[c] = -1e30f; }
    for (int i = 0; i < count; ++i)
        for (int c = 0; c < 3; ++c)
        {
            const float v = positions[i * 3 + c];
            if (v < mn[c]) mn[c] = v;
            if (v > mx[c]) mx[c] = v;
        }
    for (int c = 0; c < 3; ++c)
    {
        const float span = (mx[c] - mn[c]);
        out.size[c] = (span > 1e-9f) ? (span / (2.f * half)) : 1e-6f;
        out.init[c] = (mn[c] + mx[c]) * 0.5f; // середина диапазона -> отсчёт 0
    }
}

// Квантовать один сдвиг (-> s16, как CKeyQT8 (s8) либо CKeyQT16 (s16), в зависимости от t16).
inline void QuantizeT(const Vec3& t, const QuantParams& p, int16_t out[3])
{
    out[0] = static_cast<int16_t>(std::lround((t.x - p.init[0]) / p.size[0]));
    out[1] = static_cast<int16_t>(std::lround((t.y - p.init[1]) / p.size[1]));
    out[2] = static_cast<int16_t>(std::lround((t.z - p.init[2]) / p.size[2]));
}

// Декодировать сдвиг (соответствует X-Ray QT*_2T).
inline Vec3 DequantizeT(const int16_t in[3], const QuantParams& p)
{
    return {in[0] * p.size[0] + p.init[0],
            in[1] * p.size[1] + p.init[1],
            in[2] * p.size[2] + p.init[2]};
}
} // namespace AnimQ
} // namespace SourceMdl
