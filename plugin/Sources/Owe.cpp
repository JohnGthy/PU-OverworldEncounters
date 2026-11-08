// Core of the overworld-encounter system.  Everything here runs on the GAME thread, from inside
// hooks on the game's own per-frame field update and encounter check, so game functions can be
// called safely.  See docs/DESIGN.md.
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstring>
#include <cmath>
#include "GameOffsets.hpp"
#include "Owe.hpp"

using namespace CTRPluginFramework;

namespace OWE {

// ------------------------------------------------------------------------------------------
// small helpers
// ------------------------------------------------------------------------------------------
template <typename R = u32, typename... A>
static inline R Call(u32 addr, A... a) { return reinterpret_cast<R (*)(A...)>(addr)(a...); }

struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct RayOut { Vec4 pos; u32 tri; u32 extra[11]; };   // tri+0x40 = ground attribute

// PokeTool::SimpleParam as filled by ScriptTrialModel (code.bin 0x38D708, v1.2)
struct SimpleParam { u16 mons; u8 form; u8 sex; u8 rare; u8 egg; u8 pad[2]; u32 perRand; };
// PokeTool::PokeModel::SetupOption as filled by ScriptTrialModel
struct SetupOption { void *heap; void *devHeap; u32 unk8; u8 b0, b1, b2, b3; u32 pad[4]; };

static const Offsets *O = nullptr;
static Config g_cfg;
static Debug  g_dbg;
static u32    g_frame = 0;
static u32    g_rng = 0x1234567;
static int    g_shinyRolls = 1;   // learned from the game's own wild spec (Shiny Charm = 3)

static u32 Rand(u32 n) {               // xorshift, n > 0
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return n ? g_rng % n : 0;
}
static float Randf(float a, float b) { return a + (b - a) * (Rand(10000) / 10000.0f); }

// ------------------------------------------------------------------------------------------
// FieldRo base address (FieldRo.cro is loaded at runtime; static code.bin import stubs are
// patched with absolute FieldRo addresses, so one known stub gives us the base).
// ------------------------------------------------------------------------------------------
u32 Owe::FieldRoBase() {
    if (!O) return 0;
    static u32 lastTarget = 0, lastBase = 0;
    u32 target = *(volatile u32 *)O->Static_FieldRoStubWord;
    if (target == lastTarget) return lastBase;
    lastTarget = target; lastBase = 0;
    u32 base = target - O->FR_StubTarget;
    u32 magic = 0, fn = 0;
    bool readable = (base & 0xFFF) == 0 && Process::Read32(base + 0x80, magic);
    if (readable) Process::Read32(base + O->FR_EncountCheck, fn);
    // CRO0 magic, and the encounter-check prologue (push {r4-r11,lr}) where we expect it
    bool ok = readable && (magic == 0x304F5243 || magic == 0x44584946) && fn == 0xE92D4FF0;  // "CRO0" or "FIXD" (RO rewrites it after fix-up)
    Log("FieldRo stub word %08lX -> base %08lX magic %08lX fn %08lX ok=%d", target, base, magic, fn, ok);
    if (ok) lastBase = base;
    return lastBase;
}
Config &Owe::Cfg() { return g_cfg; }
Debug  &Owe::Dbg() { return g_dbg; }

// ------------------------------------------------------------------------------------------
// Encounter table access + our own copy of the game's slot lottery
// (mirrors PokeSet::SetNormalEncountData / 0x3A83D8 / 0x3A8584 in v1.2).
// ------------------------------------------------------------------------------------------
struct Rolled { u16 species; u8 form; u8 level; bool shiny; };

static void  *g_encSelf = nullptr;     // FieldRo encounter-check object (captured in hook)
static int    g_curTable = -1;         // encounter-data index of the grass patch the player is in
static u32    g_curTableFrame = 0;

static const u8 *GetTable(int idx) {
    if (!g_encSelf || idx < 0) return nullptr;
    void *acc = *(void **)((u32)g_encSelf + 8);
    if (!acc) return nullptr;
    u32 max = Call<u32>(O->BinLinker_GetDataMax, acc);
    if ((u32)idx >= max) return nullptr;
    return Call<const u8 *>(O->BinLinker_GetData, acc, (u32)idx);
}

static bool RollFromTable(int idx, Rolled &out) {
    const u8 *t = GetTable(idx);
    if (!t || t[0] >= 9) return false;
    void *gm = Call<void *>(O->GameManager_GetInstance);
    void *gameData = *(void **)((u32)gm + 0x24);
    int tz = Call<int>(O->GetAlolaTimeZone, gameData);
    const u8 *d = (tz == 0 || tz == 1 || tz == 2) ? t + 4 : t + 0x168;   // day / night table
    u8 minLv = d[0], maxLv = d[1];
    const u8 *rates = d + 2;
    const u32 *slots = (const u32 *)(d + 0xC);
    u32 r = Rand(100), acc = 0; int s = 0;
    for (; s < 10; s++) { acc += rates[s]; if (r < acc) break; }
    if (s >= 10) s = 0;
    u32 slot = slots[s];
    out.species = slot & 0x7FF;
    u8 form = slot >> 11;
    out.form = (form >= 0x1E) ? 0 : form;
    if (maxLv < minLv) maxLv = minLv;
    out.level = minLv + Rand(maxLv - minLv + 1);
    out.shiny = g_cfg.debugAlwaysShiny;
    for (int i = 0; i < g_shinyRolls && !out.shiny; i++) out.shiny = Rand(4096) == 0;   // 1/4096 per roll
    return out.species != 0;
}

// ------------------------------------------------------------------------------------------
// Ground probing (same collision raycast the game uses to find a MoveModel's ground attribute)
// ------------------------------------------------------------------------------------------
static bool ProbeGround(void *root, float x, float yRef, float z, float &outY, u32 &outAttr) {
    u32 fr = Owe::FieldRoBase();
    if (!fr || !root) return false;
    Vec4 from = {x, yRef + 300.0f, z, 0.0f};
    Vec4 to   = {x, yRef - 600.0f, z, 0.0f};
    RayOut ro; memset(&ro, 0, sizeof(ro));
    bool hit = Call<bool>(fr + O->FR_Raycast, root, &from, &to, &ro);
    if (!hit || !ro.tri) return false;
    outY = ro.pos.y;
    outAttr = *(u32 *)(ro.tri + 0x40);
    return true;
}

static bool IsGrassAttr(u32 attr) {
    return Call<bool>(O->Attribute_IsGrass, attr);
}

// ------------------------------------------------------------------------------------------
// Spawned Pokemon
// ------------------------------------------------------------------------------------------
// Dying = hidden and waiting until the game may free it (see RequestDelete).
enum class SpawnState : u8 { Free, Loading, Appear, Idle, Vanish, Dying };
// Wandering while Idle: pause (look around) -> turn towards a target -> walk ~3 steps -> pause ...
enum class Wander : u8 { Pause, Turn, Walk };
struct Spawn {
    SpawnState state = SpawnState::Free;
    void  *model = nullptr;          // Field::TrialModel::FieldTrialModel*
    Rolled mon{};
    int    table = -1;
    float  x = 0, y = 0, z = 0, yaw = 0;
    int    timer = 0, life = 0;
    Wander ws = Wander::Pause;
    int    wt = 0, looks = 0, walkFrames = 0;
    float  tx = 0, ty = 0, tz = 0, tyaw = 0;
    bool   canWalk = false;          // the species' field motion pack has a walk motion
    bool   shown = false;            // made visible at least once
    float  sizeScale = 1.0f;         // per-species boost so small Pokemon stay visible
};
static const int kMaxSpawns = 5;   // hard cap; Config::maxModels and memory are the runtime limits
static Spawn g_spawns[kMaxSpawns];

// TrialModel factory bring-up (mirrors ScriptTrialModel::Update states 1-3, code.bin 0x38D44C)
static void *g_heap = nullptr;
static void *g_factory = nullptr;
static int   g_facState = 0;          // 0 none, 1 initializing, 2 ready, 3 terminating, -1 failed
static u32   g_facGen = 0;           // field generation the factory state belongs to
static volatile u32 g_fieldGen = 0;  // bumped every time FieldRo is (re)hooked = new field instance

static u32 g_evFieldmap = 0;
static void *Fieldmap() {
    void *gm = Call<void *>(O->GameManager_GetInstance);
    return gm ? *(void **)((u32)gm + 0x2C) : nullptr;
}

static void ResetOwnHeap();
static void *OwnHeap();

// The field's TrialModel factory destructor (FieldRo 0x8754) asserts (= freezes the game) if its
// PokeModelSystem is still running. The game's own scripts always shut it down before the field
// ends, and so must we: before battles we start (g_battlePending), and - for everything else that
// ends the field (trainer battles, map changes, ...) - in FactoryDtorHook, right before the field
// frees the factory.
static void ResetAllNoFree() {        // FieldRo / field heap went away (battle, map change...)
    for (auto &s : g_spawns) s = Spawn();
    g_heap = nullptr; g_factory = nullptr; g_facState = 0;
    ResetOwnHeap();                   // drop our heap (its contents belonged to the old factory)
}

// Heap for our models.
//  * The game's own free field heap (heap id 11, ~4.5 MB) is too small for 2-3 battle models.
//  * The plugin's memory block cannot be used: model textures / vertex buffers must be GPU-visible.
// So we allocate extra LINEAR (FCRAM) memory for the process with svcControlMemory and build a
// gfl2 heap inside it with code.bin 0x1052E8 (what gfl2::heap::Manager::CreateHeap does after
// allocating from a parent).  nngxGetPhysicalAddr is hooked so the GPU can address it.
static u32 g_heapBufAddr = 0, g_heapBufSize = 0;
static int g_modelsFit = 3;           // models that fit into our heap (set at factory init)
static void *g_ownHeap = nullptr;
static u32 g_idleFrames = 0;

static bool AllocLinear() {
    // 12 MB = 5 Pokemon (big patches), 8 MB = 3; Old 3DS mode usually only has 4 MB free (1 Pokemon).
    static const u32 sizes[] = {0xC00000, 0xA00000, 0x800000, 0x700000, 0x600000, 0x500000, 0x400000, 0x300000};
    Log("linear memory free: %lX", (u32)osGetMemRegionFree(MEMREGION_APPLICATION));
    for (u32 sz : sizes) {
        u32 out = 0;
        Result r = svcControlMemory(&out, 0, 0, sz, (MemOp)(MEMOP_ALLOC | MEMOP_LINEAR_FLAG), (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
        Log("linear alloc %X -> res %08lX addr %08lX", sz, (u32)r, out);
        if (R_SUCCEEDED(r) && out) { g_heapBufAddr = out; g_heapBufSize = sz; return true; }
    }
    return false;
}

// The field (and with it the model factory) is destroyed by battles / map changes without our
// models being released, so the old heap still holds their memory: delete it and start fresh.
static void ResetOwnHeap() {
    if (g_ownHeap) {
        Call<void>(O->Heap_DeleteHeap, g_ownHeap);
        g_ownHeap = nullptr;
    }
    // Give the linear memory back to the system while no Pokemon are out (battles, menus, other
    // areas); it is allocated again on the next spawn.
    if (g_heapBufAddr) {
        u32 out = 0;
        Result r = svcControlMemory(&out, g_heapBufAddr, 0, g_heapBufSize, MEMOP_FREE, (MemPerm)0);
        Log("linear block %08lX freed (%08lX)", g_heapBufAddr, (u32)r);
        g_heapBufAddr = 0; g_heapBufSize = 0;
    }
}

static void *OwnHeap() {
    if (!g_ownHeap) {
        // Extra LINEAR memory from the system.  The game only translates addresses inside its own
        // device-memory block for the GPU (nngxGetPhysicalAddr), so that function is hooked to also
        // translate our block (PhysAddrHook below).
        if (!g_heapBufAddr && !AllocLinear()) return nullptr;
        g_ownHeap = Call<void *>(O->Heap_CreateFromBuffer, (void *)g_heapBufAddr, -2, (int)g_heapBufSize, 0, 0, 0);
        Log("own heap %08lX in linear block %08lX size %lX", (u32)g_ownHeap, g_heapBufAddr, g_heapBufSize);
    }
    return g_ownHeap;
}

static bool FactoryReady(u32 fr) {
    (void)fr;
    if (g_facState == 2) return true;
    if (g_facState < 0 || g_facState == 3) return false;
    void *fm = Fieldmap();
    if (!fm) return false;
    if (g_facState == 0) {
        g_factory = *(void **)((u32)fm + 0x124);
        if (!g_factory) return false;
        g_heap = OwnHeap();
        if (!g_heap) { g_facState = -1; return false; }
        // How many Pokemon fit in the memory we actually got (Old 3DS mode has much less free).
        {
            int fit = ((int)g_heapBufSize - g_cfg.systemHeap) / g_cfg.perModelHeap;
            if (fit > kMaxSpawns) fit = kMaxSpawns;
            g_modelsFit = fit;
            if (g_cfg.maxModels < g_modelsFit) g_modelsFit = g_cfg.maxModels;
            if (g_modelsFit < 1) { g_facState = -1; Log("not enough memory for even one model"); return false; }
        }
        Call<void>(fr + O->FR_TMF_Initialize, g_factory, g_heap, g_heap, g_modelsFit, 1);
        g_facState = 1;
        Log("factory %08lX heap %08lX init requested", (u32)g_factory, (u32)g_heap);
        return false;
    }
    if (g_facState == 1) {
        if (!Call<bool>(fr + O->FR_TMF_IsInitialized, g_factory)) return false;
        // Same as FieldRo's CreateHeapPool(fac, heap, 0, 0, 1) but with an explicit per-model heap
        // size (HeapOption+4); the default (3.25 MB per Pokemon) does not fit three Pokemon.
        Call<void>(fr + O->FR_TMF_CreateHeapPool, g_factory, g_heap, 0, 0, 0);
        struct { u8 b[4]; u32 size; u32 pad[2]; } opt = {{0, 0, 0, 0}, (u32)g_cfg.perModelHeap, {0, 0}};
        void *pms = *(void **)g_factory;
        Call<void>(O->PokeModelSystem_CreatePokeModelHeap, pms, g_heap, &opt);
        *(u8 *)((u32)g_factory + 0x3C) = 1;
        g_facState = 2;
        { u32 *vt = *(u32 **)g_heap; Log("factory ready (per-model heap %X) own heap free %lX", g_cfg.perModelHeap, Call<u32>(vt[11], g_heap, 4)); }
        return true;
    }
    return false;
}

static void Despawn(struct Spawn &s);
static void ProcessDying(bool force = false);
// Mirrors ScriptTrialModel::Update states 7-10: delete models, terminate factory, free heap.
// Returns true once everything is released.
static bool FactoryTeardown(u32 fr, bool force = false) {
    if (g_facState == 0 || g_facState < 0) return true;
    if (g_facState == 1) {                       // still initialising: wait until ready, then tear down
        if (!Call<bool>(fr + O->FR_TMF_IsInitialized, g_factory)) return false;
        Call<void>(fr + O->FR_TMF_CreateHeapPool, g_factory, g_heap, 0, 0, 0);
        g_facState = 2;
    }
    if (g_facState == 2) {
        // Like ScriptTrialModel states 7-8: only delete models the game says may be deleted.
        for (auto &sp : g_spawns) if (sp.state != SpawnState::Free && sp.state != SpawnState::Dying) Despawn(sp);
        ProcessDying(force);
        for (auto &sp : g_spawns) if (sp.state != SpawnState::Free) return false;
        Call<void>(fr + O->FR_TMF_TermRequest, g_factory);
        g_facState = 3;
    }
    if (g_facState == 3) {
        if (!Call<bool>(fr + O->FR_TMF_IsTermDone, g_factory)) return false;
        Call<void>(fr + O->FR_TMF_TermFinal, g_factory);
        Log("factory released");
        g_heap = nullptr; g_facState = 0;
        ResetOwnHeap();            // start the next factory in a fresh heap (no fragmentation build-up)
    }
    return true;
}

static void *PokeModelOf(void *trialModel) { return *(void **)((u32)trialModel + 0x20); }

static void ApplyTransform(Spawn &s, float scale) {
    void *pm = PokeModelOf(s.model);
    if (!pm) return;
    float k = g_cfg.modelScale * s.sizeScale * scale;
    if (k < 0.001f) k = 0.001f;
    Call<void>(O->BaseModel_SetPositionF, pm, s.x, s.y, s.z);
    Call<void>(O->BaseModel_SetRotationF, pm, 0.0f, s.yaw, 0.0f, 0);
    Call<void>(O->BaseModel_SetScaleF, pm, k, k, k);
}

// Removing a Pokemon is two-step, like the game's own ScriptTrialModel:
//  1. hide it (Despawn -> state Dying);
//  2. delete it only once its files finished loading AND PokeModel::CanDelete() is true (the GPU
//     no longer uses its buffers).
// v0.2 deleted at once. A model deleted while its files were still loading, or while the GPU still
// drew it, left the async loader / GPU working on freed memory, and a later battle or spawn crashed.
static void Despawn(Spawn &s) {
    if (s.state == SpawnState::Free || s.state == SpawnState::Dying) return;
    if (!s.model) { s = Spawn(); return; }
    u32 fr = Owe::FieldRoBase();
    if (s.shown && fr) Call<void>(fr + O->FR_TM_SetVisible, s.model, false);
    s.state = SpawnState::Dying; s.timer = 0;
}

static void ProcessDying(bool force) {
    u32 fr = Owe::FieldRoBase();
    if (!fr || !g_factory) return;
    for (auto &s : g_spawns) {
        if (s.state != SpawnState::Dying) continue;
        s.timer++;
        if (s.timer < 3) continue;                                         // hidden for 2+ frames
        if (!Call<bool>(fr + O->FR_TM_IsLoaded, s.model) && !force) continue;   // still loading
        void *pm = PokeModelOf(s.model);
        if (pm && !force && !Call<bool>(O->PokeModel_CanDelete, pm)) continue;  // still in use
        Call<void>(fr + O->FR_TMF_DeleteModel, g_factory, s.model);
        if (g_cfg.verboseLog) Log("deleted #%d after %d frames", s.mon.species, s.timer);
        s = Spawn();
    }
}

static bool StartSpawn(Spawn &s, const Rolled &mon, int table, float x, float y, float z) {
    u32 fr = Owe::FieldRoBase();
    SimpleParam sp; memset(&sp, 0, sizeof(sp));
    sp.mons = mon.species; sp.form = mon.form; sp.rare = mon.shiny ? 1 : 0;
    SetupOption so; memset(&so, 0, sizeof(so));
    so.heap = g_heap;
    so.devHeap = Call<void *>(O->Heap_GetHeapByHeapId, 7);
    so.b0 = 2;   // motion pack (SetupOption+0xC): 0 battle, 1 Pokemon Refresh, 2 field (wait/walk/run)
    void *m = Call<void *>(fr + O->FR_TMF_CreateModel, g_factory, g_heap, &sp, &so);
    if (!m) { Log("CreateModel failed sp=%d", mon.species); return false; }
    if (g_cfg.verboseLog) Log("CreateModel -> %08lX", (u32)m);
    s.model = m; s.mon = mon; s.table = table;
    s.x = x; s.y = y; s.z = z; s.yaw = Randf(0, 6.2831f);
    s.state = SpawnState::Loading; s.timer = 0;
    s.life = g_cfg.lifeMinFrames + Rand(g_cfg.lifeMaxFrames - g_cfg.lifeMinFrames + 1);
    Log("spawn #%d lv%d form%d shiny%d tbl%d at %.0f %.0f %.0f", mon.species, mon.level, mon.form, mon.shiny, table, x, y, z);
    return true;
}

// ------------------------------------------------------------------------------------------
// Grass patch analysis: BFS over a sampling grid from the player's cell, through grass cells.
// Only cells connected to the player's patch are candidates, so Pokemon always appear in the
// same patch (and therefore the same encounter table) the player is in.
// ------------------------------------------------------------------------------------------
static const int kGridMax = 2 * 8 + 1;
static u8   g_cellGrass[kGridMax][kGridMax];    // 0 unknown, 1 grass+connected, 2 not
static float g_cellY[kGridMax][kGridMax];
static float g_gridOX, g_gridOZ;
static int   g_gridN = 0, g_patchCells = 0;
static u32   g_gridFrame = 0;

static void ScanPatch(void *root, float px, float py, float pz) {
    int R = g_cfg.gridRadius; if (R > 8) R = 8;
    int N = 2 * R + 1;
    g_gridN = N; g_gridOX = px - R * g_cfg.cellSize; g_gridOZ = pz - R * g_cfg.cellSize;
    memset(g_cellGrass, 0, sizeof(g_cellGrass));
    int qx[kGridMax * kGridMax], qz[kGridMax * kGridMax], qh = 0, qt = 0;
    qx[qt] = R; qz[qt] = R; qt++;
    g_cellGrass[R][R] = 1; g_cellY[R][R] = py;
    int count = 0;
    while (qh < qt) {
        int cx = qx[qh], cz = qz[qh]; qh++;
        count++;
        static const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], nz = cz + dz[k];
            if (nx < 0 || nz < 0 || nx >= N || nz >= N || g_cellGrass[nx][nz]) continue;
            float wx = g_gridOX + nx * g_cfg.cellSize, wz = g_gridOZ + nz * g_cfg.cellSize, y; u32 a;
            if (ProbeGround(root, wx, g_cellY[cx][cz], wz, y, a) && IsGrassAttr(a) && fabsf(y - g_cellY[cx][cz]) < 60.0f) {
                g_cellGrass[nx][nz] = 1; g_cellY[nx][nz] = y; qx[qt] = nx; qz[qt] = nz; qt++;
            } else g_cellGrass[nx][nz] = 2;
        }
    }
    g_patchCells = count;
    g_gridFrame = g_frame;
}

static bool PickSpawnPoint(float px, float pz, float &ox, float &oy, float &oz) {
    for (int tries = 0; tries < 24; tries++) {
        int cx = Rand(g_gridN), cz = Rand(g_gridN);
        if (g_cellGrass[cx][cz] != 1) continue;
        float x = g_gridOX + cx * g_cfg.cellSize + Randf(-20, 20);
        float z = g_gridOZ + cz * g_cfg.cellSize + Randf(-20, 20);
        float d = sqrtf((x - px) * (x - px) + (z - pz) * (z - pz));
        if (d < g_cfg.spawnMinDist) continue;
        bool clash = false;
        for (auto &s : g_spawns)
            if (s.state != SpawnState::Free && (s.x - x) * (s.x - x) + (s.z - z) * (s.z - z) < 90.0f * 90.0f) clash = true;
        if (clash) continue;
        ox = x; oy = g_cellY[cx][cz]; oz = z;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------
// Cries (Sound::PlayVoice, like the Poke Finder). Mostly tied to what the Pokemon is doing -
// popping out, stopping after a walk, looking around - plus an occasional random call. Quieter
// with distance, slightly varied pitch, never on top of another cry.
// ------------------------------------------------------------------------------------------
enum class CryEvent : u8 { Appear, Stop, Look, Ambient };
static u32 g_lastCryFrame = 0;
static void PlayCry(const Spawn &s, float dist) {
    u32 channels = *(volatile u32 *)(O->Sound_VoiceTable + 4);   // Sound asserts on a bad channel
    if (channels == 0) return;
    if (g_lastCryFrame && !Call<bool>(O->Sound_IsVoiceFinished, (u8)0)) return;   // still playing
    float vol = 1.0f - dist / g_cfg.cryMaxDist;
    if (vol < 0.2f) vol = 0.2f;
    vol *= g_cfg.cryVolume;
    float pitch = Randf(1.0f - g_cfg.cryPitchVar, 1.0f + g_cfg.cryPitchVar);
    Call<void>(O->Sound_PlayVoice, (u8)0, (u32)s.mon.species, (u8)s.mon.form, 0u, 0, 0u);
    Call<void>(O->Sound_ChangeVoiceVolume, (u8)0, vol, 0u);
    Call<void>(O->Sound_ChangeVoicePitch, (u8)0, pitch);
    g_lastCryFrame = g_frame;
    if (g_cfg.verboseLog) Log("cry #%d vol %.2f pitch %.2f", s.mon.species, vol, pitch);
}
static void MaybeCry(const Spawn &s, float dist, CryEvent ev) {
    if (!g_cfg.cries || dist > g_cfg.cryMaxDist) return;
    if (g_lastCryFrame && g_frame - g_lastCryFrame < (u32)g_cfg.cryCooldownFrames) return;
    int oneIn = 0;
    switch (ev) {
    case CryEvent::Appear:  oneIn = g_cfg.cryOnAppear; break;
    case CryEvent::Stop:    oneIn = g_cfg.cryOnStop; break;
    case CryEvent::Look:    oneIn = g_cfg.cryOnLook; break;
    case CryEvent::Ambient: oneIn = 30 * g_cfg.cryAvgSeconds; break;   // checked every frame
    }
    if (oneIn > 0 && Rand(oneIn) == 0) PlayCry(s, dist);
}

// ------------------------------------------------------------------------------------------
// Wandering: 3 steps, stop, look around, 3 steps... (only inside the grass of this patch)
// Motions come from the species' field ("Poke Finder") pack: 0 = wait, 2 = walk.
// ------------------------------------------------------------------------------------------
static const int kAnimWait = 0, kAnimWalk = 2;

static float AngleDiff(float a, float b) {          // shortest signed a - b
    float d = a - b;
    while (d > 3.14159f) d -= 6.28318f;
    while (d < -3.14159f) d += 6.28318f;
    return d;
}

static bool GroundOk(void *root, float x, float yRef, float z, float &y) {
    u32 a;
    return ProbeGround(root, x, yRef, z, y, a) && IsGrassAttr(a) && fabsf(y - yRef) < 40.0f;
}

static bool PickWalkTarget(Spawn &s, void *root, float px, float pz) {
    for (int tries = 0; tries < 8; tries++) {
        float ang = (tries < 3) ? s.yaw + Randf(-1.0f, 1.0f) : Randf(0, 6.2831f);   // prefer ahead
        float dist = Randf(g_cfg.walkDistMin, g_cfg.walkDistMax);
        // the model faces +Z at yaw 0
        float tx = s.x + sinf(ang) * dist, tz = s.z + cosf(ang) * dist, ym, yt;
        // the whole path (not just its end) must stay walkKeepAway from the player: Pokemon never
        // walk into you by themselves, you go to them
        {
            float vx = tx - s.x, vz = tz - s.z, L2 = vx * vx + vz * vz;
            float t = L2 > 0 ? ((px - s.x) * vx + (pz - s.z) * vz) / L2 : 0;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            float cx = s.x + vx * t - px, cz = s.z + vz * t - pz;
            if (cx * cx + cz * cz < g_cfg.walkKeepAway * g_cfg.walkKeepAway) continue;
        }
        bool clash = false;
        for (auto &o : g_spawns)
            if (&o != &s && o.state != SpawnState::Free && (o.x - tx) * (o.x - tx) + (o.z - tz) * (o.z - tz) < 90.0f * 90.0f) clash = true;
        if (clash) continue;
        if (!GroundOk(root, (s.x + tx) * 0.5f, s.y, (s.z + tz) * 0.5f, ym)) continue;
        if (!GroundOk(root, tx, ym, tz, yt)) continue;
        s.tx = tx; s.ty = yt; s.tz = tz; s.tyaw = ang;
        return true;
    }
    return false;
}

static void UpdateWander(Spawn &s, void *root, float px, float pz) {
    void *pm = PokeModelOf(s.model);
    // turn smoothly towards tyaw (used by Pause "look around" and Turn)
    float d = AngleDiff(s.tyaw, s.yaw);
    if (fabsf(d) > 0.01f) s.yaw += (d > 0 ? 1 : -1) * fminf(fabsf(d), g_cfg.turnSpeed);
    switch (s.ws) {
    case Wander::Pause:
        if (--s.wt > 0) break;
        if (s.looks > 0) {                                  // look around: a turn of 30-75 degrees
            s.looks--;
            MaybeCry(s, sqrtf((s.x - px) * (s.x - px) + (s.z - pz) * (s.z - pz)), CryEvent::Look);
            s.tyaw = s.yaw + (Rand(2) ? 1 : -1) * Randf(0.5f, 1.3f);
            s.wt = 25 + Rand(35);
        } else if (s.canWalk && root && PickWalkTarget(s, root, px, pz)) {
            s.ws = Wander::Turn;
            if (g_cfg.verboseLog) Log("#%d walks %.0f,%.0f -> %.0f,%.0f", s.mon.species, s.x, s.z, s.tx, s.tz);
        } else {
            s.wt = 30 + Rand(60); s.looks = Rand(2);
        }
        break;
    case Wander::Turn:
        if (fabsf(AngleDiff(s.tyaw, s.yaw)) < 0.05f) {
            s.ws = Wander::Walk; s.walkFrames = 0;
            if (pm) Call<void>(O->PokeModel_ChangeAnimation, pm, kAnimWalk, false);
        }
        break;
    case Wander::Walk: {
        float dx = s.tx - s.x, dz = s.tz - s.z, dist = sqrtf(dx * dx + dz * dz);
        s.walkFrames++;
        // the player may walk up to it mid-walk: stop well outside the touch range
        float stopAt = g_cfg.touchDist * fmaxf(1.0f, g_cfg.modelScale * s.sizeScale) + 30.0f;
        bool tooClose = (s.x - px) * (s.x - px) + (s.z - pz) * (s.z - pz) < stopAt * stopAt;
        if (dist <= g_cfg.walkSpeed || s.walkFrames > 30 * 6 || tooClose) {
            if (dist <= g_cfg.walkSpeed) { s.x = s.tx; s.z = s.tz; s.y = s.ty; }
            s.ws = Wander::Pause; s.wt = g_cfg.pauseMinFrames + Rand(g_cfg.pauseMaxFrames - g_cfg.pauseMinFrames + 1);
            s.looks = 1 + Rand(2);
            if (pm) Call<void>(O->PokeModel_ChangeAnimation, pm, kAnimWait, false);
            MaybeCry(s, sqrtf((s.x - px) * (s.x - px) + (s.z - pz) * (s.z - pz)), CryEvent::Stop);
            break;
        }
        s.tyaw = atan2f(dx, dz);
        s.x += dx / dist * g_cfg.walkSpeed; s.z += dz / dist * g_cfg.walkSpeed;
        if ((s.walkFrames % 4) == 0 && root) { float y; u32 a; if (ProbeGround(root, s.x, s.y, s.z, y, a) && fabsf(y - s.y) < 40.0f) s.y = y; }
        break;
    }
    }
    ApplyTransform(s, 1.0f);
}

// ------------------------------------------------------------------------------------------
// Battle hand-off
// ------------------------------------------------------------------------------------------
static bool   g_forcing = false;        // inside our forced EncountCheck call
static Rolled g_forceMon{};
static bool   g_shinyPending = false;   // apply forced shiny/non-shiny to the next wild InitialSpec
static bool   g_shinyValue = false;
static u32    g_shinyFrame = 0;

static Hook g_hkCheck, g_hkFrame, g_hkSetupMons, g_hkSpec, g_hkPhys, g_hkIsRare;
static bool IsRareHook(void *core);
// nngxGetPhysicalAddr(va): the game's VA->PA for GPU buffers only knows its own device block.
static u32 PhysAddrHook(u32 va) {
    u32 pa = HookContext::GetCurrent().OriginalFunction<u32>(va);
    if (pa == 0 && g_heapBufAddr && va >= g_heapBufAddr && va < g_heapBufAddr + g_heapBufSize)
        // Same rule as the game's own nngxGetPhysicalAddr (code.bin 0x27A058):
        //   New-3DS linear heap  VA 0x30000000+ -> PA VA - 0x10000000
        //   Old-3DS linear heap  VA 0x14000000+ -> PA VA + 0x0C000000
        // (Old 3DS mode is the default on some emulator builds, e.g. the Switch fork Dekopon.)
        pa = (va >= 0x30000000) ? va - 0x10000000 : va + 0x0C000000;
    return pa;
}

static u32 EncountCheckHook(void *self, u32 force, u32 idx, u32 attr) {
    g_encSelf = self;
    if (idx < 0x100) { g_curTable = (int)idx; g_curTableFrame = g_frame; g_dbg.lastTable = (int)idx; }
    if (!g_cfg.enabled) return HookContext::GetCurrent().OriginalFunction<u32>(self, force, idx, attr);
    if (g_forcing) return HookContext::GetCurrent().OriginalFunction<u32>(self, 1u, idx, attr);
    return 0;   // random grass encounters are replaced by visible Pokemon
}

// PokeSet "setup one Pokemon from a slot" (v1.2 0x3A8584): entry+0 species, +4 level, +5 form.
static void SetupMonsHook(void *pokeSet, u8 *entry, u32 *maxLv, u32 *slot, u32 a4, u32 a5, u32 a6, u32 a7) {
    HookContext::GetCurrent().OriginalFunction<void>(pokeSet, entry, maxLv, slot, a4, a5, a6, a7);
    if (!g_forcing) return;
    *(u16 *)entry = g_forceMon.species;
    entry[4] = g_forceMon.level;
    entry[5] = g_forceMon.form;
    if (*maxLv < g_forceMon.level) *maxLv = g_forceMon.level;
}

// Shininess of the battle Pokemon.
// Findings (v1.2): the wild Pokemon is built from a pml InitialSpec whose "rare mode" (spec+8,
// -1:3 = random with N tries) is normalised by code.bin 0x320C4C; the PokemonParam is created
// right after, and the game immediately calls CoreParam::IsRare() (0x3239F4) on it.  Its ID is
// the player's TID/SID (core data +4) and its PID is core data +0x10; shiny iff
// (ID.hi ^ ID.lo ^ PID.hi ^ PID.lo) < 16.  We therefore patch the PID of that Pokemon so its shiny
// state matches what the overworld model showed.
static bool g_patchNextRare = false;

static void SpecHook(void *dst, u32 *spec) {
    bool wild = g_shinyPending && (g_frame - g_shinyFrame) < 30 * 30 && spec[2] == 0xFFFFFFFF && spec[3] == 3;
    // spec+0x23 = number of shiny rolls the game wants (1, or 3 with the Shiny Charm); remember it
    // so our own overworld rolls use the same odds.
    if (wild) { u8 tries = ((u8 *)spec)[0x23]; if (tries >= 1 && tries <= 16) g_shinyRolls = tries; }
    HookContext::GetCurrent().OriginalFunction<void>(dst, spec);
    if (wild) { g_shinyPending = false; g_patchNextRare = true; }
}

static bool IsRareHook(void *core) {
    if (g_patchNextRare) {
        g_patchNextRare = false;
        void *d = *(void **)((u32)core + 0xC);
        Call<void>(O->Core_BeginAccess, d);
        u32 *blk = Call<u32 *>(O->Core_GetBlockA, d, 1);
        u32 id = blk[1], pid = blk[4];
        u32 tsv = (id >> 16) ^ (id & 0xFFFF);
        u32 cur = tsv ^ (pid >> 16) ^ (pid & 0xFFFF);
        if (g_shinyValue && cur >= 16)        pid = (((pid & 0xFFFF) ^ tsv ^ Rand(8)) << 16) | (pid & 0xFFFF);
        else if (!g_shinyValue && cur < 16)   pid ^= 0x10000000;
        blk[4] = pid;
        Call<void>(O->Core_EndAccess, d);
        Log("wild PID set to %08lX (id %08lX) for shiny=%d", pid, id, g_shinyValue);
    }
    return HookContext::GetCurrent().OriginalFunction<bool>(core);
}

static bool TriggerBattle(const Rolled &mon, int table) {
    u32 fr = Owe::FieldRoBase();
    if (!g_encSelf || !fr) return false;
    g_forceMon = mon;
    g_forcing = true;
    g_shinyPending = true; g_shinyValue = mon.shiny; g_shinyFrame = g_frame;
    u32 ok = Call<u32>(fr + O->FR_EncountCheck, g_encSelf, 1u, (u32)table, 0xFFFFu);
    g_forcing = false;
    Log("battle trigger #%d lv%d -> %lu", mon.species, mon.level, ok);
    if (!ok) g_shinyPending = false;
    return ok != 0;
}

// Touching a Pokemon: hide all of them, wait until every model has been safely deleted (usually a
// few frames; up to ~1 s if one is still loading), then start the battle.
static bool   g_battlePending = false;
static Rolled g_battleMon{};
static int    g_battleTable = -1;
static u32    g_battleFrame = 0;

// ------------------------------------------------------------------------------------------
// Per-frame update (runs inside the field's per-frame event check)
// ------------------------------------------------------------------------------------------
static void Update(void *evCheck) {
    g_frame++;
    g_evFieldmap = *(u32 *)((u32)evCheck + 0x10);
    u32 fr = Owe::FieldRoBase();
    if (!fr) return;
    if (g_facGen != g_fieldGen) {     // new field instance: everything from the old one is gone
        ResetAllNoFree();
        g_facGen = g_fieldGen;
    } else if (g_facState != 0 && g_factory) {
        void *fm = Fieldmap();
        if (!fm || *(void **)((u32)fm + 0x124) != g_factory) { Log("field rebuilt under us"); ResetAllNoFree(); }
    }
    int active = 0;
    for (auto &s : g_spawns) if (s.state != SpawnState::Free) active++;
    g_dbg.active = active;

    if (g_facState == 2) ProcessDying();
    if (g_battlePending) {
        // Shut the whole model system down first (models, then PokeModelSystem + its archives),
        // exactly as the game's scripts do before a Totem battle.
        bool done = g_cfg.debugSkipTeardown ? true : FactoryTeardown(fr);
        if (done || g_frame - g_battleFrame > 30 * 5) {
            if (!done) Log("battle: model system still busy after 5 s, starting anyway");
            g_battlePending = false;
            TriggerBattle(g_battleMon, g_battleTable);
        }
        return;
    }
    if (!g_cfg.enabled) { for (auto &s : g_spawns) Despawn(s); return; }

    void *player = *(void **)((u32)evCheck + 0x14);
    if (!player) return;
    void *pmodel = *(void **)((u32)player + 0xF8);
    if (!pmodel) return;
    const Vec3 *pp = Call<const Vec3 *>(O->BaseModel_GetPosition, pmodel);
    float px = pp->x, py = pp->y, pz = pp->z;
    g_dbg.px = px; g_dbg.py = py; g_dbg.pz = pz;
    void *root = *(void **)((u32)player + 0x3A8);
    u32 pattr = *(u32 *)((u32)player + 0x314);
    g_dbg.lastAttr = pattr;
    bool inGrass = IsGrassAttr(pattr);
    g_dbg.playerInGrass = inGrass;
    if (g_cfg.verboseLog && (g_frame % 30) == 0) {
        for (int i = 0; i < kMaxSpawns; i++) if (g_spawns[i].state != SpawnState::Free) {
            Spawn &q = g_spawns[i]; float dd = sqrtf((q.x - px) * (q.x - px) + (q.z - pz) * (q.z - pz));
            Log("  s%d #%d st%d t%d/%d d=%.0f", i, q.mon.species, (int)q.state, q.timer, q.life, dd);
        }
    }
    if (g_cfg.verboseLog && (g_frame % 90) == 0)
        Log("f%lu pos %.0f %.0f %.0f attr %08lX grass %d tbl %d (age %lu) fac %d act %d patch %d",
            g_frame, px, py, pz, pattr, inGrass, g_curTable, g_frame - g_curTableFrame, g_facState, active, g_patchCells);

    // Release our model heap when nothing is spawned for a while, or when the field was paused
    // (menus, cutscenes, battles) so the game gets its memory back.
    static u64 lastTick = 0;
    u64 now = svcGetSystemTick();
    bool resumed = lastTick && (now - lastTick) > 268111856ULL * 2;   // > 2 s since last field frame
    if (resumed) Log("field resumed after %llu ms - releasing models", (now - lastTick) / 268111ULL);
    lastTick = now;
    g_idleFrames = active ? 0 : g_idleFrames + 1;
    static bool releasing = false;
    if (resumed || g_facState == 3 || (g_facState == 2 && !active && g_idleFrames > (u32)g_cfg.idleReleaseFrames) || releasing) {
        releasing = !FactoryTeardown(fr);
        return;
    }

    // The game only reports the patch's table when the player takes a step. Standing still in the
    // same grass keeps it valid, so Pokemon keep appearing around an idle player.
    static u32 tblSeenFrame = 0; static float tblX = 0, tblZ = 0;
    if (g_curTableFrame != tblSeenFrame) { tblSeenFrame = g_curTableFrame; tblX = px; tblZ = pz; }
    else if (inGrass && g_curTable >= 0 && (px - tblX) * (px - tblX) + (pz - tblZ) * (pz - tblZ) < 50.0f * 50.0f)
        { g_curTableFrame = g_frame; tblSeenFrame = g_frame; }
    bool canSpawn = inGrass && g_curTable >= 0 && (g_frame - g_curTableFrame) < 30 * 60;
    if (!(canSpawn || g_cfg.debugForceSpawn) && !active) return;   // no need for the heap yet
    if (!FactoryReady(fr)) return;

    // -- update existing spawns
    for (int i = 0; i < kMaxSpawns; i++) {
        Spawn &s = g_spawns[i];
        if (s.state == SpawnState::Free || s.state == SpawnState::Dying) continue;
        float dx = s.x - px, dz = s.z - pz, d2 = dx * dx + dz * dz;
        if (d2 > g_cfg.despawnDist * g_cfg.despawnDist) { Despawn(s); continue; }
        s.timer++;
        switch (s.state) {
        case SpawnState::Loading:
            if (Call<bool>(fr + O->FR_TM_IsLoaded, s.model)) {
                if (g_cfg.verboseLog) Log("loaded after %d frames; setup", s.timer);
                Call<void>(fr + O->FR_TM_Setup, s.model);
                if (g_cfg.verboseLog) Log("setup ok pm=%08lX", (u32)PokeModelOf(s.model));
                if (g_cfg.verboseLog) {
                    u32 pm = (u32)PokeModelOf(s.model);
                    int cnt = *(int *)(pm + 0x12ac); u32 *arr = *(u32 **)(pm + 0x12b0);
                    char b[200]; int n = 0;
                    for (int k = 0; k < cnt && k < 90 && n < 190; k++) if (arr[k]) n += snprintf(b + n, 200 - n, "%X ", k);
                    Log("motions cnt=%d cur=%d avail: %s", cnt, (int)(s8)*(u32 *)(pm + 0x12b8), b);
                }
                Call<void>(fr + O->FR_TM_SetAmbient, s.model, 1);
                if (g_cfg.verboseLog) Log("ambient ok");
                if (void *pm = PokeModelOf(s.model)) {
                    Call<void>(O->PokeModel_ChangeAnimation, pm, kAnimWait, false);
                    s.canWalk = g_cfg.wander && Call<bool>(O->PokeModel_IsAvailableAnimation, pm, kAnimWalk);
                    // Size: bounding box of the wait motion (gfl2::math::AABB = min xyzw, max xyzw).
                    float bb[8] = {0};
                    Call<void>(O->PokeModel_GetMotionCollision, pm, bb);
                    float h = bb[5] - bb[1], w = fmaxf(bb[4] - bb[0], bb[6] - bb[2]), size = fmaxf(h, w);
                    s.sizeScale = 1.0f;
                    if (size > 1.0f && size < g_cfg.smallSize) s.sizeScale = fminf(g_cfg.smallSize / size, g_cfg.maxSmallBoost);
                    if (g_cfg.verboseLog) Log("#%d size h%.0f w%.0f -> boost %.2f", s.mon.species, h, w, s.sizeScale);
                }
                ApplyTransform(s, 0.01f);
                if (g_cfg.verboseLog) Log("transform ok");
                Call<void>(fr + O->FR_TM_SetVisible, s.model, true);
                s.shown = true;
                s.ws = Wander::Pause; s.wt = 20 + Rand(40); s.looks = Rand(2); s.tyaw = s.yaw;
                MaybeCry(s, sqrtf(d2), CryEvent::Appear);

                s.state = SpawnState::Appear; s.timer = 0;
            } else if (s.timer > 30 * 10) { Log("load timeout"); Despawn(s); }
            break;
        case SpawnState::Appear:
            ApplyTransform(s, (float)s.timer / g_cfg.popFrames);
            if (s.timer >= g_cfg.popFrames) { s.state = SpawnState::Idle; s.timer = 0; ApplyTransform(s, 1.0f); }
            break;
        case SpawnState::Idle:
            UpdateWander(s, root, px, pz);
            MaybeCry(s, sqrtf(d2), CryEvent::Ambient);
            if (s.timer >= s.life && s.ws != Wander::Walk) { s.state = SpawnState::Vanish; s.timer = 0; }
            break;
        case SpawnState::Vanish:
            ApplyTransform(s, 1.0f - (float)s.timer / g_cfg.popFrames);
            if (s.timer >= g_cfg.popFrames) { Log("vanished #%d", s.mon.species); Despawn(s); }
            break;
        default: break;
        }
        dx = s.x - px; dz = s.z - pz; d2 = dx * dx + dz * dz;
        bool anyLoading = false; for (auto &o : g_spawns) if (o.state == SpawnState::Loading) anyLoading = true;
        bool autoB = g_cfg.debugAutoBattle && s.state == SpawnState::Idle && s.timer > 20 && (anyLoading || s.timer > 150);
        float touch = g_cfg.touchDist * fmaxf(1.0f, g_cfg.modelScale * s.sizeScale);   // bigger model, bigger reach
        if (autoB || ((s.state == SpawnState::Idle || s.state == SpawnState::Appear) && d2 < touch * touch)) {
            g_battleMon = s.mon; g_battleTable = s.table; g_battleFrame = g_frame; g_battlePending = true;
            Log("touched #%d%s", s.mon.species, anyLoading ? " (another one is still loading)" : "");
            for (auto &o : g_spawns) Despawn(o);
            return;
        }
    }

    // -- spawn new ones (only for the patch the player is standing in)
    static u32 nextSpawnFrame = 0;
    bool wantScan = inGrass && g_curTable >= 0 && (g_frame - g_curTableFrame) < 30 * 60;
    if (g_cfg.debugForceSpawn) { wantScan = g_curTable >= 0; }
    if (!wantScan) return;
    if (g_frame - g_gridFrame > 45) ScanPatch(root, px, py, pz);
    g_dbg.patchCells = g_patchCells;
    // How many Pokemon this patch shows: small patches 1-2, larger ones 2-3 (re-rolled when the
    // player enters a different patch/table), capped by the memory budget (Config::maxModels).
    // Patch sizes: small 1-2, medium 2-3, large 3-5 (re-rolled when the player enters another patch).
    static int patchTarget = 0, patchTable = -1, patchSize = -1;
    int size = g_patchCells < g_cfg.smallPatchCells ? 0 : g_patchCells < g_cfg.largePatchCells ? 1 : 2;
    if (patchTable != g_curTable || patchSize != size) {
        patchTable = g_curTable; patchSize = size;
        if (size == 0)      patchTarget = 1 + Rand(g_cfg.maxPerPatchSmall);
        else if (size == 1) patchTarget = 2 + Rand(g_cfg.maxPerPatchMedium - 1);
        else                patchTarget = 3 + Rand(g_cfg.maxPerPatchLarge - 2);
    }
    int target = patchTarget;
    if (target > g_cfg.maxModels) target = g_cfg.maxModels;      // menu: "up to 3" performance option
    if (target > g_modelsFit) target = g_modelsFit;
    if (target > kMaxSpawns) target = kMaxSpawns;
    if (g_patchCells < 3) target = 0;
    bool loadingNow = false;
    for (auto &o : g_spawns) if (o.state == SpawnState::Loading) loadingNow = true;
    if (!loadingNow && ((active < target && g_frame >= nextSpawnFrame) || g_cfg.debugForceSpawn)) {
        for (auto &s : g_spawns) {
            if (s.state != SpawnState::Free) continue;
            Rolled mon; float x, y, z;
            if (!RollFromTable(g_curTable, mon)) { snprintf(g_dbg.msg, sizeof(g_dbg.msg), "roll failed tbl %d", g_curTable); break; }
            if (!PickSpawnPoint(px, pz, x, y, z)) { snprintf(g_dbg.msg, sizeof(g_dbg.msg), "no spawn point"); break; }
            if (g_cfg.debugNearSpawn) { x = px + (&s - g_spawns) * 200.0f; z = pz - 150.0f; y = py; }
            StartSpawn(s, mon, g_curTable, x, y, z);
            snprintf(g_dbg.msg, sizeof(g_dbg.msg), "spawned #%d lv%d%s", mon.species, mon.level, mon.shiny ? " *" : "");
            break;
        }
        nextSpawnFrame = g_frame + g_cfg.respawnDelay + Rand(60);
        g_cfg.debugForceSpawn = false;
    }
}

int g_debugStage = 1;   // 0 = hook passthrough only (bring-up), 1 = full update
static u32 FrameHook(void *evCheck) {
    u32 r = HookContext::GetCurrent().OriginalFunction<u32>(evCheck);
    if (g_debugStage >= 1) Update(evCheck);
    return r;
}

// ------------------------------------------------------------------------------------------
// FieldRo hooks must be (re)installed whenever FieldRo is (re)loaded at a new address.
// A plugin-thread watcher does that; the hooks themselves run on the game thread.
// ------------------------------------------------------------------------------------------
// Runs on the game thread right before the field deletes the TrialModel factory.
static Hook g_hkFacDtor;
static u32 FactoryDtorHook(void *fac) {
    if (fac && fac == g_factory && g_facState != 0) {
        u32 fr = Owe::FieldRoBase();
        Log("field closing with our models loaded: shutting them down first");
        int i = 0;
        for (; i < 240 && fr; i++) {                       // up to ~4 s, normally a few ms
            if (FactoryTeardown(fr, i > 90)) break;         // after ~1.5 s stop waiting for the GPU/loader
            svcSleepThread(16666667LL);
        }
        Log("model system shut down (%d waits, state %d)", i, g_facState);
    }
    return HookContext::GetCurrent().OriginalFunction<u32>(fac);
}

static u32 g_hookedBase = 0;
static Mutex g_hookMutex;
static Hook g_hkDispose;

// Removes our FieldRo hooks while FieldRo is still mapped (called right before RO unloads it).
static void UnhookFieldRo() {
    if (!g_hookedBase) return;
    g_hkCheck.Disable(); g_hkFrame.Disable(); g_hkFacDtor.Disable();
    Log("FieldRo %08lX unloading - hooks removed", g_hookedBase);
    g_hookedBase = 0;
}

// gfl2::ro::RoManager::DisposeModule(this, nn::ro::Module*): FieldRo is unloaded for battles etc.
// The CTRPF hooks must be removed before that, otherwise disabling them later would write into
// unmapped (or re-used) memory.
static void DisposeModuleHook(void *self, void *module) {
    {
        Lock lock(g_hookMutex);
        u32 m = (u32)module;
        if (g_hookedBase && m == g_hookedBase) UnhookFieldRo();   // Module* == CRO image base
    }
    HookContext::GetCurrent().OriginalFunction<void>(self, module);
}

static void InstallFieldRoHooks() {
    Lock lock(g_hookMutex);
    u32 fr = Owe::FieldRoBase();
    if (fr == g_hookedBase) return;
    if (g_hookedBase) UnhookFieldRo();   // moved while still mapped (should not happen)
    if (!fr) return;
    g_hkCheck = Hook(); g_hkFrame = Hook(); g_hkFacDtor = Hook();
    g_hkCheck.InitializeForMitm(fr + O->FR_EncountCheck, (u32)EncountCheckHook);
    g_hkFrame.InitializeForMitm(fr + O->FR_EventCheckFrame, (u32)FrameHook);
    g_hkFacDtor.InitializeForMitm(fr + O->FR_TMF_Destructor, (u32)FactoryDtorHook);
    HookResult a = g_hkCheck.Enable(), b = g_hkFrame.Enable(), c = g_hkFacDtor.Enable();
    Log("FieldRo at %08lX hooks check=%d frame=%d dtor=%d", fr, (int)a, (int)b, (int)c);
    g_hookedBase = fr;
    g_fieldGen++;
}

void Owe::Poll() { InstallFieldRoHooks(); }
static void Watcher(void *) {   // plugin thread: (re)hook FieldRo whenever it is (re)loaded
    while (true) {
        InstallFieldRoHooks();
        svcSleepThread(250 * 1000 * 1000LL);
    }
}

bool Owe::Init(u64 tid, u16 ver) {
    u32 low = (u32)tid;
    if (low != 0x001B5100) return false;   // Ultra Moon only (Ultra Sun code.bin not verified yet)
    if (ver == 2080 || ver == 2) O = &kUSUM_1_2;   // GetVersion() reports 2 for the 1.2 update under Azahar
    else return false;     // v1.0 table not wired yet (offsets documented in RE_NOTES.md)
    g_rng ^= (u32)svcGetSystemTick();
    g_hkSetupMons.InitializeForMitm(O->PokeSet_SetupMons, (u32)SetupMonsHook);
    g_hkSpec.InitializeForMitm(O->InitialSpec_Normalize, (u32)SpecHook);
    g_hkPhys.InitializeForMitm(O->nngxGetPhysicalAddr, (u32)PhysAddrHook);
    g_hkIsRare.InitializeForMitm(O->Core_IsRare, (u32)IsRareHook);
    Log("israre hook=%d", (int)g_hkIsRare.Enable());
    g_hkDispose.InitializeForMitm(O->RoManager_DisposeModule, (u32)DisposeModuleHook);
    Log("dispose hook=%d", (int)g_hkDispose.Enable());
    Log("static hooks setupmons=%d spec=%d phys=%d", (int)g_hkSetupMons.Enable(), (int)g_hkSpec.Enable(), (int)g_hkPhys.Enable());
    static ThreadEx watcher(Watcher, 0x1000, 0x30, -1);
    watcher.Start(nullptr);
    return true;
}

void Owe::Shutdown() {}

} // namespace OWE
