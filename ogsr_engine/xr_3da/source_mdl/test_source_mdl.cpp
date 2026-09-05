//--------------------------------------------------------------------------------------------------
// test_rle_unit.cpp — known-answer тест RLE-декомпрессора (mstudioanimvalue_t).
//   g++ -std=c++17 -O2 -o test_rle test_rle_unit.cpp source_mdl_anim_decode.cpp
//--------------------------------------------------------------------------------------------------
#include "source_mdl_anim_decode.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <cstdint>

using namespace SourceMdl;

static int g_fail = 0;
static void check(bool ok, const char* m){ std::printf("  [%s] %s\n", ok?"ok":"FAIL", m); if(!ok) g_fail++; }

// Собираем RLE-поток из (valid,total,shorts[]) сегментов вручную.
static void put(std::vector<std::uint8_t>& b, int valid, int total, const std::vector<std::int16_t>& v)
{
    b.push_back((std::uint8_t)valid);
    b.push_back((std::uint8_t)total);
    for (auto x : v){ std::uint8_t lo=(std::uint8_t)(x&0xff), hi=(std::uint8_t)((x>>8)&0xff); b.push_back(lo); b.push_back(hi); }
}

int main()
{
    std::printf("== RLE decode ==\n");

    // Случай 1: 6 кадров, (valid=3,total=4, 100,-100,20) затем (valid=2,total=2, 7,8).
    //   Seg1: 3 свежих [100,-100,20]; repeat=1 (total-valid=1) -> повторить последнее (20).
    //          => кадры [100,-100,20,20]
    //   Seg2: 2 свежих [7,8]; repeat=0 => [7,8]
    //          => итог [100,-100,20,20,7,8]
    {
        std::vector<std::uint8_t> s;
        put(s, 3,4, {100,-100,20});
        put(s, 2,2, {7,8});
        std::vector<std::int16_t> out;
        check(DecodeRLEShorts(s.data(), s.size(), 6, out), "decodable");
        const std::int16_t want[6] = {100,-100,20,20,7,8};
        bool eq = (out.size()==6);
        for(int i=0;i<6;i++) if(!eq||out[i]!=want[i]) eq=false;
        check(eq, "6 frames [100,-100,20,20,7,8]");
    }

    // Случай 2: постоянное значение. (valid=1,total=6, 42) -> 42 повторяется 6 раз.
    {
        std::vector<std::uint8_t> s;
        put(s, 1,6, {42});
        std::vector<std::int16_t> out;
        check(DecodeRLEShorts(s.data(), s.size(), 6, out), "decodable");
        bool eq = (out.size()==6);
        for(int i=0;i<6;i++) if(!eq||out[i]!=42) eq=false;
        check(eq, "constant 42 x6");
    }

    // Случай 3: scaled (float).
    {
        std::vector<std::uint8_t> s;
        put(s, 1,6, {42});
        std::vector<float> f;
        check(DecodeAnimValues(s.data(), s.size(), 6, 0.5f, f), "decodable");
        bool eq = (f.size()==6);
        for(int i=0;i<6;i++) if(!eq||std::fabs(f[i]-21.0f)>1e-4f) eq=false;
        check(eq, "scaled 0.5 -> 21.0 x6");
    }

    // Случай 4: мальформация (поток закончился раньше времени) -> false.
    {
        std::vector<std::uint8_t> s;
        put(s, 5,5, {1,2}); // заявлено 5 свежих, но в потоке только 2 значений
        std::vector<std::int16_t> out;
        check(!DecodeRLEShorts(s.data(), s.size(), 6, out), "malformed returns false");
    }

    if(g_fail==0) std::printf("ALL RLE UNIT TESTS PASSED\n"); else std::printf("%d FAILURE(S)\n",g_fail);
    return g_fail?1:0;
}
