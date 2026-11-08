# Reverse-engineering notes (Pokemon Ultra Moon v1.0 / v1.2)

These notes record how every piece was found, so the work can be repeated for another version or
game. All addresses are for **Ultra Moon v1.2** (title version v2080) unless marked 1.0.

---------------------------------------------------------------------------------------------------

## 1. Getting the files

| What | How |
|---|---|
| Base game | Decrypted `.cci`. `ctrtool -i` checks it (CryptoType none). `ctrtool -n 0 --exefsdir --romfsdir` extracted exefs and the root `.cro` files but failed on `romfs/a/...` ("File does not exist"), so `tools/romfs_extract.py` (an IVFC level-3 RomFS reader) was written instead. |
| Update v1.2 | Decrypted `.cia` (shown as v2.2.0 = title version 2080). `tools/ctr.py` parses the CIA (TMD → content 0 → NCCH), ExeFS and RomFS. The update's `.code` is **not** compressed (exheader flag 2). The update RomFS contains `static.crs`, every `.cro` and `a/0/3/x, a/0/6/7, a/0/9/2, a/1/4/2, a/1/4/7`. |
| GARCs | `tools/garc_unpack.py`. Entries are LZ11 (`tools/lz11.py`); BinLinker "mini" packs (2-char magic + count + offsets) with `tools/mini.py`. |

Useful GARCs (UM): `a/0/8/3` world/area data (336 areas × 11 files; file `9+11*i` = `"EA"` encounter
tables; others `ED, BG, TR, AC, AS, AE, ZS, ZI`), `a/0/7/7` zone data, `a/0/9/1` world data,
`a/0/9/4` Pokemon battle models (1.3 GB), `a/1/6/9` (ARC id 0xA9) TrialModel motion packs
(12 scene-specific sets, not per species).

## 2. Symbols: the key shortcut

`romfs/static.crs` (the CRS of code.bin) **exports 4916 mangled C++ names with addresses**
(`tools/crsexp.py`). Demangling gives names such as
`Field::Encount::PokeSet::SetEncountDataFromParam(MonsNo, uchar, uint, BattleEffectId)`. 1.0 and
1.2 each have their own `static.crs`, so named functions can be looked up for both versions.

`FieldRo.cro` (the field module, ~1 MB of code) imports about 1200 of those by name.
`tools/croload.py` loads a CRO, applies its relocations and resolves its imports against
`static.crs`. `tools/fdis.py` (1.0) and `tools/fr12.py` (1.2) disassemble with capstone and label
every call (`>Name` = import stub, `FR_xxxx` = internal). Import stubs in a CRO look like
`.word target ; ldr pc,[pc,#-4]` (stub address = word address − 4).

The reverse direction also works: code.bin calls into FieldRo through **anonymous imports**
listed in `static.crs` (`tools/staticimp.py`). Each one maps a code.bin stub word to a FieldRo
offset. That's how the FieldRo functions used by `ScriptTrialModel` were identified, and how the
plugin finds FieldRo's runtime base.

RTTI strings (`N5Field…E`) → typeinfo → vtable lead to class methods (for example
`Field::TrialModel::FieldTrialModel` vtable at FieldRo `0xD5E78`, 1.2).

Version matching: `tools/pmatch.py` compares functions with branch targets and PC-relative
literals masked out. That's how the unnamed 1.2 functions were matched to 1.0 (and FieldRo 1.0 →
1.2).

## 3. Address table

### code.bin (v1.2; 1.0 in brackets where known)

