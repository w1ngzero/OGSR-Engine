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
            std::int32_t numlocalanim = 0, localanimindex = 0;
            if (!ReadAt(base, size, static_cast<std::size_t>(L.numlocalanim_off), numlocalanim) ||
                !ReadAt(base, size, static_cast<std::size_t>(L.localanimindex_off), localanimindex))
                return EAnimResult::MalformedBuffer;
            if (s < numlocalanim)
            {
                const std::size_t adb =
                    static_cast<std::size_t>(localanimindex) + static_cast<std::size_t>(s) * 92;
                // mstudioanimdesc_t: fps@8, flags@12, numframes@16, animindex@56.
                std::memcpy(&fps, base + adb + 8, 4);
                if (!ReadAt(base, size, adb + 16, numframes) ||
                    !ReadAt(base, size, adb + 56, animindex))
                    return EAnimResult::MalformedBuffer;
            }
        }

        seq.numframes = numframes;
        seq.fps = (fps > 0.001f) ? fps : 30.f;
        if (seq.numframes <= 0)
            continue; // пропускаем пустые последовательности

        // Для v49 (numframes_off == -1) каналы идут сразу от базы-анимации:
        //   animindex (из animdesc) = смещение от НАЧАЛА animdesc к mstudioanim_t.
        // Для классики: после animindex лежит массив short-смещений (по группе бленда),
        // первое short и есть смещение к первому mstudioanim_t.
        std::size_t anim_abs = 0;
        if (L.seq_numframes_off < 0)
        {
            // animindex (читается в ветке v49 выше) — смещение от базы animdesc.
            // Чтобы получить базу animdesc, пересчитаем: localanimindex + s*92.
            std::int32_t numlocalanim = 0, localanimindex = 0;
            if (!ReadAt(base, size, static_cast<std::size_t>(L.numlocalanim_off), numlocalanim) ||
                !ReadAt(base, size, static_cast<std::size_t>(L.localanimindex_off), localanimindex))
                return EAnimResult::MalformedBuffer;
            if (s >= numlocalanim)
                return EAnimResult::MalformedBuffer;
            const std::size_t adb =
                static_cast<std::size_t>(localanimindex) + static_cast<std::size_t>(s) * 92;
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
        std::size_t cur = anim_abs;
        int guard = 0;
        while (cur + static_cast<std::size_t>(L.anim_stride) <= size && guard++ < 512)
        {
            // mstudioanim_t: byte bone, byte flags, short nextoffset (v49);
            // классика: short bone, byte flags, byte type, short nextoffset.
            std::int16_t bone = 0, next = 0;
            std::uint8_t flags = 0, type = 0;
            if (L.anim_type_off < 0)
            {
                // v49: bone и flags — одиночные байты (не short!). Читаем bone как байт,
                // чтобы не захватить соседнее поле flags.
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

            // Формат канала (STUDIO_ANIM_RAWPOS/RAWROT/ANIMPOS/ANIMROT/DELTA/RAWROT2).
            // Для верификации важно, что канал реально существует у этой кости; полная
            // распаковка сжатых каналов (mstudioanimvalue RLE) — предмет следующего шага.
            const bool rawpos = (flags & 0x01) != 0;
            const bool rawrot = (flags & 0x02) != 0;
            const bool rawrot2 = (flags & 0x20) != 0; // Quaternion64
            // animpos/animrot (сжатый RLE-канал через mstudioanimvalue) — не поддержан здесь;
            // для decode-а таких каналов нужен отдельный читатель (следующий шаг).

            if (bone >= 0 && (numBones <= 0 || bone < numBones))
            {
                ANIM_TRACK tr;
                tr.bone = bone;
                const std::size_t data_off = cur + static_cast<std::size_t>(L.anim_stride);

                // RAW-каналы — НЕВАРЬИРУЕМЫЙ (постоянный) поворот/сдвиг: одно значение
                // на все кадры последовательности. Распаковываем его и копируем на каждый кадр.
                ANIM_FRAME fr{};
                bool hasRot = false;
                if (rawrot)
                {
                    // Quaternion48 (3 x int16).
                    if (InRange(data_off, 6, size))
                    {
                        std::int16_t q[3];
                        std::memcpy(q, base + data_off, 6);
                        hasRot = DecodeQuaternion48(q, fr.rot);
                    }
                }
                else if (rawrot2)
                {
                    // Quaternion64 (8 байт).
                    if (InRange(data_off, 8, size))
                        hasRot = DecodeQuaternion64(base + data_off, fr.rot);
                }
                if (rawpos)
                {
                    // Vector48 (6 байт) после поворота (если он есть).
                    const std::size_t qsize = rawrot ? 6 : (rawrot2 ? 8 : 0);
                    const std::size_t poff = data_off + qsize;
                    if (InRange(poff, 6, size))
                    {
                        std::int16_t p[3];
                        std::memcpy(p, base + poff, 6);
                        fr.pos = DecodeVector48(p);
                    }
                }

                // Заполняем все кадры последовательности (для RAW — одинаковые).
                const int nframes = (seq.numframes > 0) ? seq.numframes : 1;
                tr.frames.reserve(static_cast<std::size_t>(nframes));
                for (int f = 0; f < nframes; ++f)
                    tr.frames.push_back(fr);
                seq.tracks.push_back(std::move(tr));
                (void)hasRot;
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
