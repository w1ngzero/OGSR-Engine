# Сверка импортёра Source на реальной модели (Раунд 5)

Модель: **`gfl2_asteria_arms`** (руки из Garry's Mod, `c_hands`), версия **49** (Source 2013 / GMod).
Файлы: `.mdl` (23 КБ), `.vvd` (799 КБ), `.dx90.vtx` (214 КБ). Скачаны с Google Drive через
`tools/gdown.py`.

## Что подтвердилось на реальных байтах

### 1. Идентификатор файла (id) — БЫЛ НЕВЕРНЫЙ, исправлено
- Реальное значение: байты `49 44 53 54` = `'I','D','S','T'`, как little-endian `u32` = **`0x54534449`**.
- В ридерах (скелет/меш/анимации) было зашито `0x53544F4F` — ошибка.
  Тест «проходил» лишь потому, что тест-билдер сам писал то же неверное значение.
- **Исправлено во всех трёх ридерах** + тест-билдеры обновлены.

### 2. Размер записи кости `mstudiobone_t` = **216** (v49), а не 152
- Чтение по stride 152 даёт мусор со второй кости; скелет читается корректно **только по stride 216**.
- Оффсеты внутри записи при этом правильные: `pos@32`, `quat@44` (кватернион нормализован: |q|=1.000).
- **Исправлено**: `BoneStrideByVersion(version)` → `>=48: 216`, `44-47: 152`.
- «сверка» проходится: 49 костей `valvebiped.bip01_*` + `cup_l/r` + `zelbowpart*` + `zarmtwist*`,
  корень `bip01_spine4` (index 0, parent -1), **все родители валидны**.

### 3. Формат вершин — split (v49), не inline. Подтверждено, геометрия достижима
- В `.mdl` вершин НЕТ (это split-формат). `ReadClassicMesh` корректно возвращает `SplitVertexFile`.
- Проверен `.vvd`: `id=0x56534449`, `version=4`, `numLODs=1`, `numLODVertexes[0]=12487`,
  `vertexDataStart=64`, `tangentDataStart=599440`, stride вершины (при 3f+3f+4f+2f=48 байт) даёт
  конец вершин == `tangentDataStart` (**зазор 0**) — т.е. массив вершин структурно читается
  без разрывов на правильном количестве (12487).
- **РАЗОБРАНО :: раскладка `.vvd`-вершины (нестандартная!)**. Канонический `mstudiovertex_t`
  (pos|normal|...) здесь НЕ совпал. Реальная строка (48 байт, stride=48):
  | байты | поле | подтверждение |
  |---|---|---|
  | 0..15 | `const float[4]` (обычно (1,0,0,0)) | не геометрия — константа |
  | 16..27 | **позиция** | range (52.5, 9.9, 24.9) = x ±26.2..26.3, y −5.9..4.0, z −24.3..0.6 — совпадает с bbox модели (~10.6×54.3×28.1); x-размах ±26 = две руки врозь |
  | 28..39 | **нормаль** | **идеально единичная: |N| = 1.0000, 0 из 12487 не-единичных** |
  | 40..47 | **uv** | 0.04..0.998 по u, 0.006..0.986 по v |
  Позиция найдена брутфорсом по «полному трёхосному размаху, согласующемуся с bbox модели» —
  поля f0-3 дают размах (0.59,0.5,0.26) (слишком мало) и f0-3 => нормальный флаг, а f4 (байт 16) —
  единственное с полным (52,10,25) размахом.
- **`.vvd`-ридер исправлен**: теперь читает pos@16, normal@28, uv@40 (через `VVD_LAYOUT`),
  и на реальном файле выдаёт корректную геометрию: `vert[0] pos=(11.13,−1.45,−6.97), normal=(−0.79,−0.15,−0.60), uv=(0.362,0.199)` — правдоподобная точка пальца.
- Индексы (порядок треугольников) и веса костей лежат в `.vtx` (не в `.mdl`), где задана
  оптимизация `dx90`. `.vtx` — отдельный большой формат (id=7, checksum=−397744373 совпадает с
  `.mdl`/`.vvd` — файлы связаны). Это следующий шаг.

### 3b. `.vtx` — ИНДЕКСЫ + ВЕСА КОСТЕЙ ПОДТВЕРЖДЕНЫ (современный формат, v7)
Иерархия (все вложенные смещения **относительны начала их род. заголовка**):
`FileHeader_t` → `BodyPartHeader_t[]` → `ModelHeader_t[]` → `ModelLODHeader_t[]` →
`MeshHeader_t[]` → `StripGroupHeader_t[]` → `StripHeader_t[]` + `Vertex_t[]` + `uint16[]`.

Подтверждённые значения (сняты с реального файла):
- `FileHeader_t`: version=7, vertCacheSize=24, maxBonesPerStrip=53, maxBonesPerTri=9,
  maxBonesPerVert=3, checkSum=−397744373 (== `.mdl`/`.vvd`), numLODs=1,
  materialReplacementListOffset=214158 (== filesize−8), numBodyParts=1, bodyPartOffset=36.
- Иерархия: BodyPart@36 (numModels=1, modelOffset=8) → Model@44 (numLODs=1, lodOffset=8) →
  ModelLOD@52 (**numMeshes=3**, meshOffset=12) → MeshHeader@64,74,82.
- **MeshHeader_t = 9 байт** (`int,int,u8`, НЕ выровнен — без padding). При stride 8/16 меши 2/3
  рассыпаются в мусор; только 9 даёт все три валидными.
- Три меша читаются как непрерывный поток вершин: `vstart = 295 → 10861 → 60667`,
  сумма вершин **1174+5534+5779 = 12487 == количество в `.vvd`** → подтверждено соответствие
  `vtx`–вершина `↔` `vvd`-вершина 1:1.
- Каждый меш: 1 strip-группа (`StripGroupHeader_t` = 28 байт), 1 strip, `flags=2`
  (= `STRIP_GROUP_IS_HWSKINNED` → **`boneID` — авторитетный** источник кости).
- Индексы: 6192/21618/22674 → **0 выходящих за диапазон** (max < numVerts), ровно
  2064/7206/7558 треугольников.
- `Vertex_t` (9 байт): `boneWeightIndex[3]` (0..2), `numBones` (1..3: гистограмма {1:3594,2:5783,3:3110}),
  `origMeshVertID` (0..586), `boneID[3]` — все в диапазоне 0..37 (валидные индексы костей, < 48).
- Семантика веса: источник хранит только **индексы** костей, доля = `1/numBones`
  (равномерный вес; стандарт для Source hw-skinned). Выносится в адаптер.
- `ReadVtxMeshes()` реализована в `source_mdl_vtx.{h,cpp}` (читает LOD0), прочитано на реальном
  файле: `ok (3 meshes)`, `mesh0: 1174 verts, 2064 tris`, `totals: 12487 verts, 16828 tris,
  out-of-range indices: 0`, `index range: OK`.

### 3c. СБОРКА СКИН-МЕША из `.vvd`+`.vtx` — подтверждена (Source-конвенция)
- Связка `vtx <-> vvd` через `origMeshVertID`: каждый меш `.vtx` ссылается на СВОЙ диапазон
  `.vvd` (`origMeshVertID` ∈ 0..numverts-1 внутри меша), а `.vvd`-индекс = `meshBase + origMeshVertID`.
- **`meshBase` = кумулятивная сумма `numverts` предшествующих мешей = {0, 1174, 6708}**.
  Это точно совпадает с `mstudiomesh_t::vertexindex` из `.mdl` (0/1174/6708) и с числом вершин
  `.vvd` (12487). Проверено: `origMeshVertID` мешей = 0..1173 / 0..5533 / 0..5778.
- Модель в `.mdl` найдена @14668 (`name='asteria_arms_reference.smd'`, `nummeshes=3`,
  `numvertices=12487`, `vertexindex=0`, `meshindex=148` → mesh array @14816). Меш-записи:
  `numvertices` = 1174/5534/5779, `vertexindex` = 0/1174/6708.
- **`source_mdl_split_mesh.{h,cpp}`** — новый чистый комбайнер: `.vvd`(позиция/нормаль/uv) +
  `.vtx`(веса/кости/треугольники) → `MESH` (Source-конвенция, кости — глобальные индексы
  скелета через `boneID`, доля веса = `1/numBones`). Затем тот же `BuildXRayMesh()` собирает
  `vertBoned4W` (+basis+boneMap).
- Прочитано на реальном файле: `split-mesh build: OK (3 meshes)`, `totals: 12487 verts,
  16828 tris`, `mesh0.vert0 pos=(11.13,-1.45,-6.97) bone0=0 w0=1.00`, `max global boneID = 37`
  (< 49 костей — все валидны).
- **Базис Source→X-Ray (det==+1) к собранному мешу применён**: в X-Ray-системе (Z-вверх) bbox =
  `x[-26.2..26.3]` (влево/вправо, самая широкая ось — две руки), `y[-0.6..24.3]` (вперёд),
  `z[-5.9..4.0]` (вверх/вниз) — физически корректный охват рук. Нормали остаются единичными
  (`normals unit: OK`) — реверс winding/нормалей НЕ нужен (det==+1).

### 4. Анимации — раскладка v49 РАЗОБРАНА и подтверждена (байт-в-байт)
- **`studiohdr_t` v49**: `numlocalanim@180, localanimindex@184, numlocalseq@188, localseqindex@192`
  (не 224/228, как в классике HL2 — классические оффсеты давали мусор).
- `numlocalanim=1, localanimindex=14116; numlocalseq=1, localseqindex=14240`.
- **КЛЮЧЕВОЕ открытие**: в v49 `numframes`/`fps` лежат в **базовой анимации** `mstudioanimdesc_t`
  (индекс-связанной с последовательностью), а НЕ в `mstudioseqdesc_t`. Поэтому классический
  `seq_numframes_off=152` возвращал мусор.
- `mstudioseqdesc_t` (v49, stride=**212**): `szlabelindex@4` (имя), `flags@12`, `bbmin@32/bbmax@44`,
  `numblends@56`, `animindexindex@60`, `groupsize@68/72`, `fadeintime@104=0.2`. Прочтено:
  **seq0 `name='idle'`, `numblends=1`, `groupsize=(1,1)`**.
- `mstudioanimdesc_t` (v49): `fps@8=30`, `numframes@16=2`, `animindex@56=108` (смещение от базы
  animdesc → каналы на 14116+108=14224). Прочтено: **anim0 `name='@idle'`, fps=30, numframes=2**.
- `mstudioanim_t` (v49): **byte bone, byte flags, short nextoffset** (stride=4, БЕЗ отдельного пола
  type; формат кодируется в flags). Канал bone0 `flags=0x20` = **STUDIO_ANIM_RAWROT2 (Quaternion64)**,
  `nextoffset=0` (последний канал).
- **`source_mdl_anim.cpp` обновлён**: добавлена раскладка `V49AnimLayout()` (v49-оффсеты + пометки
  `seq_numframes_off=-1` → «брать fps/numframes из базы-анимации», `anim_type_off=-1` → «bone/flags —
  байты»), и ветка чтения таких полей. Прочитано на реальном файле: `anim reader (v49 layout): ok
  (1 seqs), seq 'idle': numframes=2 fps=30 tracks=1 bone0:1f`.
- Для RAWPOS/RAWROT (кватернион48) поворот/позиция извлекаются прямо; RAWROT2 (Quaternion64)
  фиксируется как канал (декомпрессия полного Quaternion64 + сжатых ANIMPOS/ANIMROT (RLE) —
  следующий шаг).

## Что сделано и что исправлено в коде (по результатам сверки)

| Файл | Изменение |
|---|---|
| `source_mdl_skeleton.{h,cpp}` | id → `0x54534449`; stride кости → по версии (`>=48:216`); сохранены `pos@32/quat@44` |
| `source_mdl_mesh_read.cpp` | id → `0x54534449`; для `version>=48` → `SplitVertexFile` (честная диагностика) |
| `source_mdl_anim.cpp` | id → `0x54534449` |
| `test_source_mdl.cpp` | синт-билдеры пишут верный id; все юнит-тесты проходят |
| `test_real_mdl.cpp` | **новый**: скармливает ридеру реальный `.mdl` и выводит скелет |

## Как воспроизвести сверку

```bash
# Скачать и распаковать
python3 tools/gdown.py "<ссылка>" --out hands.zip
python3 -c "import py7zr; py7zr.SevenZipFile('hands.zip').extractall('hands')"

# Разобрать реальные байты (скелет/геометрия/)
python3 tools/verify_mdl.py hands/gfl2_asteria_arms.mdl \
    --vvd hands/gfl2_asteria_arms.vvd --vtx hands/gfl2_asteria_arms.dx90.vtx

# Проверить ридер на реальном файле (C++)
cd ogsr_engine/xr_3da/source_mdl
g++ -std=c++17 -O2 -o test_real test_real_mdl.cpp \
    source_mdl_skeleton.cpp source_mdl_mesh.cpp source_mdl_mesh_read.cpp \
    source_mdl_vvd.cpp source_mdl_vtx.cpp source_mdl_split_mesh.cpp source_mdl_anim.cpp
./test_real /home/user/hands/gfl2_asteria_arms.mdl \
            /home/user/hands/gfl2_asteria_arms.vvd \
            /home/user/hands/gfl2_asteria_arms.dx90.vtx
# Ожидаем: "skeleton parse: OK", 49 костей, hierarchy validity: OK,
#          mesh (inline): split-format; vvd reader: ok (12487 verts);
#          vtx reader: ok (3 meshes), totals: 12487 verts, index range: OK;
#          split-mesh build: OK (3 meshes), max global boneID = 37 (valid),
#          basis(XRay, Z-up) bbox: x[-26..26] y[-0.6..24] z[-6..4], normals unit: OK;
#          anim reader (v49 layout): ok (1 seqs), seq 'idle': numframes=2 fps=30 tracks=1 bone0:1f
```

## Открытые пункты (следующие шаги)

1. **`.vvd` + `.vtx` + сборка меша — ПОДТВЕРЖДЕНЫ** (см. §3, §3b, §3c). Скин-меш собран в
   Source-конвенции, базис применён, кости валидны. Осталось в движке:
   - вызвать `BuildXRayMesh()` на собранном `MESH` (basis + boneMap + `vertBoned4W`) и
     **развернуть индексы костей** (порядок костей Source → порядок вектора OGSR);
   - подключить результат в `CKinematics`/визуал, чтобы руки реально отрисовались.
2. **Раскладка анимаций v49 — РАЗОБРАНА И ПОДТВЕРЖДЕНА** (см. §4): seqdesc/animdesc/anim-канал
   v49 считаны с реального файла (`idle`, fps=30, numframes=2, канал RAWROT2). Осталось:
   - декомпрессия **RAWROT2 (Quaternion64)** и сжатых каналов ANIMPOS/ANIMROT (RLE через
     `mstudioanimvalue`) — сейчас канал фиксируется, полный поворот/сдвиг в ключевые кадры
     не разворачивается;
   - дальнейший перенос кадров в X-Ray `CMotion` (квантование + базис) для по-костного трека.
3. **Проверка базиса на экране (движок)** — после подключения скин-геометрии сделать живой
   on-screen `Fmatrix`-bases check (визуально/логами подтвердить, что руки встали правильно).

## Round 5c — OGF-сериализация меша (BuildSourceMeshOGF) ✓

Результат `./test_ogf <mdl> <vvd> <vtx>` на реальном `gfl2_asteria_arms`:

```
ogf verts=12487 indices=50484 tris=16828
ogf buffer = 1050008 bytes
verts chunk: fvf=0x5a237f80 vCount=12487 payload=949020
  vert[0].m[0]=0 m[1]=0 (b0=0)  stride=76
idx chunk: count=50484 payload=100972 (expect 100972)
  max index=12486 (vCount=12487) => OK
ALL OGF CHECKS PASSED
```

**Подтверждено:**
- Размер vertBoned4W == 76 байт: payload = 8 + 12487*76 = 949020 (точно).
- Индексы покрывают диапазон 0..12486 (max < vCount) — вся геометрия адресуется.
- Чанки разворачиваются точно как в IReader::find_chunk (u32 id, u32 size, payload) — реализация
  `BuildSourceMeshOGF` в `source_mdl_mesh_ogf.cpp` воспроизводит IWriter::open_chunk/close_chunk.
- fvf = OGF_VERTEXFORMAT_FVF_4L = 5*0x12071980 (=0x5a237f80) — именно то, что читает
  Fvisual::Load для vertBoned4W.
- ID контейнеров: OGF_VERTICES=3, OGF_INDICES=4 (совпадает с xr_3da/fmesh.h).

**СЛЕДУЮЩИЙ ШАГ:** подать результат `TryImportSourceMesh()` через `BuildSourceMeshOGF` в
`RImplementation.model_CreateChild` в `FHierrarhyVisual.cpp` (OGF_VERTICES/OGF_INDICES через
синтетический IReader) — создание дочернего `Fvisual`/`Fvisual` и его Load.

## Round 5d — распаковка RAW-каналов анимации (Quaternion64/48, Vector48) ✓

Проверено на реальном `gfl2_asteria_arms.mdl` (канал bone0 RAWROT2 @14224, flags=0x20):

- **`source_mdl_anim_decode.{h,cpp}`** — чистый распаковщик:
  - `DecodeQuaternion64(raw8)` (RAWROT2): 3×21-бит (x,y,z) со смещением 2^20, масштаб 1/1048576.5,
    w = sqrt(1-x²-y²-z²), знак w — старший бит (wneg). 
  - `DecodeQuaternion48(raw3)` (RAWROT): 3×int16, w=sqrt(1-x²-y²-z²).
  - `DecodeVector48(raw3)` (RAWPOS): 3×int16 / 32768.
- **`source_mdl_anim.cpp`** — читатель теперь распаковывает RAW-каналы в РЕАЛЬНЫЕ кадры
  (заполняет все `numframes` кадров последовательности одинаковым постоянным значением);
  раньше он просто копировал сырые байты в float (неверно).

Результат `./test_anim_real` на реальном файле:
```
decoded q=(-0.54267,-0.45334,-0.45334,0.54267) |q|=1.00000   ← единичный кватернион
reader frames for bone0 = 2 (seq.numframes=2)
  frame 0: q=... diff=0.000000
  frame 1: q=... diff=0.000000
ALL ANIM DECODE CHECKS PASSED
```
`test_anim_decode_unit` (known-answer, без ассета): все случаи Quaternion64/48/Vector48 ок.
`test_real` теперь выводит `seq 'idle': ... bone0:2f` (два кадра вместо одного).

ОТКРЫТО (честно): точный ПОРЯДОК 21-битных полей внутри 64-битного слова (x/y/z на битах
0/21/42, wneg на 63) — принят как каноническая упаковка (совпадает с CAFU-дешифратором HL2,
даёт единичный кватернион на реальных байтах). Бит-порядок НЕ независимо проверен (нет
эталонного кватерниона для этого канала) — будет подтверждён on-screen при проверке базиса
после подключения скелета/анимации в движке.

## Round 5e — RLE-декомпрессор mstudioanimvalue_t (ANIMPOS/ANIMROT) ✓

Распакован алгоритм по авторской реализации (SourceIO, проверенный конвертер Source):
```
_decRLE(valid,total) для каждого сегмента:
  valid            = число НОВЫХ int16 значений подряд
  total - valid    = число следующих кадров, ПОВТОРЯЮЩИХ последнее значение
  конец: frameOffset >= frameCount
```

- **`source_mdl_anim_decode.{h,cpp}`** добавлены `DecodeRLEShorts()` и `DecodeAnimValues()`
  (функция от `rle, rleSize, frameCount, scale`). ЧИСТЫЙ модуль.
- `test_rle_unit.cpp` (known-answer, без ассета): все случаи ок — сегменты `[100,-100,20,20,7,8]`,
  константа `42 x6`, scaled `0.5 -> 21.0`, мальформация `-> false`.
  `ALL RLE UNIT TESTS PASSED`.

ЧЕСТНО: это примитив (ядро RLE-декодирования), а не полная интеграция сжатых каналов в читатель.
Полный путь ANIMROT/ANIMPOS требует чтения valueptr (3 int16 offset'а в `mstudioanim_valueptr_t`),
добавления `base_rot/base_pos` (поза покоя кости) и конверсии эйлеровых углов в кватернион
(`euler_to_quat`). Это НЕ выполнено и НЕ верифицировано, т.к. в `c_hands` сжатых каналов нет
(только постоянный RAWROT2) — для верификации нужен ассет с реальными RLE-каналами.
Отмечено как открытый пункт.

## Round 5f — вшивание меша в движок (OGF-поток для CSkeletonX_ST) ✓ (данные), ⚠ (код не скомпилирован)

**Исправлено/добавлено (движок):**
- `source_mdl_import.h`: в `SourceMeshImport` добавлено поле `int root` (раньше `.cpp` уже писал
  `out.root`, а поля в структуре не было — компилятор бы упал). Добавлены
  `psSourceMeshMode` (консольный переключатель) и декларация
  `BuildSourceMeshOGFStream(imp, texture, shader, outBytes)`.
- `source_mdl_import.cpp`: реализован `BuildSourceMeshOGFStream()` — собирает полный OGF-поток
  `CSkeletonX_ST`: `OGF_HEADER` (type=MT_SKELETON_GEOMDEF_ST=5, version=4, shader_id=0) +
  `OGF_TEXTURE` + `OGF_VERTICES` (fvf=OGF_VERTEXFORMAT_FVF_4L, vCount, vertBoned4W[root]) +
  `OGF_INDICES` (iCount, u16[]).
- `SkeletonCustom.{h,cpp}`: `CKinematics::LoadSourceMeshGeometry(N)` — строит поток, оборачивает
  в `CTempReader`, создаёт дочерний `CSkeletonX_ST` через `RImplementation.model_CreateChild`
  (уникальное имя `<model>_smesh`), заменяет им `children`. Вызывается в ветке Source-скелета
  после `LoadSourceSkeleton` при `psSourceMeshMode`.
- `xrRender_console.cpp`: команда `rs_source_mesh` (0/1).

**ВАЛИДАЦИЯ данных (офлайн) — подтверждено:**
`test_ogf_stream` (локальное зеркало vertBoned4W/ogf_header + тот же байтовый поток) — читается
ровно как `CSkeletonX_ST::Load`/`dxRender_Visual::Load`:
```
ogf stream = 1050097 bytes
  [ok] OGF_HEADER size == 44; format_version==4; type==MT_SKELETON_GEOMDEF_ST; shader_id==0
  [ok] OGF_VERTICES: fvf==FVF_4L; vCount==12487; vert payload==8+12487*76 (949020, точно)
  [ok] OGF_INDICES: iCount==50484; idx payload==4+50484*2 (100972, точно); idx[0]==0
ALL OGF STREAM CHECKS PASSED
```

**ЧЕСТНО (важно):**
- Движковый код (`.cpp` в Layers/xrRender и `source_mdl_import:*`) я **НЕ компилировал** — здесь
  нет DX9/MSVC-сборки. Правки сделаны аккуратно по существующим паттернам, но требуют сборки.
- **Материал/шейдер**: Source-меш не имеет материала → OGF_TEXTURE пишет заглушку
  (`texture="models\hands\c_hands"`, `shader="default"`). Это, вероятно, НЕ валидный set модели
  для OGSR — mesh может не отрисоваться или упасть на шейдере. Нужно подставить корректный
  именованный set (см. `GetCachedModelShader`).
- **Проверка базиса на экране** — по-прежнему требует запущенного движка.
- `test_anim_real` и юнит-набор не затронуты: `ALL ... PASSED`.

## ОТКРЫТО (следующие шаги) — обновлённый порядок
1. **Сборка OGSR на Windows** (DX9+MSVC) + правка шейдера/материала на валидный set модели.
2. **Загрузка модели с `rs_source_skeleton 1` и `rs_source_mesh 1`** → первый in-game снимок.
3. **On-screen `Fmatrix`-проверка базиса** (рифт/отражение/транспонирование) — итерация по логу/кадру.
4. Полный RLE-путь ANIMROT/ANIMPOS (нужен ассет с RLE-каналами) — примитив готов, интеграции нет.
5. Анимация в `CMotion` (позинг) — после статичного показа.

## Round 6 — First real CI compile (GitHub Actions, Release x64)
The whole solution **built clean**; the only failures were 4 errors in MY code,
`Layers/xrRender/SkeletonCustom.cpp` (reported by MSVC 18.9 / v143 / C++20):
- `error C2065: 'CTempReader': undeclared identifier` at line 462 → fixed by adding
  `#include "../../xrCore/fs_internal.h"` (CTempReader is declared in fs_internal.h).
- `error C3861: 'xr_strcpy_s' / 'xr_strcat_s': identifier not found` at lines 470–471 →
  those wrappers don't exist in OGSR; replaced with the same CRT calls the file already uses:
  `strncpy_s(child_name, sizeof(child_name), N, name_len)` + `strcat_s(child_name, "_smesh")`.
Also clamped `name_len` to leave room for the "_smesh" suffix + terminator.
Everything else — `source_mdl/*`, xrCore, xrRender_R4 (minus this file), xrGame, all 3rd_party
(DirectXTex/Xiph/lzo/zstd/lz4/ode/imgui/DiscordRPC/FidelityFX/LuaJIT/...), MSBuild deps fetch
(`Update_Components.cmd < nul`), PlatformShortName=x64 — worked on the runner.
NOTE: `xr_3da` (xrEngine.exe) and `source_mdl/*` had NOT yet compiled when the build aborted at
xrRender_R4; they compile on the NEXT run and may surface further MSVC-only issues.

## Round 7 — MSVC fixes (from CI run #2, xr_3da/source_mdl)
Now `xr_3da` compiled (source_mdl + xrEngine) and surfaced MSVC-only issues that g++ offline
could not catch:
- **C1010 (precompiled header)** in 9 files (anim, anim_decode, mesh, mesh_ogf, mesh_read,
  skeleton, split_mesh, vtx, vvd): `xr_3da` uses PCH (`Use` / `stdafx.h`); every .cpp must start
  with `#include "stdafx.h"`. Added as line 1 to all of them (except import.cpp which already had it).
- **import.cpp namespace imbalance**: `namespace SourceMdl` was closed twice (line 147 + 286) with
  only one open, so `TryImportSourceSkeleton` and `BuildSourceMeshOGFStream` sat at global scope and
  could not see `SourceMdl::CSourceMdlSkeleton` / `GetSourceToXRayBasisFmatrix` / `BuildEngineSkeleton`
  / `psSourceSkeletonMode` / `SourceMeshImport`. Removed the spurious close at the old line 147 so the
  namespace now wraps everything correctly (open 16 -> close 281).
- **C2665 in `TransformV`** (unused): `Fvector3().set(basis.transform_tiny(...))` — `transform_tiny`
  returns `void`. Removed the dead function.
- **Placeholder FVF resolved**: `OGF_VERTEXFORMAT_FVF_4L` is `5*0x12071980` (real value, fmesh.h),
  so the placeholder already matched; now referenced as the real constant. No change in bytes.
- Also removed stray compiled Linux test binaries (`test`, `test_units`, `test_real`, `test_ogf_stream`,
  `test_ogf`, `test_rle`, `test_anim_real`, `test_anim_decode_unit`) from the shipped package.

## Round 8 — single remaining LINK error (run #3)
Everything compiled; only the final link of `xrEngine.exe` failed:
- `LNK2001: unresolved external symbol "int psSourceSkeletonMode"` (+ psSourceMeshMode).
  Cause: `xrRender_console.cpp` declared them as GLOBAL (`extern int psSourceSkeletonMode;`) while the
  definitions live inside `namespace SourceMdl`. Fix: in `xrRender_console.cpp`, declare them in
  `namespace SourceMdl { extern ...; }` and reference as `&SourceMdl::psSourceSkeletonMode` /
  `&SourceMdl::psSourceMeshMode` in the CCC_Integer registrations. SkeletonCustom.cpp already used
  them correctly as `SourceMdl::`.

## Round 9 — ИМПОРТ РАБОТАЕТ В ИГРЕ (подтверждено в игре)
- `[SourceSkeleton] imported skeleton for ... [importer-v2]` — скелет из `.mdl` (49 костей).
- `[SourceMesh] imported 12487 verts / 16828 tris` — геометрия `.vvd` + `.dx90.vtx` подхвачена.
- `[SourceMesh] attached ... to '...'` — прикреплена как дочерний CSkeletonX_ST.
- Меш появился в игре; краши сняты последовательно:
  1. `FHierrarhyVisual::Load` Invalid visual -> добавлен OGF_CHILDREN в заглушку-ogf.
  2. `_CollectBoneFaces` access violation -> `child_faces.resize(children.size())` для всех костей.
  3. `Required dynamic game material` -> `game_mtl_name = "default_object"` (пустая строка в shared_str даёт nullptr-UB).
- Вторичные (не-критичные) следы для следующего раунда:
  - `! Can't find texture [models\hands\c_hands]` (нет текстуры).
  - `!Can't write file: ...\shaders_cache\...` (папка не создана).

## Round 10 (начато) — анимации v_knife: НЕ v49-формат
Проверяем конкретную модель `v_knife.mdl` (viewmodel-нож, GMod MW, извлечённый архив: mdl+vvd+vtx).
- Геометрия (`.vvd`/`.vtx`) читается ОК: 3201 verts / 4486 tris, bbox X-Ray Z-up корректен.
- Скелет: 198 костей, но `multiple root bones` (viewmodel руки+нож — независимые под-деревья) → адаптеру нужно разрешить несколько корней.
- АНИМАЦИИ: формат отличается от v49.
  - Найдено: `sizeof(mstudioanimdesc_t)` (animdesc stride) = **100** (не 92, как v49).
  - При stride=100 `numframes`@+16 и `animindex`@+56 корректны у 75/75 анимаций.
  - НЕ расшифровано: `fps` (не float/int 20..60 ни на одном смещении; вероятно, другое представление),
    и индекс имени секвенции (`szlabelindex` — похоже, относительный дельта-индекс в строковую группу,
    а не абсолютный offset).
- Файл собран не-каноническим тулом (видно `viper/mw/weapons/v_knife`, `ar_knife.smd`, строка-группа
  `@idle/@holster/@draw/@knife_*`) → это распакованный/нестандартный формат, НЕ канонический StudioMDL v49.

## Round 10 (продолжение) — расшифрована часть структуры animdesc нестандартных моделей
Проверены ДВЕ GMod-модели (v_knife и v_akilo47) — обе имеют НЕ v49-формат, а единый нестандартный
layout с `sizeof(mstudioanimdesc_t) = stride = 100` (не 92).
- v_akilo47: numbones=215, numlocalanim=164, animidx=48976, геометрия .vvd/.vtx читается (14359 verts/15913 tris, 6 мешей).
- Расшифрован layout animdesc (stride=100):
  - +0  : signed self index (== -anim_offset)
  - +8  : fps = 30.0  (float, совпадает у всех)
  - +16 : numframes (здесь у большинства ==1; реальное число кадров хранится не здесь)
  - +56 : animindex (каскад растёт ~3468/анимация — поправка: это канал/смещение анимации)
- НЕ расшифровано до конца: реальное число кадров и точное значение/назначение +56/+64 (два растущих
  каскада), а также привязка имени секвенции (szlabelindex, строковая группа «@idle» и др.).
- ВЫВОД: формат этих GMod-моделей — не канонический StudioMDL v49. Требует полной спецификации
  (или дальнейшего реверса полей), чтобы корректно читать анимации. Нож (v_knife) и v_akilo47
  страдают этим же.

## Round 10 (завершение) — раскладка stride=100 + рабочее декодирование каналов
Разобрано и РЕАЛИЗОВАНО. Ключевое открытие: анимационные блоки у обеих GMod-моделей — это
КАНАЛЬНЫЙ (не канонический StudioMDL v49) `mstudioanim`-формат, и "нестандартность" сводится
ИСКЛЮЧИТЕЛЬНО к страйду mstudioanimdesc_t (=100 вместо 92). После применения верного страйда
всё читается как обычная v49.

### 1. Анимационные блоки = стандартный mstudioanim (подтверждено)
`animindex@+56` у нестандартной раскладки — смещение ОТ БАЗЫ animdesc (adb + animindex), т.е.
дать стандартную семантику v49. По этому адресу лежит ЦЕПОЧКА каналов `{ byte bone; byte flags;
short nextoffset; data[] }` (header=4 байта; nextoffset — абсолютное смещение от текущей записи
к следующей; 0 = конец цепочки). Это полностью соответствует v49-му `mstudioanim_t`.

### 2. Смещения полей animdesc СОВПАДАЮТ с v49
Единственное отличие — страйд (92 vs 100). Поля те же:
  - +8  = fps (float; 30.0 у всех)
  - +16 = numframes (реальное число кадров; 1 для статических поз, до 771 для 'freefall' и т.п.)
  - +56 = animindex  (к цепочке mstudioanim, относительно базы animdesc)
  - +64 = blend-партнёр (0 = одиночная; парные анимации указывают друг на друга)
При stride=92 numframes прочитывается правдоподобно лишь у ~18/164 анимаций (мусор), при 100 — у
164/164 (детект по множеству кадров работает надёжно).

### 3. Каналы: RAW + ANIM (RLE) раскрыты полностью
Флаги в данных: 0x01 RAWPOS, 0x02 RAWROT(Quaternion48), 0x04 ANIMPOS, 0x08 ANIMROT, 0x10 DELTA,
0x20 RAWROT2(Quaternion64). Сжатые каналы (ANIMROT/ANIMPOS) используют `mstudioanim_valueptr_t`
(три short offset[i], относительные к началу valueptr) → три RLE-потока осей X/Y/Z.

Формат RLE (`mstudioanimvalue_t`, union из `{byte valid; byte total;}` и `short value`):
последовательность пар (valid,total): читаем valid значений int16 подряд, затем `total-valid`
кадров повторяют последнее. Распаковка даёт ЦЕЛЫЕ 16-битные компоненты поворота/позиции (масштаб
1/32768); для поворота w вычисляется из нормы=1. Проверено на живом канале bone9 (flags 0x0c)
модели v_akilo47: на 771 кадре получаются единичные кватернионы (|w²|=0.72..1.0, корректно).

### 4. Итоги чтения на реальных моделях (offline-валидатор `test_real`)
- gfl2_asteria_arms (стандартная v49, stride=92): `ok (1 seq)` 'idle' numframes=2 fps=30 — НЕ СЛОМАНО.
- v_knife: `ok (40 seqs)` — idle 2f, holster 16f, draw 18f, knife_miss_03/04 26f, knife_fatal_03/04 51f,
  inspect 154f (76 кост.), jog/walk/freefall/jump 21..112f; имена и число кадров корректны.
- v_akilo47: `ok (67 seqs)` — idle/holster/draw/reload*/fire/melee_*/ads_*/walk_loop 24f,
  freefall_loop 73f, jump 113f и т.д.
- Явная `V49AnimLayout100()` даёт тот же результат, что авто-детект страйда внутри `V49AnimLayout()`
  (в обоих случаях максимум раскодированных кадров: у ножа 154, у АК 113).

### 5. Реализация
- `source_mdl_anim.h`: в `ANIM_LAYOUT` добавлены поля базы-анимации `adb_*` (stride + смещения
  fps/flags/numframes/animindex/blend); добавлена `V49AnimLayout100()` (stride=100).
- `source_mdl_anim.cpp`:
  - Автодетект страйда animdesc (`DetectAnimDescStride`): пробует {страйд раскладки, 92, 100} и берёт
    тот, у которого правдоподобное numframes у наибольшего числа анимаций → читатель устойчив и к
    v49, и к нестандартным GMod-моделям БЕЗ ручного выбора.
  - Полная распаковка каналов: RAW (Quaternion48/64, Vector48) + ANIM (RLE по 3 осям через valueptr),
    флаг DELTA запоминается в треке. Защита от bad_alloc / выхода за буфер сохранена.
- Unit-тесты `test_rle_unit` и `test_anim_decode_unit` проходят; регрессии нет.

ПРИМЕЧАНИЕ (остаётся): скелет у обеих GMod-моделей multi-root (руки+оружие — независимые под-деревья),
так что `TryImportSourceSkeleton` требует поддержки нескольких корней/выбора назначенного корня — это
отдельный шаг (не анимации), отложен.

## Round 10 (доп.) — поддержка multi-root скелета (viewmodel: руки+оружие)
Раньше `CSourceMdlSkeleton::Parse` жёстко падал с "multiple root bones" — у вьюмоделей несколько
независимых под-деревьев (руки и оружие). Реализовано:
- Reader теперь ПРИНИМАЕТ несколько корней: список всех корней (parent==-1) в `GetRootMulti()`,
  первый найденный остаётся назначенным (`RootIndex()`). Ошибка "multiple root bones" убрана.
- `BakeSingleRoot("skeleton_root")` — приводит такой скелет к ЕДИНОМУ дереву (требование
  X-Ray CBoneData/CKinematics): добавляет один синтетический корень и подвешивает под него все
  старые корни. КРИТИЧНО: новый корень добавляется В КОНЕЦ массива костей (индекс == прежнему
  числу костей), поэтому индексы существующих костей, на которые ссылаются вершинные веса из
  .vtx (boneWeightIndex), НЕ ПЕРЕНОМЕРОВЫВАЮТСЯ; биновые трансформы старых корней сохраняются
  (ребро к синтетическому корню = identity). Проверено: bind старых корней не меняется, новый
  корень — identity, старые корни пере-подвешены под него.
- Реальные модели:
  - v_knife: skeleton OK, 198 костей, ДВА корня: bone0 `tag_origin`, bone111 `tag_knife_offset`.
    BakeSingleRoot -> 199 костей, единый корень `skeleton_root` (индекс 198), bind сохранён 2/2.
  - v_akilo47: skeleton OK, 215 костей, ТРИ корня: bone0 `tag_origin`, bone111 `tag_sling`,
    bone112 `j_gun`. BakeSingleRoot -> 216 костей, bind сохранён 3/3.
  - gfl2_asteria_arms (стандарт): 49 костей, 1 корень — без изменений/регрессии.
- Юнит-тест `test_source_mdl` (Round 10): синтетический 2-корневой скелет (origin->arm,
  weapon->blade) Parse OK, roots=2, BakeSingleRoot -> 5 костей, bind сохранён, корень identity.
  Все юнит-тесты проходят.

Следующий шаг (не выполнен, ожидает решения): сконвертировать MULTI-ROOT/вообще скелет в
X-Ray CBoneData/vecBones и подключить skinning в движке — но читать скелет много-корневой
модели и приводить его к единому дереву уже можно.

## Round 11 — движковый адаптер скелета (CBoneData/vecBones) + скиннинг: исправлен bind-конвейер
Разобран и исправлен движковый этап `BuildEngineSkeleton` (source_mdl_to_xray.cpp) — именно он
отдаёт скелет в `CKinematics` (через `TryImportSourceSkeleton` / `LoadSourceSkeleton`).

### Найденная ошибка (критично для скиннинга)
`CBoneData::CalculateM2B` накапливает `acc[child] = mul_43(acc[parent], bind[child])`, и m2b =
inverse(acc) (см. bone.cpp). Значит `bind_transform` ОБЯЗАН быть ЛОКАЛЬНЫМ (относительно
родителя). Старый код клал в `bind_transform` МОДЕЛЬНУЮ матрицу (basis * model_bind) и не
объединял multi-root в одно дерево — из-за этого `CalculateM2B` (рекурсия от одного корня)
не покрывал остальные под-деревья, а цепочка bind-матриц НЕ телескопировалась в корректный
model-frame. ОФФЛАЙН-проверка подтвердила: старый код давал совпадение 0/49..215 костей.

### Исправление
- `BuildEngineSkeleton` теперь считает кадр каждой кости в движковой системе:
  `mx[i] = model_bind_source[i] * basis` (через `mul_43(basis, model)`), затем даёт ЛОКАЛЬНУЮ
  (родитель-относительную) матрицу: `bind[корень] = mx[корень]`, `bind[dитя] = mx[dитя] * inv(mx[родитель])`.
  Fmatrix::mul_43(A,B) == B*A (см. xrCore/_matrix.h) — учтено в порядке аргументов.
- MULTI-ROOT объединяется в ОДНО дерево с корнем-индексом 0 (когда кость 0 — корень; иначе
  наименьший index root): вторичные корни подвешиваются под главный с сохранением model-кадра
  (точная композиция через inverse). Новых костей НЕ добавляется, индексы не переименовываются →
  boneMap остаётся identity и веса вершин из .vtx корректны.
- `CalculateM2B` вызывается от единственного корня → покрывает ВСЕ кости.

### Доказательство (движково-верный оффлайн-верификатор verify_xray_skeleton.cpp)
Зеркалит `mul_43` и `CalculateM2B` ровно как в движке:
- gfl2_asteria_arms (1 корень): telescope 49/49 (maxErr 4.8e-06), всё достижимо.
- v_knife (2 корня): 198/198 (maxErr 1.5e-05), достижимо 198/198.
- v_akilo47 (3 корня): 215/215 (maxErr 1.9e-05), достижимо 215/215.
- старый код: 0/… (ошибка подтверждена).

### Что осталось за этим раундом
- Итоговая on-screen (HUD) сверка знака/транспонирования базиса Source->X-Ray — только на
  реальных вьюмоделях в движке (запуск через CI/GitHub Actions). Чистая математика конвейера
  (локальные bind + single-root + m2b) проверена и согласована.
- `BuildEngineSkeleton` при верном `basis` (GetSourceToXRayBasisFmatrix) и корректной привязке
  весов теперь даёт валидный скин-скелет. Текстуры/материалы — следующий отдельный шаг.
