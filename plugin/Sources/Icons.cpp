// 2D icon mode: draws each overworld Pokemon as its PC box icon over the field (CTRPF OSD), at the
// screen position of its spot in the grass (Camera.cpp), sized by distance. No 3D models, no
// model heap, no extra memory from the system.
//
// Icons: sd:/luma/plugins/00040000001B5100/owe_icons.bin, made from the game's own icon archive
// (romfs a/0/6/2, 64x32 RGBA5551) by tools/make_icons.py: "OWEI", u32 count, 8 bytes pad, then
// count x 64x32 u16 pixels (row-major, top-down, bit 0 = opaque). Which icon a species/form uses
// is decided by the game's own lookup (code.bin 0x30CC1C, used by app::tool::PokeIcon).
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstring>
#include "Owe.hpp"

namespace OWE {
using namespace CTRPluginFramework;

static const u32 kPokeIcon_GetIndex = 0x0030CC1C;   // (species, form, sex, egg, flag) -> icon index
static const int kW = 64, kH = 32, kMaxSlots = 5;

struct IconPix {
    u16 px[kW * kH];
    int x0, y0, x1, y1;          // opaque bounding box
};
static IconPix g_icons[kMaxSlots];
static File g_file;
static u32 g_count = 0;

void Icons_Init() {
    char path[96];
    snprintf(path, sizeof(path), "/luma/plugins/%016llX/owe_icons.bin", Process::GetTitleID());
    if (File::Open(g_file, path, File::READ) != 0) { Log("icons: %s missing", path); return; }
    u32 hdr[4] = {0};
    g_file.Read(hdr, sizeof(hdr));
    if (hdr[0] != 0x4945574F /* OWEI */) { Log("icons: bad header"); g_file.Close(); return; }
    g_count = hdr[1];
    Log("icons: %lu in %s", g_count, path);
}

bool Icons_Load(int slot, int species, int form) {
    if (slot < 0 || slot >= kMaxSlots || !g_count) return false;
    u32 idx = reinterpret_cast<u32 (*)(u32, u32, u32, u32, u32)>(kPokeIcon_GetIndex)(
        (u32)species, (u32)form, 0, 0, 0) & 0xFFFF;
    if (idx >= g_count) { Log("icons: #%d form %d -> index %lu out of range", species, form, idx); return false; }
    IconPix &ic = g_icons[slot];
    if (g_file.Seek(16 + idx * sizeof(ic.px), File::SET) != 0 || g_file.Read(ic.px, sizeof(ic.px)) != 0)
        return false;
    ic.x0 = kW; ic.y0 = kH; ic.x1 = -1; ic.y1 = -1;
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++)
            if (ic.px[y * kW + x] & 1) {
                if (x < ic.x0) ic.x0 = x;
                if (x > ic.x1) ic.x1 = x;
                if (y < ic.y0) ic.y0 = y;
                if (y > ic.y1) ic.y1 = y;
            }
    return ic.x1 >= 0;
}

static inline Color Px(u16 v) {
    return Color((u8)(((v >> 11) & 31) * 255 / 31), (u8)(((v >> 6) & 31) * 255 / 31),
                 (u8)(((v >> 1) & 31) * 255 / 31));
}

bool Icons_Draw(const Screen &screen) {
    if (!screen.IsTop || !Owe_FieldActive()) return false;
    IconDraw d[kMaxSlots];
    int n = Owe_GetIconDraws(d, kMaxSlots);
    if (!n) return false;
    // far ones first, so nearer icons are drawn over them
    float sxs[kMaxSlots], sys[kMaxSlots], ppus[kMaxSlots]; bool ok[kMaxSlots]; float right[kMaxSlots][3];
    for (int i = 0; i < n; i++) ok[i] = Camera_Project(d[i].x, d[i].y, d[i].z, sxs[i], sys[i], ppus[i], right[i]);
    int order[kMaxSlots];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (ppus[order[j]] < ppus[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    bool drew = false;
    for (int oi = 0; oi < n; oi++) {
        int i = order[oi];
        if (!ok[i] || d[i].scale <= 0.02f) continue;
        const IconPix &ic = g_icons[d[i].slot];
        if (ic.x1 < 0) continue;
        // ~75 world units for the icon's 30 px: about the size of a mid-sized Pokemon model
        float k = ppus[i] * 75.0f / 30.0f;
        if (k < 0.6f) k = 0.6f;
        if (k > 3.0f) k = 3.0f;
        k *= d[i].scale;
        // icons face left; mirror when the Pokemon faces right on screen
        float fx = sinf(d[i].yaw), fz = cosf(d[i].yaw);
        bool flip = right[i][0] * fx + right[i][2] * fz > 0.0f;
        int bw = ic.x1 - ic.x0 + 1, bh = ic.y1 - ic.y0 + 1;
        int dw = (int)(bw * k + 0.5f), dh = (int)(bh * k + 0.5f);
        if (dw < 1 || dh < 1) continue;
        float hop = d[i].walking ? fabsf(sinf(d[i].frame * 0.45f)) * 3.0f * k : 0.0f;
        int left = (int)(sxs[i] - dw * 0.5f), top = (int)(sys[i] - dh - hop);
        if (left >= 400 || top >= 240 || left + dw <= 0 || top + dh <= 0) continue;
        for (int y = 0; y < dh; y++) {
            int py = top + y;
            if (py < 0 || py >= 240) continue;
            int srcY = ic.y0 + (int)(y / k);
            if (srcY > ic.y1) srcY = ic.y1;
            for (int x = 0; x < dw; x++) {
                int pxx = left + x;
                if (pxx < 0 || pxx >= 400) continue;
                int u = (int)(x / k);
                if (u >= bw) u = bw - 1;
                int srcX = flip ? ic.x1 - u : ic.x0 + u;
                u16 v = ic.px[srcY * kW + srcX];
                if (!(v & 1)) continue;
                screen.DrawPixel(pxx, py, Px(v));
            }
        }
        if (d[i].shiny && ((d[i].frame / 10) & 1)) {        // blinking sparkle, top right
            int cx = left + dw, cy = top;
            for (int t = -2; t <= 2; t++) {
                if (cx + t >= 0 && cx + t < 400 && cy >= 0 && cy < 240) screen.DrawPixel(cx + t, cy, Color(255, 236, 64));
                if (cx >= 0 && cx < 400 && cy + t >= 0 && cy + t < 240) screen.DrawPixel(cx, cy + t, Color(255, 236, 64));
            }
        }
        drew = true;
    }
    return drew;
}

} // namespace OWE
