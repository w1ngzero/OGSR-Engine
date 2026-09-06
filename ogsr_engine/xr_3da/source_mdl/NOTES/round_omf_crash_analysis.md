# Round — OMF-build crash: root-cause analysis (knife draw)

## What the crash looked like
```
[SourceMotions] building OMF: nSeq=40 nBones=198 totalFrames=1708
... then heap fault, symbolised at source_mdl_anim_to_xray.cpp-->320
  via CKinematicsAnimated::Load :757 -> CWeaponKnife::Load :39
```
Line 320 at the time = `W16(body, M._keysR[f].x)` (inside `BuildXRayMotionsOMF`),
an 8× `body.push_back()` per rotation key.

## The key logical fact
`body` is a fresh local `std::vector<std::uint8_t>`. `push_back` on a valid vector
**cannot** corrupt the heap, and reading `M._keysR[f]` out of bounds is a *read* that only
yields a garbage *byte value* (harmless). Therefore a heap fault *surfacing at that line*
means the allocator noticed heap that was **already corrupted earlier in the process** —
the write itself is not the source.

## ASAN isolation work — every touched subsystem is clean
Tested on the real `/home/user/knife/ext/v_knife.mdl` (+ `.dx90.vtx`/`.vvd`), standalone,
`-fsanitize=address,undefined`:

| subsystem | result |
|---|---|
| `ReadSourceAnims` (reader/decoder) | clean, `nSeq=40`, every track `frames.size()==numframes` |
| `BuildXRayMotion` (CKeyQR/CKeyQT16/8 builder) | clean |
| `BuildXRayMotionsOMF` (writer) | clean, `omf=5000936 bytes` |
| OMF reader (`shared_motions`/motions_value::load) | clean |
| `BuildSourceMeshOGFStream` 2W emit | clean (fvf=2, vCount, payload==8+vCount·64) |
| 2W mesh loader (`_Load`, `_Load_hw` 2B, `_CollectBoneFaces`) | clean — max bone id 197 < 198, no OOB |

Also verified:
- `smem_container::dock` **copies** (`CopyMemory`) → `Vertices*W.create()` has no
  use-after-free when the generated `ogf`/`srcOg` buffer is destroyed.
- `BuildXRayMesh` already clamps the Source `-1` sentinel bone slots to bone 0, so no
  `matrix0/matrix1 == 65535` can reach `LL_GetData()` in `_CollectBoneFaces`.
- `vertBoned2W` is 64 bytes (pack(2)) both in the emitter and the loader; stride matches.

## Conclusion
The round-15 writer is **provably in-bounds** after the frame-resize guard
(`frames.size()==dwLen`, then `BuildXRayMotion` sizes `_keysR` to exactly `frames.size()`),
so the OOB `_keysR` write attributed to line 320 is no longer possible from this code.
The crash appears to be a **symptom of heap corruption originating elsewhere** (or from a
pre-guard build). The only corruption sources *not* testable in this sandbox are the ones
that run during the same `CKinematicsAnimated::Load` and live outside the anim/mesh paths:
the DX10 **shader/material load** for `models\weapons` + `models\hands\c_hands`
(ResourceManager `rm_geom.create`), and the **bone import + basis transform**.

## Decision point / next step (not yet executed)
1. Rebuild with the current sources (already in the zip) and re-test draw/holster.
   - If it **no longer crashes** → the writer guard resolved the trigger; proceed to the
     actual goal (does the draw/holster animate?).
   - If it **still crashes** → capture the **current** exact line + the two
     `[SourceSkeleton]`/`[SourceMesh]`/`[SourceMotions]` Msg lines, and consider an ASAN
     engine build to catch the true corruptor (likely the shader/material load, still out
     of scope per "пока не до шейдеров и текстур" unless explicitly reopened).

## Not addressed here (standing scope)
- Cordon→Dump knife-draw trigger: separate open issue (geometry/trigger, not shader/UV).
- Shader/texture visuals: explicitly out of scope.

---
## Addendum (после пересборки пользователя — новая игра)

Наблюдалось расхождение savegame vs. новая игра:
- **Сейв**: по-прежнему OMF-краш (стр. 320) — авто-сборка доходила до анимаций.
- **Новая игра**: `FATAL "Can't find model file [weapons\knife\v_knife.ogf]"` в `CModelPool::Instance_Load`
  (ModelPool.cpp:139) — авто-сборка вообще не запускалась.

Причина: `TryAutoBuildSkeletonOGF` первым условием проверяет `psSourceSkeletonMode` (консоль
`rs_source_skeleton`), по умолчанию 0. В новой игре флаг был 0 → авто-сборка пропускалась → FATAL.

Исправление (этот заход):
1. `rs_source_skeleton`/`rs_source_mesh` по умолчанию = **1**.
2. Явный `Msg` при пропуске авто-сборки.
3. Предохранитель в `BuildXRayMotionsOMF`: при коротких ключах — лог + чистый `return false` вместо
   тихого OOB-записи.
