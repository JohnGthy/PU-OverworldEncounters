// Field camera capture for the 2D (icon) mode: every scene render path hands its camera to
// gfl2::renderingengine::renderer::RenderingPipeLine::SceneRenderPath::SetCamera(DrawContext&)
// (v1.2 0x33F804). The camera (or, without one, the DrawContext) starts with a 4x4 projection
// matrix followed by a 3x4 view matrix (0x70 bytes), which SetCamera copies into the renderer.
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstring>
#include "Owe.hpp"

namespace OWE {
using namespace CTRPluginFramework;
void Log(const char *fmt, ...);

static const u32 kSceneRenderPath_SetCamera = 0x0033F804;

struct CamSlot { u32 path; u32 frame; float m[28]; };
static CamSlot g_slots[8];
static volatile u32 g_camFrame = 0;
static Hook g_hkCam;

static void SetCameraHook(void *self, void *ctx) {
    HookContext::GetCurrent().OriginalFunction<void>(self, ctx);
    u32 *vt = *(u32 **)self;
    void *cam = reinterpret_cast<void *(*)(void *)>(vt[0x24 / 4])(self);
    const float *src = (const float *)(cam ? cam : ctx);
    u32 path = (u32)self;
    int free = -1;
    for (int i = 0; i < 8; i++) {
        if (g_slots[i].path == path) { free = i; break; }
        if (free < 0 && (g_slots[i].path == 0 || g_camFrame - g_slots[i].frame > 120)) free = i;
    }
    if (free < 0) return;
    g_slots[free].path = path;
    g_slots[free].frame = g_camFrame;
    memcpy(g_slots[free].m, src, sizeof(g_slots[free].m));
}

void Camera_Init() {
    g_hkCam.InitializeForMitm(kSceneRenderPath_SetCamera, (u32)SetCameraHook);
    Log("camera hook=%d", (int)g_hkCam.Enable());
}

void Camera_Tick() { g_camFrame++; }

// Most recent perspective camera (projection row 3 = 0 0 -1 0) seen this frame or the last one.
static const CamSlot *FieldCamera() {
    const CamSlot *best = nullptr;
    for (int i = 0; i < 8; i++) {
        const CamSlot &s = g_slots[i];
        if (!s.path || g_camFrame - s.frame > 2) continue;
        const float *p = s.m;
        if (p[14] > -0.99f || p[14] < -1.01f || p[15] > 0.01f || p[15] < -0.01f) continue;
        if (!best || s.frame > best->frame) best = &s;
    }
    return best;
}

// World point -> top-screen pixel (400x240, y down). The 3DS projection is rotated a quarter
// turn (the LCD is a portrait framebuffer): clip.x runs bottom-to-top, clip.y right-to-left.
// ppu = screen pixels per world unit at that depth; vdir = the camera's right vector in world.
bool Camera_Project(float x, float y, float z, float &sx, float &sy, float &ppu, float vdir[3]) {
    const CamSlot *cam = FieldCamera();
    if (!cam) return false;
    const float *p = cam->m, *v = cam->m + 16;
    float e[3];
    for (int r = 0; r < 3; r++) e[r] = v[r * 4] * x + v[r * 4 + 1] * y + v[r * 4 + 2] * z + v[r * 4 + 3];
    float c[4];
    for (int r = 0; r < 4; r++) c[r] = p[r * 4] * e[0] + p[r * 4 + 1] * e[1] + p[r * 4 + 2] * e[2] + p[r * 4 + 3];
    if (c[3] < 10.0f) return false;                     // behind or too close to the camera
    sx = 200.0f * (1.0f - c[1] / c[3]);
    sy = 120.0f * (1.0f - c[0] / c[3]);
    float k = p[1] < 0 ? -p[1] : p[1];
    ppu = 120.0f * k / c[3];
    vdir[0] = v[0]; vdir[1] = v[1]; vdir[2] = v[2];
    return true;
}

void Camera_DumpForTest(float wx, float wy, float wz) {
    for (int i = 0; i < 8; i++) {
        CamSlot &s = g_slots[i];
        if (!s.path || g_camFrame - s.frame > 5) continue;
        const float *p = s.m, *v = s.m + 16;
        Log("cam %08lX P %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
            s.path, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        Log("    V %.3f %.3f %.3f %.1f | %.3f %.3f %.3f %.1f | %.3f %.3f %.3f %.1f",
            v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]);
        // eye-space and clip of the test point (row-major: r = M * [x y z 1])
        float e[3];
        for (int r = 0; r < 3; r++) e[r] = v[r * 4] * wx + v[r * 4 + 1] * wy + v[r * 4 + 2] * wz + v[r * 4 + 3];
        float c[4];
        for (int r = 0; r < 4; r++) c[r] = p[r * 4] * e[0] + p[r * 4 + 1] * e[1] + p[r * 4 + 2] * e[2] + p[r * 4 + 3];
        Log("    pt %.0f %.0f %.0f -> eye %.1f %.1f %.1f clip %.3f %.3f %.3f %.3f ndc %.3f %.3f",
            wx, wy, wz, e[0], e[1], e[2], c[0], c[1], c[2], c[3], c[3] ? c[0] / c[3] : 0, c[3] ? c[1] / c[3] : 0);
    }
}

} // namespace OWE
