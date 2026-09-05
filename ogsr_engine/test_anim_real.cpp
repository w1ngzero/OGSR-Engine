# Схема вшивания импортированного Source-меша в движок (черновик, ещё не реализовано)

Цель: заставить импортированную Source-геометрию (`c_hands`) рендериться как **нативный** детский
визуал иерархического объекта — через стандартный загрузчик `Fvisual::Load`, а не через
самодельные DX-буферы, сшитые мимо конвейера.

## Что уже есть
- `SourceMdl::TryImportSourceMesh(name, out)` → `out.verts` (`std::vector<vertBoned4W>`),
  `out.indices` (`std::vector<u16>`), `out.boneMap` (identity), `out.sphere`, `out.loaded`,
  `out.numTriangles`. Меш уже в X-Ray-базисе (basis применён, веса нормализованы, кости — индексы
  движковых `CBoneData`, т.е. порядок `.mdl`).
- `BuildSourceMeshOGF(verts, indices, fvf, outBuffer)` — сериализует этот меш в байты OGF-чанков
  `OGF_VERTICES` (`u32 fvf; u32 vCount; vertBoned4W[]`) + `OGF_INDICES` (`u32 count; u16[]`),
  упакованных как контейнеры `u32 id; u32 size; payload` (точно как `IWriter::open_chunk` /
  `IReader::find_chunk`). **Проверено на реальном gfl2_asteria_arms: 12487 verts, 16828 tris,
  stride 76, max index 12486 < vCount, fvf=0x5a237f80 (FVF_4L).**

## Точка вшивания
`FHierrarhyVisual::Load` (Layers/xrRender/FHierrarhyVisual.cpp) читает `OGF_CHILDREN` → для каждого
детского `open_chunk(count)` вызывает `RImplementation.model_CreateChild(name_load, O)`, где `O` —
`IReader*` на поток, содержащий `OGF_VERTICES`/`OGF_INDICES`. `Fvisual::Load` читает ровно эти чанки.

Значит, чтобы вставить Source-меш как дочерний визуал, достаточно:

```
RImplementation.model_CreateChild(name, SyntheticReader(ogf_bytes))
```

где `ogf_bytes` = вывод `BuildSourceMeshOGF`. Все остальные побочные эффекты (регистрация визуала,
создание DX-буферов `p_rm_Vertices`/`p_rm_Indices`, bounding-объёмы) делает сам движок.

## Открытые вопросы (решать в след. раундах)
1. **Кто является родителем и откуда берётся его Fmatrix?** Детский визуал рендерится в модельной
   системе родителя. При импорте Source-модели как целого нужен родитель-`CKinematics` с
   `CBoneData[]` из тех же костей. Скелет уже импортируется (`TryImportSourceSkeleton`). Надо
   свести: родитель = импортированный скелет, ребёнок = импортированный меш.
2. **boneMap / boneID flip.** В `.vtx` кости — глобальные индексы Source-скелета; `CBoneData`-ы
   движка создаются в том же порядке (`.mdl`), поэтому `boneMap` identity. Но у Source-скелета
   «корневой» кост обычно индекс 0, а X-Ray-`CKinematics` может иметь другой корень/порядок.
   Нужна on-screen сверка `Fmatrix` костей (пункт 3 общего плана).
3. **fvf.** Для `vertBoned4W` = `OGF_VERTEXFORMAT_FVF_4L`. `Fvisual`/`FVisual` создают DX-буферы
   по `FVF::ComputeVertexSize(fvf)` — 76 байт, совпадает.
4. **Как именно подать байты движку.** Варианты: (а) синтетический `CTempReader` вокруг буфера и
   вызов `model_CreateChild` напрямую; (б) ручное создание `IRender_Mesh` и `p_rm_Vertices`.
   Предпочтителен (а) — максимально полагается на нативный конвейер и проще поддерживать.

## Почему именно так
Правило из контекста: **это должна быть engine-уровневая возможность, а не мод-хак.** Загрузка
через `RImplementation.model_CreateChild` + `Fvisual::Load` есть именно такой путь: меш становится
обычным визуалом, который живёт в общем пуле, участвует в отсечении, сортировке и LOD, лечится
стандартными средствами отладки (`r__geometry`, wireframe). Самодельные DX-буферы этого не дают.
