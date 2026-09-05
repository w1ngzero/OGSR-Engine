//--------------------------------------------------------------------------------------------------
// test_anim_real.cpp — проверка декодирования RAWROT2 (Quaternion64) в реальные кадры.
//
// Извлекает байтовый канал RAWROT2 напрямую из .mdl и декодирует его в кватернион,
// проверяя что |q| == 1 (единичный). Также показывает, что кадры последовательности,
// заполненные читателем (source_mdl_anim.cpp), совпадают с этим кватернионом.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_anim.h"
#include "source_mdl_anim_decode.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

enum { kLastAnimCount = 1, kNewAnimCount = 3 };

bool ReadAt(const unsigned char* b, size_t size, size_t off, void* out, size_t n)
{
    if (off + n > size) return false;
    std::memcpy(out, b + off, n);
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <model.mdl>\n", argv[0]);
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 2;
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> b((size_t)sz);
    if (std::fread(b.data(), 1, (size_t)sz, f) != (size_t)sz) return 2;
    std::fclose(f);
    const size_t N = b.size();

    std::printf("mdl size = %zu\n", N);

    // Прочитаем через читатель анимаций (v49) уже заполненные кадры.
    std::vector<SourceMdl::ANIM_SEQ> seqs;
    SourceMdl::EAnimResult r = SourceMdl::ReadSourceAnims(b.data(), N, seqs, SourceMdl::V49AnimLayout(), 49);
    std::printf("reader: %s (seqs=%d)\n", SourceMdl::AnimResultName(r), (int)seqs.size());
    if (r != SourceMdl::EAnimResult::Ok)
        return 2;

    // Каналы RAWROT2 на 14224 (v49, animindex=108 от базы 14116).
    const std::size_t animCh = 14224;
    std::printf("anim channel at %zu: bone=%d flags=0x%02x\n", animCh, b[animCh], b[animCh + 1]);
    const std::uint8_t* raw = &b[animCh + 4]; // после mstudioanim_t {bone,flags,nextoffset}

    SourceMdl::AnimQ::Quat4 q;
    if (!SourceMdl::DecodeQuaternion64(raw, q))
    {
        std::printf("decoder FAIL\n");
        return 2;
    }
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    std::printf("decoded q=(%.5f,%.5f,%.5f,%.5f) |q|=%.5f\n", q.x, q.y, q.z, q.w, n);
    if (std::fabs(n - 1.0f) > 0.005f)
    {
        std::printf("FAIL: |q| != 1\n");
        return 2;
    }
    const bool nonIdentity = std::fabs(q.x) > 1e-4f || std::fabs(q.y) > 1e-4f || std::fabs(q.z) > 1e-4f ||
                             std::fabs(q.w - 1.0f) > 1e-4f;
    std::printf("non-identity rotation: %s\n", nonIdentity ? "yes" : "no (identity)");

    // Сверка с кадрами читателя (bone0, оба кадра == q).
    if (seqs.empty() || seqs[0].tracks.empty())
    {
        std::printf("FAIL: no tracks\n");
        return 2;
    }
    const auto& tr = seqs[0].tracks[0];
    if (tr.bone != 0)
    {
        std::printf("FAIL: track bone=%d (expected 0)\n", tr.bone);
        return 2;
    }
    std::printf("reader frames for bone0 = %d (seq.numframes=%d)\n", (int)tr.frames.size(), seqs[0].numframes);
    for (std::size_t i = 0; i < tr.frames.size(); ++i)
    {
        const auto& fq = tr.frames[i].rot;
        const float d = std::fabs(fq.x - q.x) + std::fabs(fq.y - q.y) + std::fabs(fq.z - q.z) + std::fabs(fq.w - q.w);
        std::printf("  frame %zu: q=(%.5f,%.5f,%.5f,%.5f) diff=%.6f\n", i, fq.x, fq.y, fq.z, fq.w, d);
        if (d > 0.01f)
        {
            std::printf("FAIL: frame != decoded q\n");
            return 2;
        }
    }

    std::printf("ALL ANIM DECODE CHECKS PASSED\n");
    return 0;
}
