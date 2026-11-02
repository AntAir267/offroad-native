/* Reactive puddles (More Options > Lighting > Puddles): the game's puddles, in their places and at their sizes, drawn as water
   with a height grid simulated on the graphics chip: runtime/shaders/puddle*, vk.c vkr_puddle_*. For the look only: the game's
   puddle objects still make their drag, sound and splash objects (0x49f6e0); only their drawing is replaced. Rivers and lakes
   stay as the game draws them.
   Puddle object [verified: 0x4a1710]: +0x74 ground height at its middle, +0x7c, +0x80 its middle, +0x84, +0x88 half size. Drawn by 0x4a0180,
   the second pass 0x4a1360 (the sky through a stencil). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"
#include "gfx.h"
#include "vk.h"

#define SEEN 16
static uint32_t seen[SEEN], slots[VK_PUDDLES];
static int nseen, puddle_on = -1;
static unsigned tick_steps;
static float car_last[12][3];
static uint32_t car_last_o[12];

void puddle_options(void) { puddle_on = -1; }
int puddle_active(void) { return puddle_on > 0 && slots[0] != 0; }
void puddle_tick(void) { tick_steps++; }

static int enabled(void)
{
    if (puddle_on < 0)
        puddle_on = widescreen_allowed() && vk_on() && !strcasecmp(option_str("puddles", "reactive"), "reactive") && !strcasecmp(option_str("baked_lighting", "on"), "on");
    return puddle_on;
}

static int slot_of(uint32_t o)
{
    for (int i = 0; i < VK_PUDDLES; i++)
        if (slots[i] == o)
            return i;
    return -1;
}

/* a view's start (light.c): the nearest puddles get grids; a step for each tick that has gone by */
void puddle_view(const float *eye, const uint32_t *cars, int ncars, int level_new)
{
    if (!enabled())
        return;
    if (level_new) {
        memset(slots, 0, sizeof slots);
        nseen = 0;
        memset(car_last_o, 0, sizeof car_last_o);
    }
    static uint32_t chosen[VK_PUDDLES];
    float best[VK_PUDDLES];
    memset(chosen, 0, sizeof chosen);
    for (int i = 0; i < VK_PUDDLES; i++)
        best[i] = 25000.0f * 25000.0f;
    for (int i = 0; i < nseen; i++) {
        float dx = RF32(seen[i] + 0x7c) - eye[0], dz = RF32(seen[i] + 0x80) - eye[2], d = dx * dx + dz * dz;
        for (int k = 0; k < VK_PUDDLES; k++)
            if (d < best[k]) {
                for (int m = VK_PUDDLES - 1; m > k; m--) {
                    best[m] = best[m - 1];
                    chosen[m] = chosen[m - 1];
                }
                best[k] = d;
                chosen[k] = seen[i];
                break;
            }
    }
    VkPuddleSlot out[VK_PUDDLES];
    uint32_t next[VK_PUDDLES];
    memset(next, 0, sizeof next);
    /* (one keeps its slot while it stays chosen) */
    for (int k = 0; k < VK_PUDDLES; k++)
        for (int s = 0; s < VK_PUDDLES; s++)
            if (chosen[k] && slots[s] == chosen[k]) {
                next[s] = chosen[k];
                chosen[k] = 0;
            }
    for (int k = 0; k < VK_PUDDLES; k++)
        for (int s = 0; chosen[k] && s < VK_PUDDLES; s++)
            if (!next[s]) {
                next[s] = chosen[k];
                chosen[k] = 0;
            }
    for (int s = 0; s < VK_PUDDLES; s++) {
        out[s].fresh = next[s] != slots[s];
        slots[s] = next[s];
        if (!next[s]) {
            out[s].level = -2e8f;
            continue;
        }
        uint32_t o = next[s];
        float cx = RF32(o + 0x7c), cz = RF32(o + 0x80), hx = RF32(o + 0x84) * 1.25f, hz = RF32(o + 0x88) * 1.25f;
        out[s].rect[0] = cx - hx;
        out[s].rect[1] = cz - hz;
        out[s].rect[2] = 2.0f * hx;
        out[s].rect[3] = 2.0f * hz;
        out[s].level = RF32(o + 0x74) + 8.0f;
    }
    /* the cars: where they are, which way they go, how fast (a tick's move) */
    float C[12 * 8];
    int n = 0;
    for (int i = 0; i < ncars && n < 12; i++, n++) {
        uint32_t mo = cars[i] + 0x24;
        float p[3] = {RF32(mo + 48), RF32(mo + 52), RF32(mo + 56)};
        float dx = 0, dz = 0;
        if (car_last_o[n] == cars[i]) {
            dx = p[0] - car_last[n][0];
            dz = p[2] - car_last[n][2];
        }
        float sp = sqrtf(dx * dx + dz * dz);
        if (tick_steps) {
            memcpy(car_last[n], p, sizeof p);
            car_last_o[n] = cars[i];
        }
        C[n * 8] = p[0];
        C[n * 8 + 1] = p[1];
        C[n * 8 + 2] = p[2];
        C[n * 8 + 3] = 0.0f;
        C[n * 8 + 4] = sp > 0.01f ? dx / sp : RF32(mo + 32);
        C[n * 8 + 5] = 0.0f;
        C[n * 8 + 6] = sp > 0.01f ? dz / sp : RF32(mo + 40);
        C[n * 8 + 7] = sp;
    }
    if (tick_steps || out[0].fresh) {
        vk_puddle_step(out, VK_PUDDLES, C, n);
        tick_steps = 0;
    }
}

/* 0x4a0180, a puddle's draw (ret 4; ECX it, first argument the view) */
int puddle_draw_hook(CPU *c)
{
    if (!enabled())
        return 0;
    uint32_t o = c->ecx;
    int known = 0;
    for (int i = 0; i < nseen; i++)
        known |= seen[i] == o;
    if (!known && nseen < SEEN)
        seen[nseen++] = o;
    int s = slot_of(o);
    if (s < 0)
        return 0;
    uint32_t view = R32(c->esp + 4), cam = R32(view + 4), keep = c->ecx;
    static uint32_t buf;
    if (!buf)
        buf = galloc(64);
    c->ecx = cam;
    uint32_t mp = guest_call(c, 0x4f0cb0, 1, buf);
    c->ecx = keep;
    float m[16], rows[16];
    for (int i = 0; i < 16; i++)
        m[i] = RF32(mp + 4 * (uint32_t)i);
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++)
            rows[r * 4 + k] = m[k * 4 + r];
    extern const D3DState *d3d_last_state(void);
    const D3DState *ds = d3d_last_state();
    if (ds)
        vk_puddle_draw(ds->rt, ds->zb, s, rows, (float)frame_count / 60.0f);
    api_return(c, 1, 1);
    return 1;
}

/* 0x4a1360: the sky through the stencil, for the game's own puddle: not for ours */
int puddle_sky_hook(CPU *c)
{
    if (!enabled() || slot_of(c->ecx) < 0)
        return 0;
    api_return(c, 1, 1);
    return 1;
}
