// Overworld Encounters (OWE) - SwSh-style visible wild Pokemon for Pokemon Ultra Sun / Ultra Moon.
// See docs/DESIGN.md for the architecture and docs/RE_NOTES.md for how every address was found.
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include "GameOffsets.hpp"
#include "Owe.hpp"

namespace OWE { static CTRPluginFramework::File *g_log = nullptr; }
namespace CTRPluginFramework {
using namespace OWE;

// ---------------------------------------------------------------------------------------------
// Logging (sdmc:/luma/plugins/<tid>/owe_log.txt) - kept small; essential for remote debugging.
// ---------------------------------------------------------------------------------------------
} namespace OWE {
void Log(const char *fmt, ...) {
    if (!g_log) return;
    char buf[256];
    va_list va; va_start(va, fmt); int n = vsnprintf(buf, sizeof(buf) - 2, fmt, va); va_end(va);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
    buf[n++] = '\n';
    g_log->Write(buf, n);
    g_log->Flush();
}
} namespace CTRPluginFramework {
static void InitLog(u64 tid) {
    char path[96];
    snprintf(path, sizeof(path), "/luma/plugins/%016llX/owe_log.txt", tid);
    static File f;
    if (File::Open(f, path, File::WRITE | File::CREATE | File::TRUNCATE) == 0) OWE::g_log = &f;
}

// ---------------------------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------------------------
static bool OsdDebug(const Screen &screen) {
    if (!screen.IsTop) return false;
    char line[96];
    snprintf(line, sizeof(line), "OWE fr=%08lX tbl=%d grass=%d spawned=%d", (unsigned long)Owe::FieldRoBase(),
             Owe::Dbg().lastTable, Owe::Dbg().playerInGrass, Owe::Dbg().active);
    screen.Draw(line, 4, 4, Color::White, Color::Black);
    snprintf(line, sizeof(line), "pos %.0f %.0f %.0f  patch=%d attr=%08lX", Owe::Dbg().px, Owe::Dbg().py, Owe::Dbg().pz,
             Owe::Dbg().patchCells, (unsigned long)Owe::Dbg().lastAttr);
    screen.Draw(line, 4, 14, Color::White, Color::Black);
    snprintf(line, sizeof(line), "%s", Owe::Dbg().msg);
    screen.Draw(line, 4, 24, Color::Yellow, Color::Black);
    return true;
}

// Menu entries are "action" entries (selected with A); their name shows the current state.
static void ToggleEnabled(MenuEntry *e) {
    Owe::Cfg().enabled = !Owe::Cfg().enabled;
    e->Name() = Owe::Cfg().enabled ? "Overworld encounters: ON" : "Overworld encounters: OFF";
}
static void ToggleDebug(MenuEntry *e) {
    static bool on = false;
    on = !on;
    if (on) OSD::Run(OsdDebug); else OSD::Stop(OsdDebug);
    e->Name() = on ? "Debug overlay: ON" : "Debug overlay: OFF";
}

void PatchProcess(FwkSettings &settings) {
    settings.ThreadPriority = 0x30;
}

void OnProcessExit(void) { Owe::Shutdown(); }

int main(void) {
    u64 tid = Process::GetTitleID();
    InitLog(tid);
    u16 ver = Process::GetVersion();
    Log("OWE start tid=%016llX ver=%u", tid, ver);

    PluginMenu *menu = new PluginMenu("Overworld Encounters", 0, 6, 0,
        "Visible wild Pokemon in tall grass (Sword/Shield style).\n"
        "Random grass encounters are replaced by Pokemon you can see and touch.");
    menu->SynchronizeWithFrame(true);

    OWE::Icons_Init();
#ifdef OWE_TEST_OVERLAY
    OSD::Run(OWE::Icons_Draw);
#endif
#ifdef OWE_TEST_AUTOBATTLE
    Owe::Cfg().debugAutoBattle = true;
#endif
#ifdef OWE_TEST_ICONS
    Owe::Cfg().iconMode = true;
    OSD::Run(OWE::Icons_Draw);
#endif
#ifdef OWE_TEST_SPECIES
    Owe::Cfg().debugSpecies = OWE_TEST_SPECIES;
    Owe::Cfg().debugForm = OWE_TEST_FORM;
    Owe::Cfg().verboseLog = true;
#ifdef OWE_TEST_HEAP
    Owe::Cfg().perModelHeap = OWE_TEST_HEAP;
#endif
#endif
    if (!Owe::Init(tid, ver)) {
        Log("unsupported game/version");
        OSD::Notify("Overworld Encounters: unsupported game version", Color::Red);
    } else {
        OSD::Notify("Overworld Encounters ready");
    }

    *menu += new MenuEntry("Overworld encounters: ON", nullptr, ToggleEnabled,
        "ON: wild Pokemon appear in tall grass and random grass encounters are disabled.\n"
        "OFF: vanilla behaviour.");
    *menu += new MenuEntry("Pokemon display: 3D models", nullptr, [](MenuEntry *e) {
        Owe::Cfg().iconMode = !Owe::Cfg().iconMode;
        if (Owe::Cfg().iconMode) OSD::Run(OWE::Icons_Draw); else OSD::Stop(OWE::Icons_Draw);
        e->Name() = Owe::Cfg().iconMode ? "Pokemon display: 2D icons (performance)" : "Pokemon display: 3D models";
    }, "3D models: the Pokemon's own animated model.\n"
       "2D icons: its PC box icon instead - much lighter (no models, no extra memory).\n"
       "Needs owe_icons.bin next to the plugin.");
    *menu += new MenuEntry("Pokemon cries: ON", nullptr, [](MenuEntry *e) {
        Owe::Cfg().cries = !Owe::Cfg().cries;
        e->Name() = Owe::Cfg().cries ? "Pokemon cries: ON" : "Pokemon cries: OFF";
    }, "Now and then a Pokemon in the grass calls out (rarely).");
    *menu += new MenuEntry("Big grass patches: up to 5 Pokemon", nullptr, [](MenuEntry *e) {
        Owe::Cfg().maxModels = Owe::Cfg().maxModels == 5 ? 3 : 5;
        e->Name() = Owe::Cfg().maxModels == 5 ? "Big grass patches: up to 5 Pokemon" : "Big grass patches: up to 3 Pokemon";
    }, "5: livelier big patches. 3: lighter on performance.\n(Old 3DS mode only has memory for 1 at a time.)");
    *menu += new MenuEntry("Debug overlay: OFF", nullptr, ToggleDebug, "Shows internal state on the top screen.");
    *menu += new MenuEntry("Force spawn now (debug)", nullptr, [](MenuEntry *) {
        Owe::Cfg().debugForceSpawn = true;
        MessageBox("A Pokemon will appear as soon as you are in tall grass.")();
    }, "Spawns a Pokemon right away (you must have walked in this patch of grass first).");

    menu->Run();
    delete menu;
    return 0;
}

} // namespace CTRPluginFramework
