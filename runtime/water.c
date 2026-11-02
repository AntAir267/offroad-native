/* Water (More Options > Lighting > Water): the game's puddles and lake as a real shallow-water simulation on the graphics chip, for the look only
   (shaders water*, vk.c vkr_water_*). The game's puddle objects still make their drag, sound and splash objects (0x49f6e0) and the car's
   handling goes on reading them, wherever the simulated water has gone; only the drawing is replaced.
   One grid moves with the view, its cells numbered from the world, so moving it copies nothing. It only runs where there is water: the box
   round the puddles in reach, or, near the lake, all of it. With none there it costs nothing.
   Puddles [verified: 0x4a1710, a run's log]: each made once by 0x4a1710 as the level loads (the game's list 0x5caed8 only holds those in view). +0x74 ground height at the middle, +0x7c, +0x80 the middle, +0x84,
   +0x88 half size. Drawn by 0x4a0180, second pass 0x4a1360 (the sky through a stencil). Far ones, wholly outside the grid, stay the game's.
   The lake: cells flagged 2 at height 0; the game draws its surface only with view +0x38 bit 1, which it never sets [verified: 0x4d9340 clears it;
   0x5458c0]: set here, native_land.c, so the game's waves carry on past the grid.
   Recovery (Ant asked to be told): a seep of a twentieth of what is missing each second, so a puddle driven through is 90% back in about 45 s,
   half back in 14; water that left its place drains away slowly (a film 3 cm deep in 4 s) and the ground stays dark for 50 s more. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"
#include "gfx.h"
#include "vk.h"

#define DX 25
#define MAX_PUD 8

static uint32_t pud_all[MAX_PUD];
static int pud_n;
static int water_log;
static int water_on = -1, image = 512, recover = 1;
static unsigned tick_steps;
static double last_step;
static float car_last[12][3];
static uint32_t car_last_o[12];
static VkWaterStep st;
static int have_rect, in_grid[MAX_PUD];
static int org[2], org_ok;

void water_options(void)
{
    water_on = -1;
}
int water_active(void) { return water_on > 0 && have_rect; }
void water_tick(void) { tick_steps++; }

static int enabled(void)
{
    if (water_on < 0) {
        const char *v = option_str("water", "simulated");
        water_on = widescreen_allowed() && vk_on() && strcasecmp(v, "original") && !strcasecmp(option_str("baked_lighting", "on"), "on");
        image = !strcasecmp(option_str("water_area", "large"), "small") ? 256 : 512;
        recover = !getenv("ORR_WATER_KEEP");
        water_log = getenv("ORR_WATER_LOG") != NULL;
    }
    return water_on;
}
int water_enabled(void) { return enabled(); }

static double wat_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/* ORR_WATER_TEST=<cars>,<puddle>[,<metres per second>]: for tests, ghost cars (their wheels only) back and forth across a puddle, the camera on it. The
   same scene each run. Nothing of the game's is touched but the camera's picture. */
