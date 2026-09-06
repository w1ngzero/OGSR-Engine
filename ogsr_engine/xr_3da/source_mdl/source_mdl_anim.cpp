#include "stdafx.h"
//--------------------------------------------------------------------------------------------------
// source_mdl_anim.cpp — реализация читателя анимаций Source .MDL (см. source_mdl_anim.h).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_anim.h"
#include "source_mdl_anim_decode.h"
#include <cstring>

namespace SourceMdl
{
namespace
{
template <typename T>
bool ReadAt(const std::uint8_t* base, std::size_t size, std::size_t off, T& out)
{
    if (off + sizeof(T) > size)
        return false;
    std::memcpy(&out, base + off, sizeof(T));
    return true;
}
bool InRange(std::size_t off, std::size_t n, std::size_t size)
{
    return off <= size && n <= size - off;
}
} // namespace

const char* AnimResultName(EAnimResult r)
{
    switch (r)
    {
    case EAnimResult::Ok: return "ok";
    case EAnimResult::NotSourceMdl: return "not a Source MDL (bad id)";
    case EAnimResult::UnsupportedVersion: return "unsupported version";
    case EAnimResult::NoSequences: return "no sequences";
    case EAnimResult::UnsupportedFormat: return "unsupported anim format (compressed/IK/events)";
    case EAnimResult::MalformedBuffer: return "buffer out of range";
    }
    return "unknown";
}

const char* AnimBoneFormatName(EAnimBoneFormat f)
{
    switch (f)
    {
    case EAnimBoneFormat::Raw: return "raw(uncompressed)";
    case EAnimBoneFormat::Compressed: return "compressed";
    case EAnimBoneFormat::Unknown: return "unknown";
    }
    return "?";
}

// Правдоподобность числа кадров (для автодетекта страйда animdesc). Реальная частота кадров
// у Source-анимаций редко превышает несколько сотен; 0 и гигантские значения — признак того,
// что поле прочитано по НЕПРАВИЛЬНОМУ смещению/страйду раскладки.
inline bool FrameCountPlausible(int n)
{
    return n > 0 && n <= 4096;
}

// Автодетект страйда базы-анимации mstudioanimdesc_t. Пробует кандидаты (страйд из раскладки,
// затем стандартный v49 92 и нестандартный 100) и выбирает тот, у которого правдоподобное
// число кадров прочитывается у НАИБОЛЬШЕГО числа анимаций. Это делает читатель устойчивым и к
// стандартной v49, и к нестандартным GMod-моделям (страйд=100) — без ручного выбора раскладки.
int DetectAnimDescStride(const std::uint8_t* base, std::size_t size, int numlocalanim, int localanimindex,
                         const ANIM_LAYOUT& L)
{
    const int candidates[3] = {L.adb_stride, 92, 100};
    int best = L.adb_stride, bestScore = -1;
    for (int c = 0; c < 3; ++c)
    {
        const int stride = candidates[c];
        if (stride <= 0)
            continue;
        int score = 0;
        for (int a = 0; a < numlocalanim && a < 4096; ++a)
        {
            const std::size_t adb =
                static_cast<std::size_t>(localanimindex) + static_cast<std::size_t>(a) * static_cast<std::size_t>(stride);
            if (!InRange(adb, 20, size))
                break;
            std::int32_t nf = 0;
            std::memcpy(&nf, base + adb + static_cast<std::size_t>(L.adb_numframes_off), 4);
            if (FrameCountPlausible(nf))
                ++score;
        }
        if (score > bestScore)
        {
            bestScore = score;
            best = stride;
        }
    }
    return best;
}

EAnimResult ReadSourceAnims(const void* data, std::size_t size, std::vector<ANIM_SEQ>& outSeqs,
                            const ANIM_LAYOUT& L, const int numBones)
{
    outSeqs.clear();
    if (!data || size < 300)
        return EAnimResult::MalformedBuffer;

    const std::uint8_t* base = static_cast<const std::uint8_t*>(data);

    std::uint32_t id = 0, version = 0;
    if (!ReadAt(base, size, 0, id) || !ReadAt(base, size, 4, version))
        return EAnimResult::MalformedBuffer;
    if (id != 0x54534449u) // 'IDST' little-endian; verified on real Source v49
        return EAnimResult::NotSourceMdl;
    if (version < 44 || version > 49)
        return EAnimResult::UnsupportedVersion;

    std::int32_t numlocalseq = 0, localseqindex = 0;
    if (!ReadAt(base, size, static_cast<std::size_t>(L.numlocalseq_off), numlocalseq) ||
        !ReadAt(base, size, static_cast<std::size_t>(L.localseqindex_off), localseqindex))
        return EAnimResult::MalformedBuffer;

    if (numlocalseq <= 0 || localseqindex < 0)
        return EAnimResult::NoSequences;

    // База-анимация (mstudioanimdesc_t): stride авто-определяется (92 для v49, 100 для GMod).
    std::int32_t numlocalanim = 0, localanimindex = 0;
    if (!ReadAt(base, size, static_cast<std::size_t>(L.numlocalanim_off), numlocalanim) ||
        !ReadAt(base, size, static_cast<std::size_t>(L.localanimindex_off), localanimindex))
        return EAnimResult::MalformedBuffer;
    const int adb_stride = DetectAnimDescStride(base, size, numlocalanim, localanimindex, L);
    const std::size_t adbBase = static_cast<std::size_t>(localanimindex);

    outSeqs.reserve(static_cast<std::size_t>(numlocalseq));

    for (std::int32_t s = 0; s < numlocalseq; ++s)
    {
        const std::size_t seq_off =
            static_cast<std::size_t>(localseqindex) + static_cast<std::size_t>(s) * static_cast<std::size_t>(L.seq_stride);
        if (!InRange(seq_off, static_cast<std::size_t>(L.seq_stride), size))
            return EAnimResult::MalformedBuffer;

        ANIM_SEQ seq;

        // Имя: относительное смещение от начала записи последовательности.
        std::int32_t sznameindex = 0;
        if (!ReadAt(base, size, seq_off + static_cast<std::size_t>(L.seq_sznameindex_off), sznameindex))
            return EAnimResult::MalformedBuffer;
        const std::size_t name_off = seq_off + static_cast<std::size_t>(sznameindex);
        if (name_off >= size)
            return EAnimResult::MalformedBuffer;
        std::size_t nlen = strnlen(reinterpret_cast<const char*>(base + name_off), size - name_off);
        seq.name.assign(reinterpret_cast<const char*>(base + name_off), nlen);
        for (auto& c : seq.name)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        std::int32_t numframes = 0, animindex = 0;
        float fps = 30.f;
        // v49 (и др., где seq_numframes_off == -1): numframes/fps живут в БАЗОВОЙ анимации
        // mstudioanimdesc_t (индекс-связан со последовательностью), а не в seqdesc.
        if (L.seq_numframes_off >= 0)
        {
            if (!ReadAt(base, size, seq_off + static_cast<std::size_t>(L.seq_numframes_off), numframes) ||
                !ReadAt(base, size, seq_off + static_cast<std::size_t>(L.seq_animindex_off), animindex))
                return EAnimResult::MalformedBuffer;
            // fps может быть прочитан как int-байты; читаем как float напрямую.
            std::memcpy(&fps, base + seq_off + static_cast<std::size_t>(L.seq_fps_off), 4);
        }
        else
        {
            // Базовая анимация для этой последовательности (индекс = индекс последовательности).
            if (s >= numlocalanim)
                return EAnimResult::MalformedBuffer;
            const std::size_t adb = adbBase + static_cast<std::size_t>(s) * static_cast<std::size_t>(adb_stride);
            if (!InRange(adb, 64, size))
                return EAnimResult::MalformedBuffer;
            // mstudioanimdesc_t (auto-страйд): fps@8, flags@12, numframes@16, animindex@56.
            std::memcpy(&fps, base + adb + static_cast<std::size_t>(L.adb_fps_off), 4);
            if (!ReadAt(base, size, adb + static_cast<std::size_t>(L.adb_numframes_off), numframes) ||
                !ReadAt(base, size, adb + static_cast<std::size_t>(L.adb_animindex_off), animindex))
                return EAnimResult::MalformedBuffer;
        }

        seq.numframes = numframes;
        // fps: принимаем только правдоподобный диапазон (иначе мусорное значение из несовпадающей
        // раскладки animdesc улетело бы в playback-скорость; на частоту кадров оно всё равно не влияет).
        seq.fps = (fps > 1.f && fps < 120.f) ? static_cast<float>(fps) : 30.f;
        // numframes: если число кадров неправдоподобно велико — раскладка animdesc этого файла не
        // совпадает с текущей (см. несовпадающий stride/offset). Не резервируем миллионы кадров
        // (иначе bad_alloc), а просто пропускаем такую последовательность как нечитаемую.
        if (seq.numframes <= 0 || seq.numframes > 4096)
            continue; // пропускаем пустые/несовпадающие последовательности (без краша)

        // Цикличность: читаем int flags из mstudioseqdesc_t и смотрим STUDIO_LOOPING (0x100).
        // ВАЖНО: раньше seq.loop НИГДЕ не присваивался и был неинициализирован (UB) — из-за этого
        // моушены вроде knife_fatal_03/holster случайно считались ЦИКЛАМИ, тогда как были одноразовыми.
        // Одноразовые (не loop) должны быть esmStopAtEnd, иначе weapon-state machine застревает
        // (motion_length()==0 -> OnAnimationEnd никогда не срабатывает).
        seq.loop = false;
        if (L.seq_flags_off >= 0)
        {
            std::int32_t seq_flags = 0;
            if (ReadAt(base, size, seq_off + static_cast<std::size_t>(L.seq_flags_off), seq_flags))
                seq.loop = ((static_cast<std::uint32_t>(seq_flags) & 0x0100u) != 0) || // STUDIO_LOOPING
                            ((static_cast<std::uint32_t>(seq_flags) & 0x0001u) != 0); // STUDIO_LOOP
        }
        // У idle флаги=0x0 (ни STUDIO_LOOPING, ни STUDIO_LOOP) у некоторых GMod-моделей, но idle
        // ОБЯЗАН быть циклом (иначе motion_length()==0 -> OnAnimationEnd для него не сработает и
        // weapon-state machine зависнет на стоп-кадре). Принудительно зацикливаем по имени.
        if (!seq.loop && (seq.name == "idle"))
            seq.loop = true;

        // Для v49 каналы идут от базы-анимации: animindex (из animdesc) = смещение от НАЧАЛА
        // animdesc к mstudioanim_t. Для классики — массив short-смещений (первое = к первому каналу).
        std::size_t anim_abs = 0;
        if (L.seq_numframes_off < 0)
        {
            const std::size_t adb = adbBase + static_cast<std::size_t>(s) * static_cast<std::size_t>(adb_stride);
            anim_abs = adb + static_cast<std::size_t>(animindex);
        }
        else
        {
            std::int16_t firstAnimOff = 0;
            if (!InRange(static_cast<std::size_t>(animindex), 2, size) ||
                !ReadAt(base, size, static_cast<std::size_t>(animindex), firstAnimOff))
                return EAnimResult::MalformedBuffer;
            anim_abs = static_cast<std::size_t>(animindex) + static_cast<std::size_t>(firstAnimOff);
        }
        if (anim_abs >= size)
            return EAnimResult::MalformedBuffer;

        // Проход по цепочке анимаций (nextoffset).
        // Полная распаковка каналов: RAW (постоянные) и ANIM (сжатые по RLE через
        // mstudioanim_valueptr_t с тремя смещениями на оси X/Y/Z — подтверждена на реальных
        // GMod-моделях v_knife/v_akilo47, см. VERIFICATION.md, Раунд 10).
        std::size_t cur = anim_abs;
        int guard = 0;
        while (cur + static_cast<std::size_t>(L.anim_stride) <= size && guard++ < 512)
        {
            // mstudioanim_t: byte bone, byte flags, short nextoffset (v49/GMod);
            // классика: short bone, byte flags, byte type, short nextoffset.
            std::int16_t bone = 0, next = 0;
            std::uint8_t flags = 0, type = 0;
            if (L.anim_type_off < 0)
            {
                std::uint8_t boneB = 0;
                if (!ReadAt(base, size, cur + static_cast<std::size_t>(L.anim_bone_off), boneB) ||
                    !ReadAt(base, size, cur + static_cast<std::size_t>(L.anim_flags_off), flags) ||
                    !ReadAt(base, size, cur + static_cast<std::size_t>(L.anim_next_off), next))
                    return EAnimResult::MalformedBuffer;
                bone = static_cast<std::int16_t>(boneB); // 0..255
            }
            else
            {
                if (!ReadAt(base, size, cur + static_cast<std::size_t>(L.anim_bone_off), bone) ||
                    !ReadAt(base, size, cur + static_cast<std::size_t>(L.anim_flags_off), flags) ||
                    !ReadAt(base, size, cur + static_cast<std::size_t>(L.anim_type_off), type) ||
                    !ReadAt(base, size, cur + static_cast<std::size_t>(L.anim_next_off), next))
                    return EAnimResult::MalformedBuffer;
            }

            const std::size_t nxt = (next == 0) ? 0 : static_cast<std::size_t>(next);

            const bool rawpos = (flags & 0x01) != 0;
            const bool rawrot = (flags & 0x02) != 0;   // Quaternion48
            const bool rawrot2 = (flags & 0x20) != 0;  // Quaternion64
            const bool animpos = (flags & 0x04) != 0;  // RLE-позиция
            const bool animrot = (flags & 0x08) != 0;  // RLE-поворот
            const bool delta = (flags & 0x10) != 0;    // приращение к базовой позе

            if (bone >= 0 && (numBones <= 0 || bone < numBones))
            {
                ANIM_TRACK tr;
                tr.bone = bone;
                tr.delta = delta;
                const std::size_t data_off = cur + static_cast<std::size_t>(L.anim_stride);
                const int nframes = (seq.numframes > 0) ? seq.numframes : 1;
                tr.frames.reserve(static_cast<std::size_t>(nframes));

                // Смещение к mstudioanim_valueptr_t с учётом впереди лежащих RAW-полей.
                // RAW-поля в этих (GMod/converted) .mdl лежат ПОВОРОТ ПЕРВЫМ, потом позиция
                // (проверено на реальных RAWROT2+RAWPOS: разворот кв. в начале канала всегда
                // даёт |q|=1, см. VERIFICATION.md Раунд 13). Сам же valueptr у СЖАТЫХ каналов
                // идёт ПОЗИЦИЯ ПЕРВОЙ, затем ПОВОРОТ (проверено статистически: rot-first даёт в
                // ~5 раз больше нарушений x²+y²+z²>1 на каналах с обоими типами).
                std::size_t rawSize = 0;
                if (rawrot2)
                    rawSize += 8;
                else if (rawrot)
                    rawSize += 6;
                if (rawpos)
                    rawSize += 6;
                const std::size_t posVP = data_off + rawSize;                      // для animpos (первый)
                const std::size_t rotVP = posVP + ((animpos) ? (6u) : 0u);         // для animrot (второй)

                // Читатель одной оси RLE-потока. Смещение — относительное к началу valueptr.
                auto decodeAxis = [&](std::size_t vpBase, int axis, std::vector<std::int16_t>& out) -> bool
                {
                    std::int16_t off = 0;
                    if (!ReadAt(base, size, vpBase + static_cast<std::size_t>(axis) * 2, off))
                        return false;
                    if (off <= 0)
                    {
                        // ось без данных -> нейтраль (0). Может встречаться у постоянных каналов.
                        out.assign(static_cast<std::size_t>(nframes), 0);
                        return true;
                    }
                    const std::size_t sOff = vpBase + static_cast<std::size_t>(off);
                    if (sOff >= size)
                        return false;
                    return DecodeRLEShorts(base + sOff, size - sOff, nframes, out);
                };

                // Предвычисляем оси поворота/позиции (RAW — постоянны, ANIM — по кадрам).
                AnimQ::Quat4 constRot{}; bool hasConstRot = false;
                AnimQ::Vec3 constPos{};  bool hasConstPos = false;
                std::vector<std::int16_t> qx, qy, qz, px, py, pz;

                if (rawrot2)
                {
                    if (InRange(data_off, 8, size))
                        hasConstRot = DecodeQuaternion64(base + data_off, constRot);
                }
                else if (rawrot)
                {
                    if (InRange(data_off, 6, size))
                    {
                        std::int16_t q[3]; std::memcpy(q, base + data_off, 6);
                        hasConstRot = DecodeQuaternion48(q, constRot);
                    }
                }
                else if (animrot)
                {
                    decodeAxis(rotVP, 0, qx); decodeAxis(rotVP, 1, qy); decodeAxis(rotVP, 2, qz);
                }

                if (rawpos)
                {
                    const std::size_t poff = data_off + (rawrot2 ? 8 : (rawrot ? 6 : 0));
                    if (InRange(poff, 6, size))
                    {
                        std::int16_t p[3]; std::memcpy(p, base + poff, 6);
                        constPos = DecodeVector48(p); hasConstPos = true;
                    }
                }
                else if (animpos)
                {
                    decodeAxis(posVP, 0, px); decodeAxis(posVP, 1, py); decodeAxis(posVP, 2, pz);
                }

                // Собираем кадры.
                for (int f = 0; f < nframes; ++f)
                {
                    ANIM_FRAME fr{};
                    fr.rot = {0.f, 0.f, 0.f, 1.f}; // identity по умолчанию (если канал поворота отсутствует)
                    if (hasConstRot)
                        fr.rot = constRot;
                    else if (!qx.empty() && !qy.empty() && !qz.empty())
                    {
                        std::int16_t q[3] = {qx[f], qy[f], qz[f]};
                        DecodeQuaternion48(q, fr.rot); // из RLE-осей (масштаб 1/32768)
                    }
                    if (hasConstPos)
                        fr.pos = constPos;
                    else if (!px.empty() && !py.empty() && !pz.empty())
                    {
                        std::int16_t p[3] = {px[f], py[f], pz[f]};
                        fr.pos = DecodeVector48(p);
                    }
                    tr.frames.push_back(fr);
                }
                seq.tracks.push_back(std::move(tr));
            }

            if (nxt == 0)
                break; // конец цепочки: записали текущий канал и выходим
            cur = cur + nxt;
        }

        outSeqs.push_back(std::move(seq));
    }

    if (outSeqs.empty())
        return EAnimResult::NoSequences;
    return EAnimResult::Ok;
}
} // namespace SourceMdl
