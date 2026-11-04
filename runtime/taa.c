/* The game's side of vk.c's scene (Video > More Options > Anti-Aliasing FXAA, TAA, FSR 2, DLSS; Colour Precision High): the race's
   main view, 0x4daec0, drawn into the scene's images; at its end vk.c resolves them into the back buffer and the HUD follows there.
   Off-Road hands the renderer screen-space vertices (x, y, z, 1/w), so the renderer is given the game's own matrix, world -> screen
   (0x4f0cb0, as the camera is when the view draws: the right stick's, the in-between frames'), and takes each vertex back to the
   world and on to where the last frame saw it. What moves of itself is told: the sky (it moves with the camera, from the view's start
   to the land; the clouds, 0x4ea330), and each car (0x497d50: its matrix now and in the last scene; its wheels are drawn with it).
   Everything else is taken as still. In a two-player race the scene is not used (two views). Not in --test runs (widescreen_allowed). */
#include <math.h>
#include <string.h>
#include "runtime.h"
#include "funcs.h"
#include "gfx.h"
#include "vk.h"
#include "widescreen.h"

static int in_scene;
static unsigned long scene_frame;

#define CARS 16
static struct { uint32_t o; float w[16]; unsigned long frame; } was[CARS], now_[CARS];
static int nwas, nnow;

static void car_matrix(uint32_t o, float *w)
{
    uint32_t m = o + 0x24;
    for (int r = 0; r < 3; r++) {
        for (int k = 0; k < 3; k++)
            w[r * 4 + k] = RF32(m + 16 * (uint32_t)r + 4 * (uint32_t)k);
        w[r * 4 + 3] = 0.0f;
    }
    for (int k = 0; k < 3; k++)
        w[12 + k] = RF32(m + 48 + 4 * (uint32_t)k);
    w[15] = 1.0f;
}

/* world now -> world then: through the car's own space (rows: an orthonormal turn and a position) */
static void car_move(const float *cur, const float *prev, float *d)
{
    float inv[16];
    memset(inv, 0, sizeof inv);
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++)
            inv[k * 4 + r] = cur[r * 4 + k];
    for (int k = 0; k < 3; k++)
        inv[12 + k] = -(cur[12] * inv[k] + cur[13] * inv[4 + k] + cur[14] * inv[8 + k]);
    inv[15] = 1.0f;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            d[i * 4 + j] = inv[i * 4] * prev[j] + inv[i * 4 + 1] * prev[4 + j] + inv[i * 4 + 2] * prev[8 + j] + inv[i * 4 + 3] * prev[12 + j];
}

/* right before 0x4daec0 draws, the camera as it will draw */
void taa_view_begin(CPU *c, uint32_t view)
{
    extern int pad_is_two_players(void);
    if (in_scene || !widescreen_allowed() || !vk_scene_on() || pad_is_two_players())
        return;
    uint32_t cam = R32(view + 4), keep = c->ecx;
    static uint32_t buf;
    if (!buf)
        buf = galloc(64);
    c->ecx = cam;
    uint32_t mp = guest_call(c, 0x4f0cb0, 1, buf);
    c->ecx = keep;
    VkScene s;
    memset(&s, 0, sizeof s);
    for (int i = 0; i < 16; i++)
        s.g[i] = RF32(mp + 4 * (uint32_t)i);
    for (int i = 0; i < 3; i++)
        s.eye[i] = RF32(cam + 0x44 + 4 * (uint32_t)i);
    for (int i = 0; i < 3; i++)
        s.cam[i] = (int32_t)s.eye[i];
    s.near_far[0] = RF32(cam + 0x24c);
    s.near_far[1] = RF32(cam + 0x250);
    /* the vertical angle: how far up the screen a point a little above straight ahead goes */
    float up[3], fw[3], p[2][3];
    for (int i = 0; i < 3; i++) {
        up[i] = RF32(cam + 0x14 + 4 * (uint32_t)i);
        fw[i] = RF32(cam + 0x24 + 4 * (uint32_t)i);
        p[0][i] = s.eye[i] + fw[i] * 1000.0f;
        p[1][i] = p[0][i] + up[i] * 100.0f;
    }
    float ys[2];
    for (int k = 0; k < 2; k++) {
        float y = s.g[1] * p[k][0] + s.g[5] * p[k][1] + s.g[9] * p[k][2] + s.g[13];
        float w = s.g[3] * p[k][0] + s.g[7] * p[k][1] + s.g[11] * p[k][2] + s.g[15];
        ys[k] = w != 0.0f ? y / w : 0.0f;
    }
    float per = fabsf(ys[1] - ys[0]) * 10.0f;          /* screen units per unit of tan */
    s.fov_y = per > 1.0f ? 2.0f * atanf(384.0f / per) : 1.0f;
    extern const D3DState *d3d_last_state(void);
    const D3DState *ds = d3d_last_state();
    s.rt = ds ? ds->rt : NULL;
    vk_scene_begin(&s);
    vk_scene_class(VK_SC_SKY);
    in_scene = 1;
    scene_frame = frame_count;
    memcpy(was, now_, sizeof was);
    nwas = nnow;
    nnow = 0;
}

void taa_view_end(void)
{
    if (!in_scene)
        return;
    in_scene = 0;
    vk_scene_object(NULL);
    vk_scene_end();
}

/* the land and what follows it (time_world_hook), the clouds (time_sky_hook) */
void taa_world(void)
{
    if (in_scene)
        vk_scene_class(VK_SC_OBJECT);
}
void taa_land(int on)
{
    if (in_scene)
        vk_scene_class(on ? VK_SC_TERRAIN : VK_SC_OBJECT);
}
void taa_sky(int on)
{
    if (in_scene)
        vk_scene_class(on ? VK_SC_SKY : VK_SC_OBJECT);
}

/* 0x497d50, a car's draw (ECX it): its move since the last scene for what it draws */
int taa_car_hook(CPU *c)
{
    extern int light_car_hook(CPU *c);
    light_car_hook(c);
    if (!in_scene)
        return 0;
    uint32_t o = c->ecx;
    float w[16], d[16];
    car_matrix(o, w);
    int found = 0;
    for (int i = 0; i < nwas; i++)
        if (was[i].o == o) {
            car_move(w, was[i].w, d);
            found = 1;
            break;
        }
    if (nnow < CARS) {
        now_[nnow].o = o;
        memcpy(now_[nnow].w, w, sizeof w);
        nnow++;
    }
    vk_scene_object(found ? d : NULL);
    F_00497d50_orig(c);
    vk_scene_object(NULL);
    return 1;
}
