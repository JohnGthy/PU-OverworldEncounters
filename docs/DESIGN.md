# Overworld Encounters: design

This document explains how the plugin works, so it can be maintained or ported (see also
`RE_NOTES.md` for the addresses and the reverse-engineering method).

## 1. Big picture

```
            plugin thread (CTRPF)                        game main thread
 ┌─────────────────────────────────┐        ┌────────────────────────────────────────────┐
 │ Watcher: every 250 ms checks    │ hooks  │ FieldRo per-frame event check  ──► Update() │
 │ where FieldRo.cro is loaded and │──────► │ FieldRo encounter step check   ──► EncountCheckHook
 │ (re)installs the FieldRo hooks  │        │ RoManager::DisposeModule       ──► unhook before unload
 └─────────────────────────────────┘        │ PokeSet "setup one Pokemon"    ──► SetupMonsHook
                                            │ InitialSpec normaliser         ──► SpecHook
                                            │ CoreParam::IsRare              ──► IsRareHook (PID fix)
                                            │ nngxGetPhysicalAddr            ──► PhysAddrHook
                                            │ TrialModel factory destructor  ──► FactoryDtorHook
                                            └────────────────────────────────────────────┘
```

All game functions are called from inside hooks that run on the **game's own thread**, so calling
them is as safe as if the game had called them. The plugin thread only installs and removes hooks.

## 2. Code layout

| File | Purpose |
|---|---|
| `plugin/Sources/main.cpp` | CTRPF entry point, menu, debug overlay, log file |
| `plugin/Sources/Owe.cpp` | the whole system (spawning, models, battles, hooks) |
| `plugin/Includes/GameOffsets.hpp` | every game address, one struct per game version |
| `plugin/Includes/Owe.hpp` | tunables (`Config`) and debug state |

## 3. Frame update (`Update`, called after the game's per-frame field event check)

1. **New field instance?** Battles and some transitions unload and reload `FieldRo.cro`. Each time
   the watcher re-hooks FieldRo, it increments `g_fieldGen`. When `Update` sees a new generation, it
   forgets all models (the field destroyed them) and resets its private heap.
2. **Player state.** `evCheck+0x14` is the player MoveModel: `+0xF8` is its model (position),
   `+0x3A8` the ground-collision root, `+0x314` the attribute of the ground under the player.
   `Field::Attribute::IsGrass(attr)` tells whether the player stands in grass.
3. **Memory release.** If nothing is spawned for 5 s, or the field was paused for more than 2 s
   (menus, cutscenes, battles), the model system is torn down so the game has its model factory
   back for scripted scenes (trials and Totems use the same factory).
4. **Model system bring-up** (only when we are about to spawn) mirrors the game's own
   `ScriptTrialModel::Update` states 1–3, but with our own heap (section 6).
5. **Existing spawns** run a small state machine:
   `Loading → Appear (scale 0→1 over 15 frames) → Idle (wanders, section 4b) → Vanish (scale 1→0)
   → Dying`. Pokemon too far away (> 1500 units) are removed.
   **Removing is always two-step** (`Despawn` → `Dying` → `ProcessDying`): the model is hidden,
   and only deleted once `IsLoaded` and `PokeModel::CanDelete()` are both true, the same rule the
   game's `ScriptTrialModel` uses. Deleting earlier leaves the async loader or the GPU working on
   freed memory.
6. **Touch.** If the player gets within 60 units of an Appear/Idle Pokemon, the battle is queued
   (`g_battlePending`, section 5).
7. **Spawning** (only while the player is in grass and the game has recently reported which
   encounter table this grass belongs to):
   * `ScanPatch` probes a 13×13 grid (75 units apart) around the player with the game's collision
     raycast, flood-filling only through grass cells connected to the player's cell. Result: the
     cells of **this** patch, and its size.
   * The patch target count is rolled once per patch: small (< 24 cells) 1–2, medium 2–3, large
     (≥ 60 of the 169 grid cells) 3–5, capped by `Config::maxModels` (menu: 5 or 3) and by how many
     models fit in memory (section 6).
   * `RollFromTable` picks species/form/level/shiny from the live encounter table (section 4).
   * `PickSpawnPoint` picks a random cell of the patch, at least 150 units from the player and not
     on top of another Pokemon.
   * Only one model loads at a time.

## 4. Which Pokemon (same patch, randomizer-proof)

In Gen 7, each area has several encounter tables (an `"EA"` BinLinker in the area's world data).
Which table applies is decided by *encounter regions* placed on the map. Each step on an encounter
tile, FieldRo calls its encounter check with the table index of the region the player is in
(`EncountCheck(this, force, tableIndex, attr)`). The plugin hooks that function:

* It records `tableIndex` → the patch the player is in.
* It returns 0, which turns off random encounters.
* `this+8` is the `BinLinkerAccessor` holding the area's tables. `GetData(tableIndex)` returns the
  live table, which is exactly what the game would use. Layout: 4-byte header, then day/night
  tables of 0x164 bytes each:
  `min level, max level, 10 slot rates, 10 slots (u32: species & 0x7FF, form >> 11)`.
  The day table is used for Alola time zones 0–2, the night table otherwise (same rule as
  `PokeSet::SetNormalEncountData`).

Because everything is read from the game's memory, encounter edits made by randomizers,
LayeredFS mods and pk3DS show up correctly.

## 4b. Wandering (Idle state)

Models are created with the species' **field motion pack** (`SetupOption+0xC = 2`, the pack the
Poké Finder uses) instead of the battle pack, which gives them a wait (`0`) and a walk (`2`)
motion. While Idle, each Pokemon loops:

* **Pause** 2–4 s on the wait motion, "looking around" with 1–2 smooth turns of 30–75°.
* **Pick a target** 60–110 units away (about 3 steps), preferably ahead. The middle and the end of
  the path must be grass at nearly the same height (collision raycast + `IsGrass`), at least 90
  units from the player and from the other Pokemon.
* **Turn** towards it, then **walk** (walk motion, 1.6 units/frame, ground height re-sampled every
  4 frames), then pause again.

Species without a walk motion just pause and look around. `Config::wander` turns it off.

## 4c. Size and cries

* **Size**: `modelScale` (1.15) for every Pokémon. After setup the model's bounding box
  (`PokeModel::GetMotionCollision(AABB*)`, `AABB = {min xyzw, max xyzw}`) gives its size; anything
  below `smallSize` (80 units; Inkay ≈ 57, Slowpoke ≈ 154, Wingull 29 tall/177 wide) is scaled up
  to that size, by at most `maxSmallBoost` (1.6). The touch distance grows with the scale.
* **Cries**: `Sound::PlayVoice(channel 0, species, form, type 0, 0, 0)` then
  `Sound::ChangeVoiceVolume(0, vol, 0)` and `Sound::ChangeVoicePitch(0, 0.95–1.05)`, as the Poké
  Finder does (plus the pitch variation). Sound asserts on a bad channel, so the plugin checks the
  voice-player count (`*(0x671AEC + 4)`, 2 in the field) first, and `IsVoiceFinished(0)` so cries
  never cut each other off. Cries are tied to behaviour: 1 in 3 when a Pokémon pops out, 1 in 6
  when it stops after a walk, 1 in 10 when it looks around, plus about once per 40 s at random;
  at least 7 s between two cries, silent beyond 900 units, volume 0.8 fading to 0.16 with distance.
  With a full patch that is about one cry every 10 s.
* **Walking never starts a battle**: a walk target is rejected if any point of the path comes
  within `walkKeepAway` (90) of the player, and a walking Pokémon stops 30 units outside its
  touch range if the player comes closer. Battles only start when the player walks into one.
* **Standing still**: the game only reports the patch's encounter table on steps; while the
  player stays in the same grass (< 50 units from where it was last reported) the table stays
  valid, so Pokémon keep appearing.

## 5. Battles

Touching a Pokemon hides all of them and queues the battle. Each frame `FactoryTeardown` then
shuts the whole model system down (models, then the factory's PokeModelSystem), and only then is
the battle started. This takes a few frames.

**Why (v0.2 freeze on the third battle):** the field's TrialModel factory destructor (FieldRo
`0x8754`) asserts if its PokeModelSystem is still running. The game's scripts always terminate it
before a Totem battle; v0.2 did not, and the game hung a couple of battles later. For everything
else that can end the field while Pokemon are out (trainer battles, warps, cutscenes),
`FactoryDtorHook` runs right before the destructor and shuts the system down synchronously
(normally within a frame).

`TriggerBattle` calls the game's own encounter check with `force = 1` and the Pokemon's table
index (`attr = 0xFFFF` = "use the player's current ground attribute"). This runs the game's normal
wild-battle flow (music, transition, battle setup, `EventBattleCall::CallWild`). Two hooks make it
use *our* Pokemon:

* **`SetupMonsHook`** (PokeSet "setup one entry from slot", v1.2 `0x3A8584`) runs after the game
  rolled its slot and overwrites the entry: `+0 u16 species`, `+4 u8 level`, `+5 u8 form`.
* **Shininess.** The wild `PokemonParam` is built from a pml `InitialSpec` with rare mode
  `(-1, 3)` (random, N tries). Right after creating it, the game calls `CoreParam::IsRare()` on it.
  `SpecHook` notices the wild spec (and learns N, which is 3 with the Shiny Charm). The next
  `IsRare` call (`IsRareHook`) rewrites the PID (core data `+0x10`) so that
  `(ID.hi ^ ID.lo ^ PID.hi ^ PID.lo) < 16` exactly when the overworld model was shiny. The ID is the
  player's TID/SID (core data `+4`). Battle copies are made after this point, so the shiny state
  carries into the battle, the sparkle animation and the caught Pokemon.

All spawns are already gone when the battle starts.

## 6. Showing any species in the overworld (TrialModel factory)