static int test_cars = -1, test_pud;
static float test_speed = 15.0f, test_at[2];   /* (puddle -1: at this x, z, the lake's shore) */
static void test_parse(void)
{
    const char *e = getenv("ORR_WATER_TEST");
    test_cars = 0;
    if (e) {
        sscanf(e, "%d,%d,%f,%f,%f", &test_cars, &test_pud, &test_speed, &test_at[0], &test_at[1]);
    }
}
void water_test_camera(uint32_t cam)
{
    if (test_cars < 0)
        test_parse();
    if (!test_cars)
        return;
    if (pud_n <= test_pud)
        return;
    uint32_t o = test_pud >= 0 ? pud_all[test_pud] : 0;
    float cx = o ? RF32(o + 0x7c) : test_at[0], cz = o ? RF32(o + 0x80) : test_at[1], hx = o ? RF32(o + 0x84) : 1500.0f, level = o ? RF32(o + 0x74) : 0.0f;
    static const float dir[3] = {0.0f, 0.0f, 1.0f};
    float d = hx * 1.5f + 400.0f;
    float pos[3] = {cx - dir[0] * d, level + 260.0f + hx * 0.18f, cz - dir[2] * d};
    float f[3] = {cx - pos[0], level - pos[1], cz - pos[2]};
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (int i = 0; i < 3; i++)
        f[i] /= fl;
    float r[3] = {f[2], 0.0f, -f[0]};
    float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
    r[0] /= rl;
    r[2] /= rl;
    float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    for (int i = 0; i < 3; i++) {
        union { float v; uint32_t b; } a = {r[i]}, b = {u[i]}, c = {f[i]}, p = {pos[i]};
        W32(cam + 4 + 4 * (uint32_t)i, a.b);
        W32(cam + 0x14 + 4 * (uint32_t)i, b.b);
        W32(cam + 0x24 + 4 * (uint32_t)i, c.b);
        W32(cam + 0x44 + 4 * (uint32_t)i, p.b);
    }
    W8(cam + 0x1d0, 0);
    W8(cam + 0xcc, 0);
}
static int test_add_cars(int n, float ticks)
{
    if (test_cars <= 0 || pud_n <= test_pud)
        return n;
    uint32_t o = test_pud >= 0 ? pud_all[test_pud] : 0;
    float cx = o ? RF32(o + 0x7c) : test_at[0], cz = o ? RF32(o + 0x80) : test_at[1], hx = o ? RF32(o + 0x84) : 1500.0f, hz = o ? RF32(o + 0x88) : 1500.0f, lev = o ? RF32(o + 0x74) : 0.0f;
    for (int i = 0; i < test_cars && n < 12; i++, n++) {
        /* along x, each on its own line across and its own time: a run across, then 5 seconds' rest, so the water can be seen to come back */
        float reach = hx + 800.0f, period = 2.0f * reach / (test_speed * 100.0f) + 5.0f;
        float t = fmodf(ticks / 60.0f + (float)i * period / (float)test_cars, period);
        float xp = t < period - 5.0f ? -reach + test_speed * 100.0f * t : 1e6f;
        float zo = (test_cars > 1 ? ((float)i / (float)(test_cars - 1) - 0.5f) * 1.3f : 0.0f) * hz;
        float *C = st.cars + n * 24;
        C[0] = cx + xp; C[1] = lev + 60.0f; C[2] = cz + zo;
        C[3] = 500.0f + test_speed * 100.0f / 60.0f;
        C[4] = test_speed * 100.0f; C[5] = 0; C[6] = 0; C[7] = 0;
        for (int w = 0; w < 4; w++) {
            float *W = C + 8 + w * 4;
            W[0] = C[0] + (w & 1 ? 125.0f : -125.0f);
            W[1] = lev + 34.0f;
            W[2] = C[2] + (w & 2 ? 85.0f : -85.0f);
            W[3] = 34.0f;
        }
    }
    return n;
}

/* a view's start (light.c): where the grid goes, what is in it, a step for each tick gone by (at most 3: below 20 frames a second the
   water goes slower, never wrong). In a two-player race it follows the first view. */
