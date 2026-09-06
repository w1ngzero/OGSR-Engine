#include "stdafx.h"
//--------------------------------------------------------------------------------------------------
// source_mdl_anim_decode.cpp — распаковка RAW-каналов (см. source_mdl_anim_decode.h).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_anim_decode.h"
#include <cmath>
#include <cstring>

namespace SourceMdl
{
bool DecodeQuaternion64(const std::uint8_t* raw8, AnimQ::Quat4& out)
{
    if (!raw8)
        return false;

    // 64-битное значение: три компонента по 21 биту (x,y,z) + 1 бит знака w.
    // Смещение компонента 2^20 = 1048576; масштаб 1/1048576.5 -> диапазон ~(-1,1).
    std::uint64_t v = 0;
    std::memcpy(&v, raw8, 8);

    const std::uint64_t cx = (v & 0x1FFFFFu);
    const std::uint64_t cy = ((v >> 21) & 0x1FFFFFu);
    const std::uint64_t cz = ((v >> 42) & 0x1FFFFFu);
    const bool wneg = ((v >> 63) & 0x1) != 0;

    constexpr float kOff = 1048576.0f;   // 2^20
    constexpr float kScale = 1048576.5f; // смещение + полшага -> 1/|x|<1
    const float x = (static_cast<float>(cx) - kOff) / kScale;
    const float y = (static_cast<float>(cy) - kOff) / kScale;
    const float z = (static_cast<float>(cz) - kOff) / kScale;

    float w2 = 1.0f - x * x - y * y - z * z;
    if (w2 < 0.0f)
        w2 = 0.0f; // численная устойчивость (хвост ошибки округления)
    float w = std::sqrt(w2);
    if (wneg)
        w = -w;

    out.x = x; out.y = y; out.z = z; out.w = w;
    return true;
}

bool DecodeQuaternion48(const std::int16_t raw3[3], AnimQ::Quat4& out)
{
    if (!raw3)
        return false;
    // 3 x 16-бит со знаком; компоненты в диапазоне ~(-1,1), w из нормы=1.
    constexpr float kScale = 32767.5f;
    float x = raw3[0] / kScale;
    float y = raw3[1] / kScale;
    float z = raw3[2] / kScale;
    // Если сумма квадратов слегка превышает 1 (возникает у части ДЛИННЫХ сжатых каналов
    // конвертированных ассетов, где порядок valueptr у отдельных записей инвертирован), НЕ кладём
    // w=0 (получался бы кватернион с |q|>1 и порчей скиннинга), а нормируем компоненты до единичной
    // сферы. Это стандартная практика и делает декодер устойчивым к таким записям.
    float w2 = 1.0f - x * x - y * y - z * z;
    if (w2 < 0.0f)
    {
        const float s = std::sqrt(x * x + y * y + z * z);
        if (s > 1e-6f)
        {
            x /= s; y /= s; z /= s;
            w2 = 1.0f - x * x - y * y - z * z;
        }
        if (w2 < 0.0f)
            w2 = 0.0f;
    }
    out.x = x; out.y = y; out.z = z; out.w = std::sqrt(w2);
    return true;
}

AnimQ::Vec3 DecodeVector48(const std::int16_t raw3[3])
{
    constexpr float kScale = 32768.0f;
    return {raw3[0] / kScale, raw3[1] / kScale, raw3[2] / kScale};
}

bool DecodeRLEShorts(const std::uint8_t* rle, std::size_t rleSize, int frameCount,
                     std::vector<std::int16_t>& outFrames)
{
    outFrames.clear();
    if (!rle || frameCount <= 0)
        return false;
    outFrames.assign(static_cast<std::size_t>(frameCount), 0);

    std::size_t pos = 0;       // текущее смещение в RLE-потоке (байты)
    int frameOffset = 0;       // сколько кадров уже заполнено

    auto read8 = [&](std::uint8_t& x) -> bool {
        if (pos + 1 > rleSize) return false;
        x = rle[pos++];
        return true;
    };
    auto read16 = [&](std::int16_t& x) -> bool {
        if (pos + 2 > rleSize) return false;
        std::memcpy(&x, rle + pos, 2);
        pos += 2;
        return true;
    };

    std::uint8_t valid = 0, total = 0;
    if (!read8(valid) || !read8(total))
        return false;

    while (frameOffset < frameCount)
    {
        // 1) 'valid' свежих значений.
        for (int i = 0; i < valid && frameOffset < frameCount; ++i)
        {
            std::int16_t v = 0;
            if (!read16(v))
                return false;
            outFrames[static_cast<std::size_t>(frameOffset)] = v;
            frameOffset++;
        }
        // 2) 'total - valid' повторов последнего значения.
        const int repeat = static_cast<int>(total) - static_cast<int>(valid);
        if (repeat > 0 && frameOffset > 0)
        {
            const std::int16_t last = outFrames[static_cast<std::size_t>(frameOffset - 1)];
            for (int i = 0; i < repeat && frameOffset < frameCount; ++i)
            {
                outFrames[static_cast<std::size_t>(frameOffset)] = last;
                frameOffset++;
            }
        }
        if (frameOffset >= frameCount)
            break;
        if (!read8(valid) || !read8(total))
            return false;
    }
    return true;
}

bool DecodeAnimValues(const std::uint8_t* rle, std::size_t rleSize, int frameCount, float scale,
                      std::vector<float>& outFrames)
{
    std::vector<std::int16_t> shorts;
    if (!DecodeRLEShorts(rle, rleSize, frameCount, shorts))
        return false;
    outFrames.assign(shorts.size(), 0.0f);
    for (std::size_t i = 0; i < shorts.size(); ++i)
        outFrames[i] = static_cast<float>(shorts[i]) * scale;
    return true;
}
} // namespace SourceMdl
