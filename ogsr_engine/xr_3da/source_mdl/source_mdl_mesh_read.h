#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_mesh_read.h -- разбор классического ВСТРОЕННОГО (inline) формата вершин Source .MDL.
//
// ИСТОРИЧЕСКОЕ ОГРАНИЧЕНИЕ (важно!):
//   Современные модели Garry's Mod / Source v48+ хранят геометрию в ОТДЕЛЬНОМ файле
//   (MODEL_VERTEX_FILE_ID + fixup-таблица). Этот модуль читает только классический INLINE
//   вариант (примерно v44-v47, "SDK 2007/HL2"-подобный), где mstudiovertex_t лежит прямо в .mdl.
//   Split-формат — отдельный хрупкий под-модуль, требующий реального .mdl/.vtx для проверки.
//
//   Смещения вложенных записей зависят от версии, поэтому ВСЕ они собраны в одной таблице
//   CLASSIC_LAYOUT ниже. Если конкретный ассет не читается первым подходом — обычно достаточно
//   поправить одно-два значения в этой таблице (обычно это размер/выравнивание mstudiovertex_t).
//   Значения по умолчанию соответствуют классической раскладке v47 inline.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_mesh.h"
#include <cstdint>

namespace SourceMdl
{
// Оффсеты записей классического inline-формата.
struct CLASSIC_LAYOUT
{
    // studiohdr_t
    int numbodyparts_off;   // смещение int numbodyparts
    int bodypartindex_off;  // смещение int bodypartindex (байтовое смещение к массиву)
    // mstudiobodyparts_t
    int bp_nummodels_off;   // 4
    int bp_modelindex_off;  // 12 (байтовое смещение к массиву моделей)
    // mstudiomodel_t
    int mdl_numvertices_off;  // 64
    int mdl_vertexindex_off;  // 68 (байтовое смещение к массиву вершин)
    int mdl_nummeshes_off;    // 72
    int mdl_meshindex_off;    // 76 (байтовое смещение к массиву мешей)
    // mstudiomesh_t
    int mesh_numtri_off;      // 0
    int mesh_triindex_off;    // 4 (байтовое смещение к массиву индексов)
    int mesh_numvert_off;     // 8
    int mesh_vertoffset_off;  // 12 (счётчик смещения вершин в массиве модели)
    // mstudiovertex_t
    int vert_stride;          // sizeof(mstudiovertex_t)
    int vert_weight_off;      // смещение блока весов (0)
    int vert_bone_off;        // смещение массива индексов костей (+3)
    int vert_numbones_off;    // смещение байта числа влияний (+6)
    int vert_pos_off;         // смещение позиции
    int vert_normal_off;      // смещение нормали
    int vert_uv_off;          // смещение UV (два float)
};

// Классическая раскладка по умолчанию (v47 inline). См. комментарий выше.
inline CLASSIC_LAYOUT ClassicInlineLayout()
{
    return CLASSIC_LAYOUT{
        /*numbodyparts_off*/ 232,
        /*bodypartindex_off*/ 236,
        /*bp_nummodels_off*/ 4,
        /*bp_modelindex_off*/ 12,
        /*mdl_numvertices_off*/ 64,
        /*mdl_vertexindex_off*/ 68,
        /*mdl_nummeshes_off*/ 72,
        /*mdl_meshindex_off*/ 76,
        /*mesh_numtri_off*/ 0,
        /*mesh_triindex_off*/ 4,
        /*mesh_numvert_off*/ 8,
        /*mesh_vertoffset_off*/ 12,
        /*vert_stride*/ 40,
        /*vert_weight_off*/ 0,
        /*vert_bone_off*/ 3,
        /*vert_numbones_off*/ 6,
        /*vert_pos_off*/ 8,
        /*vert_normal_off*/ 20,
        /*vert_uv_off*/ 32};
}

// Тип результата чтения (для понятных сообщений).
enum class EReadMeshResult
{
    Ok = 0,
    NotSourceMdl,         // плохой id
    UnsupportedVersion,   // версия вне ожидаемого
    SplitVertexFile,      // модель использует отдельный файл вершин (не поддерживается в этом раунде)
    MalformedBuffer,      // выход за пределы буфера
    EmptyModel,           // нет вершин
};

// Читает геометрию из буфера классического .mdl в плоский вектор мешей (по одному на модель).
// Модель рассматривается как "скелет + меш": здесь возвращаются только вершины/треугольники,
// без скелета (скелет берётся из source_mdl_skeleton). Если не требуется считывать тела —
// numbodyparts читается по той же таблице.
EReadMeshResult ReadClassicMesh(const void* data, std::size_t size, std::vector<MESH>& outMeshes,
                                const CLASSIC_LAYOUT& layout = ClassicInlineLayout());
const char* ReadMeshResultName(EReadMeshResult r);
} // namespace SourceMdl