void water_view(const float *eye, const float *forward, const uint32_t *cars, int ncars, int level_new)
{
    if (water_log && frame_count % 600 == 0)
        LOG(LOG_ALWAYS, "water view: on %d ticks %u puddles %d", water_on, tick_steps, pud_n);
    if (!enabled())
        return;
    if (level_new) {
        have_rect = 0;
        org_ok = 0;
        memset(&st, 0, sizeof st);
        memset(car_last_o, 0, sizeof car_last_o);
        memset(in_grid, 0, sizeof in_grid);
    }
    static unsigned long last_frame;
    int first = last_frame != frame_count;
    last_frame = frame_count;
    if (!first || !tick_steps)
        return;
    if (water_log && frame_count % 60 == 0 && ncars > 0) {
        uint32_t mo = cars[0] + 0x24, wl = R32(cars[0] + 0x508), w0 = wl ? R32(wl) + 0x24 : 0;
        float nearest = 1e12f;
        for (int i = 0; i < pud_n; i++) {
            float dx = RF32(pud_all[i] + 0x7c) - RF32(mo + 48), dz = RF32(pud_all[i] + 0x80) - RF32(mo + 56), d = sqrtf(dx * dx + dz * dz);
            nearest = d < nearest ? d : nearest;
        }
        LOG(LOG_ALWAYS, "water: cars %d, first at %.0f %.0f %.0f wheels %d, wheel 0 at %.0f %.0f %.0f, nearest puddle %.0f", ncars, RF32(mo + 48), RF32(mo + 52), RF32(mo + 56), R32(cars[0] + 0x500), w0 ? RF32(w0 + 48) : 0, w0 ? RF32(w0 + 52) : 0, w0 ? RF32(w0 + 56) : 0, nearest);
    }
    int half = image / 2;
    /* ahead of the view, in steps of 16 cells */
    int cx = (int)floorf((eye[0] + forward[0] * 2200.0f) / DX), cz = (int)floorf((eye[2] + forward[2] * 2200.0f) / DX);
    if (!org_ok || abs(cx - (org[0] + half)) >= 16 || abs(cz - (org[1] + half)) >= 16) {
        org[0] = (cx & ~15) - half;
        org[1] = (cz & ~15) - half;
        org_ok = 1;
    }
    int x0 = org[0], z0 = org[1], x1 = org[0] + image, z1 = org[1] + image;
    /* the box round the water */
    int bx0 = 1 << 30, bz0 = 1 << 30, bx1 = -(1 << 30), bz1 = -(1 << 30), np = 0;
    for (int i = 0; i < pud_n; i++) {
        uint32_t o = pud_all[i];
        float px = RF32(o + 0x7c), pz = RF32(o + 0x80), hx = RF32(o + 0x84) * 1.25f, hz = RF32(o + 0x88) * 1.25f;
        int a0 = (int)floorf((px - hx) / DX) - 40, b0 = (int)floorf((pz - hz) / DX) - 40, a1 = (int)ceilf((px + hx) / DX) + 40, b1 = (int)ceilf((pz + hz) / DX) + 40;
        in_grid[i] = a0 >= x0 && b0 >= z0 && a1 <= x1 && b1 <= z1;
        if (!in_grid[i])
            continue;
        if (a0 < bx0) bx0 = a0;
        if (b0 < bz0) bz0 = b0;
        if (a1 > bx1) bx1 = a1;
        if (b1 > bz1) bz1 = b1;
        float *P = st.puddles + np * 8;
        P[0] = px;
        P[1] = pz;
        P[2] = RF32(o + 0x84);
        P[3] = RF32(o + 0x88);
        P[4] = RF32(o + 0x74) + 8.0f;
        P[5] = P[6] = P[7] = 0.0f;
        np++;
    }
    extern int light_lake_near(float x0, float z0, float x1, float z1);
    int lake = light_lake_near((float)x0 * DX, (float)z0 * DX, (float)x1 * DX, (float)z1 * DX);
    if (lake) {
        bx0 = x0;
        bz0 = z0;
        bx1 = x1;
        bz1 = z1;
    }
    memcpy(st.prev, st.rect, sizeof st.prev);
    if (bx1 <= bx0) {
        /* no water in reach: nothing runs */
        have_rect = 0;
        memset(st.rect, 0, sizeof st.rect);
        tick_steps = 0;
        return;
    }
    bx0 &= ~1; bz0 &= ~1;
    st.rect[0] = bx0;
    st.rect[1] = bz0;
    st.rect[2] = (bx1 - bx0 + 1) & ~1;
    st.rect[3] = (bz1 - bz0 + 1) & ~1;
    if (st.rect[2] > image) st.rect[2] = image;
    if (st.rect[3] > image) st.rect[3] = image;
    have_rect = 1;
    st.image = image;
    st.npuddles = np;
    st.lake = lake;
    st.recover = recover;
    st.ticks = (float)frame_count;
    /* the cars that can reach it */
    float ax0 = (float)st.rect[0] * DX, az0 = (float)st.rect[1] * DX, ax1 = ax0 + (float)st.rect[2] * DX, az1 = az0 + (float)st.rect[3] * DX;
    int n = 0;
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < ncars && i < 12; i++) {
        uint32_t mo = cars[i] + 0x24;
        float p[3] = {RF32(mo + 48), RF32(mo + 52), RF32(mo + 56)};
        float v[3] = {0, 0, 0};
        if (car_last_o[i] == cars[i])
            for (int k = 0; k < 3; k++)
                v[k] = (p[k] - car_last[i][k]) / (dt * (float)tick_steps);
        memcpy(car_last[i], p, sizeof p);
        car_last_o[i] = cars[i];
        if (p[0] < ax0 - 400 || p[2] < az0 - 400 || p[0] > ax1 + 400 || p[2] > az1 + 400)
            continue;
        float *C = st.cars + n * 24;
        C[0] = p[0];
        C[1] = p[1];
        C[2] = p[2];
        C[3] = 500.0f + sqrtf(v[0] * v[0] + v[2] * v[2]) * dt;
        C[4] = v[0];
        C[5] = v[1];
        C[6] = v[2];
        C[7] = 0;
        /* the wheels: from the car's own matrix (the wheel objects' positions are the car's own [verified: a run's log]) */
        for (int w = 0; w < 4; w++) {
            float *W = C + 8 + w * 4;
            float sx = w & 1 ? 85.0f : -85.0f, fz = w & 2 ? 125.0f : -125.0f;
            W[0] = p[0] + RF32(mo + 0) * sx + RF32(mo + 32) * fz;
            W[1] = p[1] + RF32(mo + 4) * sx + RF32(mo + 36) * fz - 40.0f;
            W[2] = p[2] + RF32(mo + 8) * sx + RF32(mo + 40) * fz;
            W[3] = 34.0f;
        }
        n++;
    }
    n = test_add_cars(n, (float)frame_count);
    st.ncars = n;
    if (water_log && frame_count % 300 == 0)
        LOG(LOG_ALWAYS, "water: frame %lu grid %d %d, area %d %d %d %d, puddles %d, lake %d, cars %d, wheel 0 %.0f %.0f %.0f", frame_count, org[0], org[1], st.rect[0], st.rect[1], st.rect[2], st.rect[3], np, lake, n, st.cars[8], st.cars[9], st.cars[10]);
    int steps = (int)tick_steps;
    steps = steps > 3 ? 3 : steps;
    for (int i = 0; i < steps; i++) {
        vk_water_step(&st);
        memcpy(st.prev, st.rect, sizeof st.prev);
    }
    tick_steps = 0;
    last_step = wat_now_ms();
}