| Address | What |
|---|---|
| `0x001048B4` | `SingletonAccessor<GameSys::GameManager>::GetInstance()` → GM. `GM+0x24` GameData, `GM+0x2C` Fieldmap |
| `0x00361724` / `0x003617B0` / `0x00107374` | gfl2 heap: `CreateHeap(parent,id,size,type,bool,name)`, `DeleteHeap`, `GetHeapByHeapId` |
| `0x001052E8` | create a gfl2 heap inside a raw buffer `(buf, id(-2 = auto), size, type, 0, 0)` |
| heap vtable `[10]`, `[11]` | total free / allocatable size (`[11](heap, align)`) |
| `0x004C37BC`, `0x0042B4E0`, `0x0042B710`, `0x0042C9D0` | `BaseModel::GetPosition`, `SetPosition(f,f,f)`, `SetRotation(f,f,f,order)`, `SetScale(f,f,f)` |
| `0x003A8180` [1.0 `0x3A6D74`] | `PokeSet::EncountProbCalc` |
| `0x003A83D8` [1.0 `0x3A6FCC`] | slot lottery + per-Pokemon setup loop (unnamed) |
| `0x003A8584` [1.0 `0x3A7178`] | setup one PokeSet entry: `+0 u16 species, +4 u8 level, +5 u8 form` |
| `0x003A876C` | `PokeSet::SetNormalEncountData(ENCOUNT_DATA const&, BattleEffectId)` (copies 0x2CC table to PokeSet+0x21C) |
| `0x003A646C` | `Field::EvTime::GetAlolaTimeZone(GameData*)` (0–2 = day table, else night) |
| `0x003A9944` | `Field::Attribute::IsGrass(attr)` |
| `0x004B5474` / `0x004B54E8` | `BinLinkerAccessor::GetDataMax` / `GetData(i)` |
| `0x00320C4C` [1.0 `0x320528`] | InitialSpec normaliser: `+0 u64 personalRnd, +8 u64 rare mode/rnd, +0x10 id, +0x23 u8 tries`. Rare mode `(-1,1)` force *not* shiny (`0x323CD4`), `(-1,2)` force shiny (`0x323C70`), `(-1,3)` random with tries |
| `0x003239F4` | `CoreParam::IsRare()`: compares core data `+4` (ID = TID\|SID<<16) and `+0x10` (PID) with `0x323A20` |
| `0x00322CB0` / `0x004AF138` / `0x00322C38` | core data access begin / block pointer `(data, writable)` / end (sequence used by `CoreParam::SetID` `0x322DAC`) |
| `0x00279FC0` | `nngxGetPhysicalAddr` (only knows the app's device block) |
| `0x0034DFCC` | `gfl2::ro::RoManager::DisposeModule(nn::ro::Module*)` (Module* = CRO base) |
| `0x0038D44C` | `ScriptTrialModel::Update` (state machine, the template for our model handling) |
| `0x00445298` / `0x004456A8` | `PokeModelSystem::CreatePokeModelHeap(heap, HeapOption*)` / `CalcPokeModelHeapSize` (`HeapOption+4` = explicit size, default 0x340000) |
| `0x003BF630` / `0x003BDDD0` / `0x003BD848` | `Sound::PlayVoice(u8 ch, MonsNo, u8 form, VoiceType, int, u32)` / `ChangeVoiceVolume(u8 ch, float, u32)` / `IsVoiceFinished(u8 ch)`. Voice player table at `0x671AEC`, `+4` = channel count (asserts if `ch >= count`) |
| `0x0044B040` | `PokeModel::GetMotionCollision(AABB*)`: bounding box of the current motion, scaled by the model scale |
| `0x0044BC18` | `PokeTool::PokeModel::CanDelete()` (virtual; the game waits for it before deleting a model) |
| `0x0044A9D4` / `0x0044A940` | `PokeModel::ChangeAnimation(MODEL_ANIME, bool)` / `IsAvailableAnimation(MODEL_ANIME)`: the index goes straight into the loaded motion pack (`pm+0x12AC` count, `+0x12B0` array) |
| `0x004450E0` → `0x004492A8` | `PokeModelSystem::CreateModelSimple` → PokeModel setup: copies `SetupOption` (0xF bytes) to `pm+0x121C`. **`SetupOption+0xC` (u8) selects the motion pack**: 0 battle (32 slots), 1 Pokémon Refresh (40), 2 field / Poké Finder (27), 3 a fourth pack |
| `0x00444CCC` / `0x00444F68` | `PokeModelSystem::InitSystemAsync` (opens ARC `0x5E` and `0x61`) / `TermSystemAsync` (closes them) |
| `0x0010F528` | import word → FieldRo `0x7F3C` (used to find the FieldRo base) |

### FieldRo.cro (offset from the CRO image; v1.2 [1.0])

| Offset | What |
|---|---|
| `0x75488` [`0x7511C`] | encounter step check `(this, force, tableIdx, attr)`. `this+8` = BinLinker of the area tables. `mov r0,#100 ; bl GetPublicRand` at `+0x130` (AlolanCTRPF's "no wild Pokemon" patch = FieldRo `0x807E5B8` when loaded at `0x08009000`) |
| `0x256B4` [`0x25518`] | per-frame field event check `(this)`. `+4` GM, `+0x10` Fieldmap, `+0x14` player MoveModel |
| `0x24F80…` [`0x24E40…`] | caller: decides whether to run the encounter check (`this+0x34` table index, `+0x38` flags) |
| `0x73C08` [`0x7398C`] | collision raycast `(root, Vec4* from, Vec4* to, Out*)`. `Out+0x10` = CollisionTriangle*, triangle `+0x40` = ground attribute |
| `0x7FCC 0x7210 0x7104 0x7E08 0x7F3C 0x71AC 0x86AC 0x7DB0` | TrialModel factory: init, initialised?, create system, create model, delete model, term request, term done?, term final |
| `0x6F0C 0x6C1C 0x6EF0 0x72974` | FieldTrialModel: loaded?, setup, ambient, visible |
| `0x6BCC` | FieldTrialModel: load motion pack (scripts load ARC 0xA9; not used by the plugin) |
| `0x8754` | TrialModel factory destructor, called by the Fieldmap teardown (`0x80678`). **Asserts if the factory's PokeModelSystem (`fac+0`) still exists**, i.e. if it was not terminated (`0x71AC`/`0x86AC`/`0x7DB0`) first |
| `0x95CC…0xA0CC` | `FinderModelControlWalker` (Poké Finder walking Pokémon; vtable at `0xD5F18`) |

**Pokémon motion packs** (GARC `a/0/9/4`, LZ11, `PC` containers; per species/form: model,
textures, then battle / Refresh / field motion packs). Field pack slots present for every species
checked: `0` wait, `2` walk, `3` run (plus `13`/`14` face, `17`, `24`, `25`; some have `1`). The
plugin plays `0` and `2`.

Player MoveModel: `+0xF8` model, `+0x314` ground attribute, `+0x3A8` collision root. Fieldmap:
`+0x124` TrialModel factory, `+0xF8` encounter object.

### Game heaps seen in the overworld (free bytes)

`id 1` 1.2 MB, `id 3` 1.0 MB, `id 7` 2.0 MB (device), `id 11` 4.5 MB (field/event). Models need
GPU-visible memory inside a block `nngxGetPhysicalAddr` can translate.

## 4. Toolchain and test rig (all in the cloud session, reproducible)

* **devkitARM r65** (gcc 16.1): `pkg.devkitpro.org` is behind Cloudflare, so the
  `devkitpro/devkitarm` Docker image layers were pulled from the registry API and extracted to
  `/opt/devkitpro`.
* **CTRPluginFramework**: the prebuilt `libctrpf.a` in AlolanCTRPluginFramework is too old. It
  loads in Azahar but never reaches `main` (no log, no menu). The current ThePixellizerOSS source
  (gitlab, commit 96fd3d9) plus libcwav is built by `scripts/get_ctrpf.sh` (pinned to that commit).
* **.3gx packing**: `plugin/mk3gx.py` is a Python reimplementation of 3gxtool v1.3 (header,
  default checksum payload, no symbols).
* **Emulator**: Azahar 2126.2 AppImage under Xvfb (llvmpipe), plugin loader on, gdbstub on port
  24689 (`gdb-multiarch`: `target remote :24689`). `tools/run.sh` restarts it with the new build;
  `tools/emu.sh` sends keys and takes screenshots. Keys: A=`a`, B=`s`, X=`z`, Y=`x`,
  Start=`m`, Select=`n`, circle pad = arrow keys.
* Pitfall: `pkill -f AppRun.wrapped` from a shell whose own command line contains that string
  kills the shell instead. Use `pkill -9 -x AppRun.wrapped`. Several stray emulator instances made
  everything look 10× slower and "frozen" for a while.
* A game freeze with the main thread looping at `GFLassertStop` (`0x1074BC`) means a gfl2 assert.
  Read the return addresses from the stack in gdb; most were heap allocation failures.
* Stress test: build with `debugAutoBattle` (and optionally `debugNearSpawn`,
  `debugSkipTeardown`), then `tools/battleloop.sh N` runs N battle → flee → field round trips by
  itself. v0.2 froze on the third round trip; v0.3 passes 10 in a row (New 3DS mode) and 3 in Old
  3DS mode.

## 5. Adding another version (Ultra Sun, USUM 1.0)

1. Extract `exefs/code.bin`, `static.crs` and `FieldRo.cro` for that version.
2. Named functions: look them up in that version's `static.crs` (`crsexp.exports()`).
3. Unnamed code.bin functions (`SetupMons`, InitialSpec normaliser, core access helpers, heap from
   buffer): `pmatch.find()` with the 1.2 bytes. 1.0 values found so far: `0x3A7178`, `0x320528`.
4. FieldRo offsets: `pmatch.find(fr12.m, offset, fdis.m, 0x180, 0xD0000)`. 1.0 values found so far
   are in the table above.
5. Import stub for the base: find the code.bin stub whose anonymous import resolves to the FieldRo
   delete-model function (`staticimp.fieldro_stubs()`).
6. Add an `Offsets` table to `GameOffsets.hpp` and select it in `Owe::Init` by title ID and
   version. Note: `Process::GetVersion()` returned **2** for the 1.2 update under Azahar, not 2080.

## 6. Porting to Omega Ruby / Alpha Sapphire (planned next)

ORAS is Gen 6 and its engine is quite different, so plan it as a new port that reuses the
*design*, not the addresses:

* **No CROs and probably no exported symbol table.** XY/ORAS don't ship `static.crs` with
  thousands of names, so expect to name functions by hand (Ghidra plus known structures from
  PKHeX / pk3DS, and community RAM maps).
* **Encounter data**: ORAS encounters are per-zone binaries (pk3DS `RSWE`/`OWSE` code shows the
  formats). The live encounter table in RAM is again the randomizer-proof source.
* **Visible-Pokemon precedents in ORAS**: the DexNav "hidden Pokemon" (rustling grass with a
  sneaking Pokemon) already has the game deciding a specific Pokemon before the battle. Its code
  path, from choosing the Pokemon to starting the battle, is the equivalent of
  `PokeSet`/`CallWild` here. Soaring and legendary encounters show battle-quality models in the
  overworld; find the class that does that (the counterpart of `FieldTrialModel`).
* **Plugin plumbing reused as is**: CTRPF hooks on the game thread (per-frame field update +
  encounter check), a lifetime state machine, patch flood-fill over the ground-attribute raycast,
  slot rolling from the live table, overriding the generated Pokemon before the battle, and the
  PID shiny fix (Gen 6 uses the same `TSV ^ PSV < 16` rule; check the per-roll odds of 1/4096 and
  the Shiny Charm).
* Lifetime: whatever model system the port uses, shut it down completely before anything that ends
  the overworld (battles, warps). USUM v0.2 froze on the third battle because the field was
  destroyed while the TrialModel system was still running (its destructor asserts). Also wait for
  the game's "can delete" check before freeing a model.
* Memory: ORAS runs in the Old 3DS memory mode. Check free heaps the same way (heap vtable sizes)
  before choosing a model memory strategy. Test in **Old 3DS mode**: USUM's v0.1 froze there
  because only 4 MB of extra linear memory was free and the factory was set up for 3 models. Size
  the model count from the memory you actually got, and use the game's own
  `nngxGetPhysicalAddr` formula for both linear-heap locations (`0x14000000` on Old 3DS,
  `0x30000000` on New 3DS).

The other requested features (mints and bottle caps in the Poke Mart, PC gender change) come
after this mod is finished. For USUM they would be item-data / shop-table / script edits plus
small code hooks, and the same RE workflow applies.
