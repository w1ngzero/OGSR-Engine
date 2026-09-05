#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_mesh_ogf.h -- сериализация импортированного Source-меша в OGF-контейнеры геометрии.
//
// Движок создаёт видимый меш через стандартный загрузчик визуала (Fvisual::Load), который читает
// байтовые контейнеры OGF_VERTICES (скин-вершины vertBoned4W) и OGF_INDICES (u16-индексы).
// Чтобы импортировать Source-геометрию как нативный визуал, мы собираем именно ЭТИ байты и
// отдаём их движку (в CKinematics — через синтетический IReader/RImplementation.model_CreateChild).
//
// THIS MODULE IS PURE (без движка/без DX), чтобы byte-формат можно было проверить юнит-тестом,
// воспроизводя логику FVisual::Load (см. source_mdl_mesh_ogf.cpp-комментарии). В реальном
// движковом коде те же байты уходят в model_CreateChild.
//
// Входная вершина — чистая копия полей vertBoned4W (по одной на колонку), чтобы не тянуть
// движковые типы: x,y,z pos; nx,ny,nz normal; tx..tz tangent; bx..bz bitangent;
// u,v uv; m[4] индексы костей; w[4] веса.
//--------------------------------------------------------------------------------------------------
#include <cstdint>
#include <vector>

namespace SourceMdl
{
struct MeshOGFVertex
{
    float x, y, z;     // P
    float nx, ny, nz;  // N
    float tx, ty, tz;  // T
    float bx, by, bz;  // B
    float u, v;        // uv
    std::uint16_t m[4]; // индексы костей (4 влияния)
    float w[4];         // веса
};

// Собирает OGF-поток из одного скин-меша: OGF_VERTICES (vertBoned4W) + OGF_INDICES (u16).
// ВХОДЯЩИЕ данные должны уже быть в X-Ray-конвенции (basis применён, веса нормализованы,
// кости переиндексированы) — именно такой меш даёт TryImportSourceMesh().
//   fvf         -- FVF-код формата вершин (для vertBoned4W это OGF_VERTEXFORMAT_FVF_4L).
//   outBuffer   -- заполняется байтами OGF-потока (по 4-байтовым контейнерам).
// Возвращает false, если вершин/индексов нет.
bool BuildSourceMeshOGF(const std::vector<MeshOGFVertex>& verts,
                        const std::vector<std::uint16_t>& indices,
                        std::uint32_t fvf, std::vector<std::uint8_t>& outBuffer);

// Константы тех же контейнеров, что в xr_3da/fmesh.h (дублируем, чтобы модуль был чистым).
enum { kOGF_VERTICES = 3, kOGF_INDICES = 4 };
} // namespace SourceMdl
