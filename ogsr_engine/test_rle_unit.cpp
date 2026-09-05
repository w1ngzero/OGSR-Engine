//--------------------------------------------------------------------------------------------------
// test_anim_decode_unit.cpp — known-answer тесты распаковки RAW-каналов (без реального ассета).
//   g++ -std=c++17 -O2 -o test_anim_decode_unit test_anim_decode_unit.cpp source_mdl_anim_decode.cpp
//--------------------------------------------------------------------------------------------------
#include "source_mdl_anim_decode.h"
#include <cstdio>
#include <cmath>
#include <cstdint>

using namespace SourceMdl;

static int g_fail = 0;

static void check(bool ok, const char* msg)
{
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", msg);
    if (!ok) g_fail++;
}

int main()
{
    std::printf("== Quaternion64 (RAWROT2) ==\n");
    // Реальные байты gfl2_asteria_arms.mdl bone0 RAWROT2 (проверены на ассете):
    //   decode -> q=(-0.54267,-0.45334,-0.45334,0.54267), |q|=1
    {
        const std::uint8_t raw[8] = {0x3e, 0x51, 0x67, 0xe4, 0x17, 0x8d, 0xfc, 0x22};
        AnimQ::Quat4 q;
        check(DecodeQuaternion64(raw, q), "decodable");
        const float n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        char buf[128];
        std::snprintf(buf, sizeof buf, "|q|==1 (got %.5f)", n);
        check(std::fabs(n - 1.0f) < 1e-4f, buf);
        std::snprintf(buf, sizeof buf, "x~-0.54267 (got %.5f)", q.x);
        check(std::fabs(q.x - (-0.54267f)) < 1e-3f, buf);
        std::snprintf(buf, sizeof buf, "w~0.54267 (got %.5f)", q.w);
        check(std::fabs(q.w - 0.54267f) < 1e-3f, buf);
    }
    // Знаковый бит w: установленный старший бит должен дать отрицательный w (норма проверяется).
    {
        std::uint8_t raw[8] = {0x3e, 0x51, 0x67, 0xe4, 0x17, 0x8d, 0xfc, 0x22};
        raw[7] |= 0x80; // wneg=1
        AnimQ::Quat4 q;
        check(DecodeQuaternion64(raw, q) && q.w < 0.0f, "wneg flips w sign");
        const float n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        check(std::fabs(n - 1.0f) < 1e-3f, "wneg keeps |q|==1");
    }

    std::printf("== Quaternion48 (RAWROT) ==\n");
    {
        // identity ~ (0,0,0,1)
        const std::int16_t q48[3] = {0, 0, 0};
        AnimQ::Quat4 q;
        check(DecodeQuaternion48(q48, q) && std::fabs(q.w - 1.0f) < 1e-3f, "zero -> w=1 (identity)");
        const float n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        check(std::fabs(n - 1.0f) < 1e-3f, "|q|==1");
        // 0.5,0.5,0.5 -> w2 = 1-0.75 = 0.25 -> w=0.5
        const std::int16_t q48b[3] = {16384, 16384, 16384};
        check(DecodeQuaternion48(q48b, q) && std::fabs(q.x - 0.5f) < 0.01f && std::fabs(q.w - 0.5f) < 0.01f,
              "0.5,0.5,0.5 -> w=0.5");
    }

    std::printf("== Vector48 (RAWPOS) ==\n");
    {
        const std::int16_t p[3] = {16384, -8192, 8192};
        const AnimQ::Vec3 v = DecodeVector48(p);
        check(std::fabs(v.x - 0.5f) < 1e-3f && std::fabs(v.y + 0.25f) < 1e-3f && std::fabs(v.z - 0.25f) < 1e-3f,
              "16384,-8192,8192 -> (0.5,-0.25,0.25)");
    }

    if (g_fail == 0)
        std::printf("ALL ANIM DECODE UNIT TESTS PASSED\n");
    else
        std::printf("%d FAILURE(S)\n", g_fail);
    return g_fail ? 1 : 0;
}
