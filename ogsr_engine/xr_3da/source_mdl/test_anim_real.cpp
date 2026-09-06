//--------------------------------------------------------------------------------------------------
// test_anim_real.cpp — проверка декодирования каналов реального .mdl в кадры (V49AnimLayout100).
//
// Задачи:
//   1) Прочитать анимации ЧЕРЕЗ читатель (source_mdl_anim.cpp) ровно так, как это делает
//      движковый путь TryImportSourceAnimations (та же раскладка V49AnimLayout100).
//   2) Для последовательностей holster/draw/knife_fatal_03 убедиться, что кадры РЕАЛЬНО
//      меняются (не постоянная/identity-поза), т.е. моушен присутствует.
//   3) Убедиться, что декодированные кватернионы единичные (|q|=1) — признак корректного
//      RAWROT/RAWROT2 декодера.
//
// Это отдельная проверка "данные анимации есть и меняются". Если она зелёная, а нож в игре
// всё равно статичен — причина НЕ в декодировании каналов, а в привязке меш<->скелет или в
// проигрывании OMF (см. VERIFICATION.md).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_anim.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <model.mdl>\n", argv[0]);
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::printf("cannot open %s\n", argv[1]); return 2; }
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::size_t N = b.size();
    std::printf("mdl size = %zu\n", N);

    // Та же раскладка, что и в TryImportSourceAnimations(): V49AnimLayout100.
    std::vector<SourceMdl::ANIM_SEQ> seqs;
    SourceMdl::EAnimResult r = SourceMdl::ReadSourceAnims(b.data(), N, seqs, SourceMdl::V49AnimLayout100(), 256);
    std::printf("reader: %s (seqs=%d)\n", SourceMdl::AnimResultName(r), (int)seqs.size());
    if (r != SourceMdl::EAnimResult::Ok)
        return 2;

    int pass = 0, fail = 0;
    const char* const focus[] = {"holster", "draw", "knife_fatal_03", "knife_fatal_04", "idle"};
    for (const char* name : focus)
    {
        const SourceMdl::ANIM_SEQ* s = nullptr;
        for (const auto& sq : seqs)
            if (sq.name == name) { s = &sq; break; }
        if (!s) { std::printf("%-16s : NOT FOUND\n", name); ++fail; continue; }

        int varying = 0, total = 0;
        float maxBadNorm = 0.f, maxQ = 0.f;
        for (const auto& t : s->tracks)
        {
            ++total;
            if (t.frames.size() >= 2)
            {
                float md = 0.f;
                for (std::size_t k = 1; k < t.frames.size(); ++k)
                {
                    const auto& a = t.frames[0].rot, c = t.frames[k].rot;
                    const float d = std::fabs(a.x - c.x) + std::fabs(a.y - c.y) + std::fabs(a.z - c.z) + std::fabs(a.w - c.w);
                    if (d > md) md = d;
                }
                if (md > 0.001f) ++varying;
            }
            for (const auto& fr : t.frames)
            {
                const float n2 = fr.rot.x * fr.rot.x + fr.rot.y * fr.rot.y + fr.rot.z * fr.rot.z + fr.rot.w * fr.rot.w;
                if (std::fabs(n2 - 1.f) > maxBadNorm) maxBadNorm = std::fabs(n2 - 1.f);
                if (std::fabs(fr.rot.w) > maxQ) maxQ = std::fabs(fr.rot.w);
            }
        }
        const bool decays = varying > 0;
        const bool units = maxBadNorm < 0.01f;
        // Для idle главное — что он ЦИКЛИЧЕСКИЙ (иначе стоп-кадр); это не анимированная поза
        // движения (реальный "живой" idle здесь — a_idle_active, кадры меняются), а короткая
        // 2-кадровая поза. Поэтому проверяем loop, а не изменение кадров.
        bool ok;
        if (std::string(name) == "idle")
            ok = s->loop && units;
        else
            ok = decays && units;
        std::printf("%-16s : %s  tracks=%3d varyingRot=%3d loop=%d |q^2-1|max=%.4f  (nf=%d)\n",
                    name, ok ? "PASS" : "FAIL", total, varying, (int)s->loop, maxBadNorm, s->numframes);
        (ok ? pass : fail)++;
    }

    std::printf("\n=== %d PASS / %d FAIL ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
