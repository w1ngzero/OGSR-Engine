#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_anim.h — ЧИТАТЕЛЬ анимаций Source .MDL (чистый, без движка, тестируемый).
//
// Разбирает данные последовательностей (sequences) и по-костных анимационных каналов.
// Представление результата — в "чистой" форме (координаты Source, локальные трансформации),
// чтобы преобразование в X-Ray (квантование + базис) лежало в отдельном модуле.
//
// ВАЖНО О СМЕЩЕНИЯХ И ВЕРСИЯХ (честно):
//   Смещения mstudioseqdesc_t и, особенно, формат по-костных mstudioanim_t сильно зависят от
//   версии и от того, считан ли файл через отдельный вершино-файл (split) и т.п. Все ключевые
//   смещения собраны в таблице ANIM_LAYOUT (см. ниже), которая настраивается в одном месте.
//   Значения по умолчанию соответствуют классической раскладке HL2-подобных .mdl и, вместе с
//   типом по-костного канала (RAW против COMPRESSED), ПОДЛЕЖАТ ВЕРИФИКАЦИИ на реальном .mdl
//   (см. README, Раунд 4). Ошибки/неподдерживаемые случаи возвращаются явным enum'ом.
//
//   Поддерживаемый сейчас по-костный формат: STUDIO_ANIM_RAW (простая распаковка по кадрам).
//   Сжатые каналы (STUDIO_ANIM_COMPRESSED / события / IK) возвращают EAnimResult::Unsupported.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_anim_q.h"
#include <cstdint>
#include <string>
#include <vector>

namespace SourceMdl
{
// Представление одного ключевого кадра кости в одной последовательности (координаты Source).
struct ANIM_FRAME
{
    AnimQ::Quat4 rot; // локальный поворот кости
    AnimQ::Vec3 pos;  // локальное смещение кости
};

// По-костный трек: кадры (по одному на кадр последовательности) для конкретной кости.
struct ANIM_TRACK
{
    int bone; // индекс кости (в том же порядке, что у скелета)
    std::vector<ANIM_FRAME> frames;
};

// Последовательность (анимация) целиком.
struct ANIM_SEQ
{
    std::string name;
    int numframes;   // общее число кадров
    float fps;
    bool loop;       // зациклена ли (esmLoop)
    std::vector<ANIM_TRACK> tracks; // по одной на анимируемую кость
};

// Тип по-костного канала (studioanim flags/type).
enum class EAnimBoneFormat
{
    Raw = 0,          // STUDIO_ANIM_RAW: разворачиваемый по кадрам
    Compressed = 1,   // STUDIO_ANIM (сжатый запрос): не поддержан в этом раунде
    Unknown = -1,
};

// Результат чтения.
enum class EAnimResult
{
    Ok = 0,
    NotSourceMdl,
    UnsupportedVersion,
    NoSequences,
    UnsupportedFormat,     // сжатый канал или IK/события
    MalformedBuffer,       // выход за пределы
};

// Раскладка полей последовательности/по-костных анимаций (версионно-зависимая).
struct ANIM_LAYOUT
{
    // studiohdr_t
    int numlocalseq_off;      // смещение int numlocalseq
    int localseqindex_off;    // смещение int localseqindex (байтовое к массиву mstudioseqdesc_t)
    int numlocalanim_off;     // смещение int numlocalanim (нужно для v49: fps/numframes в animdesc)
    int localanimindex_off;   // смещение int localanimindex
    // mstudioseqdesc_t
    int seq_sznameindex_off;  // 4  (szlabelindex — имя последовательности)
    int seq_animindex_off;    // 56 (классика) / 60 (v49: animindexindex — массив short-смещений)
    int seq_numframes_off;    // классика: прямо в seqdesc; v49: НЕ здесь (см. animdesc)
    int seq_fps_off;          // классика: прямо в seqdesc; v49: НЕ здесь (см. animdesc)
    int seq_groupsize_off;    // 64 (классика) / 68 (v49)
    int seq_stride;           // sizeof(mstudioseqdesc_t)
    // mstudioanim_t (по-костный). В v49 — byte bone, byte flags, short nextoffset
    // (без отдельного поля type; тип кодируется в flags), stride = 4.
    int anim_bone_off;       // 0
    int anim_flags_off;      // 2 (классика) / 1 (v49, byte)
    int anim_type_off;       // 3 (классика; v49 не используется)
    int anim_next_off;       // 4 (классика) / 2 (v49, short)
    int anim_stride;         // 16 (классика) / 4 (v49)
    // флаги StudioAnimType
    int animflag_loop;       // бит "цикл" в флагах последовательности
};

inline ANIM_LAYOUT ClassicAnimLayout()
{
    ANIM_LAYOUT l{};
    l.numlocalseq_off = 224;
    l.localseqindex_off = 228;
    l.numlocalanim_off = 180;   // классика HL2: numlocalanim тоже на своём месте
    l.localanimindex_off = 184;
    l.seq_sznameindex_off = 4;
    l.seq_animindex_off = 56;
    l.seq_numframes_off = 152; // классический HL2: numframes поздно в структуре
    l.seq_fps_off = 156;
    l.seq_groupsize_off = 64;
    l.seq_stride = 160;
    l.anim_bone_off = 0;
    l.anim_flags_off = 2;
    l.anim_type_off = 3;
    l.anim_next_off = 4;
    l.anim_stride = 16;
    l.animflag_loop = 0x8000; // esm-флаг цикла в mstudioseqdesc_t::flags (часто: 1<<15)
    return l;
}

// ПОДТВЕРЖДЕННАЯ раскладка для Source 2013 / Garry's Mod (v48+/v49). Верифицирована
// на реальном gfl2_asteria_arms.mdl (см. VERIFICATION.md): seq0='idle', fps=30, numframes=2.
// ВАЖНО: в v49 numframes/fps живут в mstudioanimdesc_t (pLocalAnimdesc[seq]), а НЕ в seqdesc;
// поэтому читатель берёт их из базы-анимации (индекс = индекс последовательности).
inline ANIM_LAYOUT V49AnimLayout()
{
    ANIM_LAYOUT l{};
    l.numlocalseq_off = 188;       // studiohdr v49
    l.localseqindex_off = 192;
    l.numlocalanim_off = 180;
    l.localanimindex_off = 184;
    l.seq_sznameindex_off = 4;     // szlabelindex
    l.seq_animindex_off = 60;      // animindexindex
    l.seq_numframes_off = -1;      // НЕ используется для v49 (сигнал "из animdesc")
    l.seq_fps_off = -1;
    l.seq_groupsize_off = 68;
    l.seq_stride = 212;            // sizeof(mstudioseqdesc_t) для v49
    l.anim_bone_off = 0;           // byte bone
    l.anim_flags_off = 1;          // byte flags
    l.anim_type_off = -1;          // нет отдельного поля
    l.anim_next_off = 2;           // short nextoffset
    l.anim_stride = 4;
    l.animflag_loop = 0x0001;      // STUDIO_LOOP в mstudioseqdesc_t::flags (v49)
    return l;
}

// Читает все последовательности из буфера классического .mdl (без массивов индексов — просто
// последовательности + по-костные RAW-каналы).
EAnimResult ReadSourceAnims(const void* data, std::size_t size, std::vector<ANIM_SEQ>& outSeqs,
                            const ANIM_LAYOUT& layout = ClassicAnimLayout(),
                            const int numBones = 0);

const char* AnimResultName(EAnimResult r);
const char* AnimBoneFormatName(EAnimBoneFormat f);
} // namespace SourceMdl
