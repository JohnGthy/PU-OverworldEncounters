#pragma once
#include <3ds/types.h>

namespace OWE {

struct Config {
    bool  enabled         = true;
    bool  debugForceSpawn = false;
    bool  verboseLog      = false;   // detailed per-frame logging to owe_log.txt
    bool  debugNearSpawn  = false;   // testing aid: spawn right in front of the player
    bool  debugAlwaysShiny = false;  // testing aid: every spawned Pokemon is shiny
    bool  debugAutoBattle  = false;  // testing aid: start battles by itself (stress test)
    bool  debugSkipTeardown = false; // testing aid: start battles with the model system still running
    int   maxPerPatchSmall = 2;      // small patches: 1-2 Pokemon
    int   maxPerPatchMedium = 3;     // medium patches: 2-3 Pokemon
    int   maxPerPatchLarge = 5;      // large patches: 3-5 Pokemon
    int   smallPatchCells  = 24;     // sampled grass cells below which a patch is small
    int   largePatchCells  = 60;     // ... and from which it is large (the grid has 13x13 = 169 cells)
    float cellSize         = 75.0f;  // grass sampling grid spacing (field units)
    int   gridRadius       = 6;      // sampling grid half-size in cells (13x13 grid around the player)
    float spawnMinDist     = 150.0f; // never pop out right on top of the player
    float despawnDist      = 1500.0f;
    float touchDist        = 60.0f;  // player <-> Pokemon distance that starts the battle
    bool  cries            = true;   // Pokemon cry now and then
    int   cryOnAppear      = 3;      // 1-in-N chance to cry when popping out,
    int   cryOnStop        = 6;      // ...when stopping after a walk,
    int   cryOnLook        = 10;     // ...when looking around,
    int   cryAvgSeconds    = 40;     // ...plus about once every N seconds per Pokemon at random
    int   cryCooldownFrames = 30 * 7; // at least 7 s between two cries (and never over one)
    float cryMaxDist       = 900.0f; // silent beyond this distance; quieter with distance
    float cryPitchVar      = 0.05f;  // pitch varies by +-5%
    float cryVolume        = 0.8f;
    bool  wander           = true;   // Pokemon walk a few steps, stop and look around
    float walkSpeed        = 1.6f;   // field units per frame (the player walks ~3.8)
    float walkDistMin      = 60.0f;  // one walk = "about 3 steps"
    float walkDistMax      = 110.0f;
    float walkKeepAway     = 90.0f;  // never walk towards the player closer than this
    float turnSpeed        = 0.12f;  // radians per frame
    int   pauseMinFrames   = 30 * 2; // standing still between walks (looking around)
    int   pauseMaxFrames   = 30 * 4;
    float modelScale       = 1.15f;  // overall model scale in the field (battle size = 1.0)
    float smallSize        = 80.0f;  // Pokemon smaller than this (model units) are scaled up...
    float maxSmallBoost    = 1.6f;   // ...by up to this factor
    int   lifeMinFrames    = 30 * 12;
    int   lifeMaxFrames    = 30 * 22;
    int   popFrames        = 15;     // grow-in / shrink-out duration
    int   respawnDelay     = 30 * 2;
    int   maxModels        = 5;        // simultaneous Pokemon (menu: 5 or 3; also limited by memory)
    int   perModelHeap     = 0x240000; // memory per Pokemon model (very large species may not fit)
    int   systemHeap       = 0x90000;  // PokeModelSystem bookkeeping inside our heap
    int   idleReleaseFrames = 30 * 5;  // release the heap after this long with nothing spawned
};

struct Debug {
    int   lastTable = -1;
    int   playerInGrass = 0;
    int   active = 0;
    int   patchCells = 0;
    float px = 0, py = 0, pz = 0;
    u32   lastAttr = 0;
    char  msg[80] = "";
};

namespace Owe {
    bool    Init(u64 titleId, u16 version);
    void    Shutdown();
    void    Poll();
    u32     FieldRoBase();
    Config &Cfg();
    Debug  &Dbg();
}

void Log(const char *fmt, ...);

} // namespace OWE

namespace CTRPluginFramework { using OWE::Log; }