Gen 7 already shows battle models in the field: Totem Pokemon and Poke Finder subjects use
`Field::TrialModel::FieldTrialModel`, created by a factory (`Fieldmap+0x124`). The plugin drives
that factory exactly like the game's `ScriptTrialModel::Update` (v1.2 `0x38D44C`):

| step | game function (FieldRo offset, v1.2) |
|---|---|
| init system `(fac, heap, heap, maxModels, 1)` | `0x7FCC` |
| wait until initialised | `0x7210` |
| create system `(fac, heap, 0, 0, 0)` | `0x7104` |
| per-model heaps, with an explicit size | `PokeModelSystem::CreatePokeModelHeap` (code.bin) |
| create model `(fac, heap, SimpleParam*, SetupOption*)` | `0x7E08` |
| wait loaded / setup / ambient / visible | `0x6F0C` / `0x6C1C` / `0x6EF0` / `0x72974` |
| delete model `(fac, model)` | `0x7F3C` |
| terminate: request / wait / final | `0x71AC` / `0x86AC` / `0x7DB0` |

`SimpleParam` = `{u16 species, u8 form, u8 sex, u8 rare, u8 egg, …}`. **`rare = 1` gives the shiny
model.** `SetupOption` = `{heap, device heap, u32, u8 motion pack, …}`; the plugin asks for the
field pack (2). `FieldTrialModel+0x20` is the `PokeTool::PokeModel` (a `poke_3d::model::BaseModel`), so
`BaseModel::SetPosition/SetRotation/SetScale` place, turn and scale it (scale is used for the
pop-in and shrink-out).

### Memory

The default per-model heap is 3.25 MB (`CalcPokeModelHeapSize`). `HeapOption+4` overrides that
size, and the plugin uses 2.25 MB per model. That's more memory than the game has free, and the
memory must be GPU-visible:

* The plugin allocates extra **linear** memory with `svcControlMemory(LINEAR)` and builds a gfl2
  heap inside it (`0x1052E8`, the "create heap in a buffer" routine). It asks for 12 MB and steps
  down to 3 MB if the system has less free (New 3DS mode had 38 MB free, Old 3DS mode 4.25 MB).
  The block is freed again whenever the model system shuts down (before battles, after 5 s with
  nothing spawned), so the game has it back during battles and cutscenes.
* **How many models fit is computed from what was actually obtained**
  (`(block − systemHeap) / perModelHeap`, capped by `maxModels`) and that number is passed to the
  factory's init. In New 3DS mode the 8 MB block fits 3 models. In Old 3DS mode only 4 MB is free,
  which fits 1 model at a time. 12 MB fits 5.
* The GPU addresses buffers through `nngxGetPhysicalAddr`, which only knows the game's original
  device block, so `PhysAddrHook` translates the plugin block too, with the game's own formula:
  VA ≥ `0x30000000` → PA = VA − `0x10000000`, otherwise PA = VA + `0x0C000000` (the Old 3DS
  linear heap at `0x14000000`). Without this hook the emulator crashes as soon as a model is drawn.

**Old 3DS mode freeze (fixed in v0.2).** v0.1 always initialised the factory for 3 models. In Old
3DS mode (the default in some emulators, such as the Dekopon Switch port of Azahar) only a 4 MB
block was available, so creating the per-model heaps failed a gfl2 heap assert. The game thread
stopped in the assert loop on the first spawn in grass, while audio, which runs on another thread,
kept playing.
* Memory taken from the game's own heap 11 also works (tested), but there is only room for one
  large Pokemon there.

## 7. FieldRo life cycle

`FieldRo.cro` is loaded at a runtime address (seen at `0x006DF000`). The base is recovered from a
code.bin import stub that RO patches with an absolute FieldRo address
(`*(u32*)0x10F528 − 0x7F3C`; the CRO magic is `"FIXD"` after fix-up).
CTRPF hooks write into the target, so they must be removed **before** RO unloads FieldRo: the
plugin hooks `gfl2::ro::RoManager::DisposeModule` and removes its FieldRo hooks when the disposed
module is FieldRo (`Module* == CRO base`). The watcher re-hooks after the reload.

## 8. Tunables (`plugin/Includes/Owe.hpp`)

`maxModels`, `perModelHeap`, `lifeMin/MaxFrames`, `popFrames`, `touchDist`, `cellSize`,
`gridRadius`, `smallPatchCells`, `spawnMinDist`, `despawnDist`, `modelScale`, and for wandering
`wander`, `walkSpeed`, `walkDistMin/Max`, `walkKeepAway`, `turnSpeed`, `pauseMin/MaxFrames`.
Testing aids: `debugNearSpawn` (spawn just in front of the player), `debugAlwaysShiny`,
`debugAutoBattle` + `debugSkipTeardown` (battle stress test with `tools/battleloop.sh`) and
`verboseLog`.

## 9. Ideas for later

* Fleeing and chasing behaviour (the field pack also has a run motion, slot 3).
* A grass-rustle effect when a Pokemon pops out (`EffectEncGrass`).
* Shiny sparkle in the overworld.
* Ultra Sun and v1.0 offset tables (method in `RE_NOTES.md`).