/* after the world (game.c): the surface */
void water_draw(CPU *c, uint32_t view)
{
    if (!water_active())
        return;
    uint32_t cam = R32(view + 4), keep = c->ecx;
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
    if (!ds)
        return;
    /* behind the camera or off the sides: nothing to copy or draw */
    {
        float xs[2] = {(float)st.rect[0] * DX, (float)(st.rect[0] + st.rect[2]) * DX}, zs[2] = {(float)st.rect[1] * DX, (float)(st.rect[1] + st.rect[3]) * DX};
        float ys[2] = {1e9f, -1e9f};
        for (int i = 0; i < st.npuddles; i++) {
            float l = st.puddles[i * 8 + 4];
            ys[0] = l < ys[0] ? l : ys[0];
            ys[1] = l > ys[1] ? l : ys[1];
        }
        if (st.lake) {
            ys[0] = ys[0] > -100.0f ? -100.0f : ys[0];
            ys[1] = ys[1] < 100.0f ? 100.0f : ys[1];
        }
        int left = 0, right = 0, behind = 0;
        for (int i = 0; i < 8; i++) {
            float x = xs[i & 1], y = ys[(i >> 1) & 1] + ((i >> 1) & 1 ? 100.0f : -100.0f), z = zs[(i >> 2) & 1];
            float cx = rows[0] * x + rows[1] * y + rows[2] * z + rows[3], cw = rows[12] * x + rows[13] * y + rows[14] * z + rows[15];
            behind += cw <= 0.0f;
            left += cx < -2000.0f && cw > 0.0f;
            right += cx > cw * 1024.0f + 2000.0f;
        }
        if (behind == 8 || left == 8 || right == 8)
            return;
    }
    float between = (float)((wat_now_ms() - last_step) * 60.0 / 1000.0);
    between = between < 0 ? 0 : between > 1 ? 1 : between;
    vk_water_draw(ds->rt, ds->zb, rows, between, (float)frame_count / 60.0f);
}

static int is_ours(uint32_t o)
{
    if (!water_active())
        return 0;
    for (int i = 0; i < pud_n; i++)
        if (pud_all[i] == o)
            return in_grid[i];
    return 0;
}

/* a level loading (game.c); each puddle as it is made, 0x4a1710 (ret 0x14; ECX it) */
void water_level(void)
{
    pud_n = 0;
}
int puddle_init_hook(CPU *c)
{
    uint32_t o = c->ecx;
    F_004a1710_orig(c);
    if (pud_n < MAX_PUD)
        pud_all[pud_n++] = o;
    if (getenv("ORR_WATER_LOG"))
        LOG(LOG_ALWAYS, "water: puddle %x centre %.0f %.0f half %.0f %.0f level %.1f", o, RF32(o + 0x7c), RF32(o + 0x80), RF32(o + 0x84), RF32(o + 0x88), RF32(o + 0x74));
    return 1;
}

/* 0x4a0180, a puddle's draw; 0x4a1360, its sky through the stencil (ret 4; ECX it) */
int puddle_draw_hook(CPU *c)
{
    if (!is_ours(c->ecx))
        return 0;
    api_return(c, 1, 1);
    return 1;
}
int puddle_sky_hook(CPU *c)
{
    if (!is_ours(c->ecx))
        return 0;
    api_return(c, 1, 1);
    return 1;
}
