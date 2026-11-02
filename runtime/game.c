/* Off-Road's own fixes and hooks (builds/orr_zoom.py HOOKS).

   Draw distance: VIDEO's Draw Distance row picks a far plane of 16000, 21000 or 26000 at the race's load
   (0x4d8630: 0x5d0e78), which the cameras take from there [verified: decompile]. The More Options row raises how
   far they cull: game_camera_set. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"

static int fog_off;
#include "gfx.h"
#include "vk.h"
#include "native_land.h"

/* A camera's far plane, +0x250, comes through 0x4f1350 from the race's setup and again from the camera's own stored
   values, so a raised one comes back in. All of it must be raised together: the game clips every polygon at z 1 (renderer
   +0x400, 0x53fc20), and the fog runs to the far plane. A first go kept the depth mapping at the game's number to keep the sky
   behind things (the fault was the missing stencil, DESIGN.md) and raised only +0x250: nothing past the game's 260 m ever drew
   (Ant: "still fogged", then with the fog moved, "low draw distance"). Extreme is 2 times: at 2.5 the game's CPU went over a frame's time in puddles. Only numbers the game's own range can give are raised. */
#define GAME_FAR_MAX 26000.0f
static float far_scale(void)
{
    const char *v = option_str("draw_distance", "far");
    return !strcasecmp(v, "far") ? 1.5f : !strcasecmp(v, "extreme") ? 2.0f : 1.0f;
}
float game_far(float far)
{
    return far >= 16000.0f ? (far > GAME_FAR_MAX ? far : far * far_scale()) : far;
}

/* Volumetric dust. The game's puffs are objects of a sprite class whose draw (0x53c260) makes one camera-facing
   quad: +0x2c world position, +0x3c, +0x40 half width and height, +0x48 colour (ARGB, its alpha fades), +0x50 shown,
   +0x64 age [verified: 0x53c0b0, 0x417a30, disassembly of 0x53c260]. The tailpipe's is 0x57abcc (0x491070, the car's
   Exhaust objects, 0xff4f4f4f), the wheels' 0x57ad34 (0x4a42e0, a surface's colour) [verified: their code; a first go
   had them the wrong way round and painted the exhaust tan]. 0x57acf4 (0x4bc5e0) stays a sprite. With the option on, a puff of these
   goes to the volumetric renderer (vk.c, from 1nsane-native) in view space and the sprite is not drawn. That
   renderer reads a Direct3D view and projection, which this game does not use, so it gets a copy of the
   device's state with them made up from the camera (+0x178, +0x188: z = a + b / w; focal length 640). */
static int dust_on, streamer_on, stick_camera, int_on, int_hz;

void game_options(void)
{
    dust_on = widescreen_allowed() && !strcasecmp(option_str("volumetric_dust", "on"), "on");
    streamer_on = !strcasecmp(option_str("streamer_mode", "off"), "on");
    stick_camera = !strcasecmp(option_str("camera_stick", "on"), "on");
    fog_off = !strcasecmp(option_str("fog", "off"), "off");
    {
        const char *fr = option_str("frame_rate", "display");
        extern int host_refresh_hz(void);
        int_hz = !strcasecmp(fr, "display") ? host_refresh_hz() : atoi(fr);
        int_on = int_hz > 61;
    }
    extern void light_options(void), mix_options(void), puddle_options(void);
    puddle_options();
    light_options();
    mix_options();
}

/* One tick a frame. The game steps at 60 a second however fast it draws: 0x4db240 reads the time since the last frame
   from the timer at 0x5d2758 (made at 60 in 0x502630), and +0x30 is the whole ticks in it [verified: decompile]. On a faster
   screen (Ant's: 157 frames a second, last-run.log) most frames have no tick: the camera and world stand still, and
   the car and puffs move on the frame after the ones that do, so they shake against the world at 60 Hz [verified: real-time
   dumps at 150 fps, frame-to-frame differences: the world, then the car and puffs a frame later, then nothing].
   So a frame waits here for a whole tick since the last. (The timer's own wait, its second argument, spins on Sleep(0) and
   counts from when it last finished: on a beat of its own it spun a little longer every frame, 14 ms a frame by a lap's end.)
   Not in test runs (their clock is a count of frames); the ticks a second are what they were, only the frames with none go. */
enum { T_WORLD, T_WALK, T_LAND, T_FOG, T_SKY, T_SIM, T_N };
static double t_sum[T_N];
static unsigned long puddle_recent;
static double mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}
static int frame_timer_hook(CPU *c)
{
    extern int deterministic;
    static int off = -1;
    if (off < 0)
        off = getenv("ORR_TICK_FRAMES") && !atoi(getenv("ORR_TICK_FRAMES"));
    if (deterministic || off || c->ecx != 0x5d2758u)
        return 0;
    static double last;
    double due = last + 1000.0 / 60.0, now = mono_ms();
    /* a slow frame, always in the log: what Ant's play shows that scripted runs do not */
    if (last > 0 && now - last > 25.0 && frame_count > 600) {
        static int said;
        if (said++ < 400)
            LOG(LOG_ALWAYS, "slow frame %lu: %.1f ms%s", frame_count, now - last, puddle_recent + 30 > frame_count ? " (puddle)" : "");
    }
    if (now < due - 1.0) {
        struct timespec ts = {0, (long)((due - 1.0 - now) * 1e6)};
        nanosleep(&ts, NULL);
    }
    while (mono_ms() < due)
        __builtin_ia32_pause();
    last = mono_ms();
    return 0;
}

/* ORR_RACE_LOOK=<time>[_<weather>] (tools/dev/race.sh): the race's time of day and weather whatever the menus chose,
   day, sunset or night, rain or snow: +0, +4 of the settings [verified: 0x4d9c30] */
static int level_load_hook(CPU *c)
{
    const char *l = getenv("ORR_RACE_LOOK");
    if (l) {
        uint32_t st = R32(c->esp + 4);
        W32(st, strstr(l, "snow") ? 1u : strstr(l, "rain") ? 3u : 0u);
        W32(st + 4, strstr(l, "sunset") ? 2u : strstr(l, "night") ? 3u : 1u);
    }
    return 0;
}

/* The terrain's polygon records, 16 bytes each, are in the object itself: +0x54 up to +0x40054, which is the cursor, and
   nothing checks (0x53f680 only adds, 0x54c0c0 resets to +0x54; readers follow pointers stored per polygon [verified:
   decompile]). 16384 polygons were never reached at 260 m. At Extreme they run over the object's other fields: Ant's crash in
   0x540260 on a pointer from a list [verified: last-run.log]. So the cursor starts in a larger block of our own. */
static int terrain_begin_hook(CPU *c)
{
    static uint32_t pool;
    static int plog = -1;
    enum { POOL = 16 << 20 };
    uint32_t t = c->ecx;
    if (plog < 0)
        plog = getenv("ORR_POOL_LOG") != NULL;
    if (plog && pool && frame_count % 200 == 0)
        LOG(LOG_ALWAYS, "terrain: frame %lu polygons %u vertices %d %d", frame_count, (R32(t + 0x40054) - pool) / 16, (int)(int16_t)R16(t + 0x50), (int)(int16_t)R16(t + 0x52));
    if (!pool)
        pool = galloc(POOL);
    F_0054c0c0_orig(c);
    W32(t + 0x40054, pool);
    return 1;
}

static int car_available_hook(CPU *c)
{
    if (!getenv("ORR_UNLOCK_ALL"))
        return 0;
    api_return(c, 1, 0);
    return 1;
}

/* A splash is 4 parts of 16 see-through sheets, of flecks over mostly nothing, about 72 splashes a car through a puddle: 25 screens
   of overdraw, and the CPU clipping each sheet. Only one part of each is drawn now, chosen for the splash's life so nothing
   flickers, and the renderer makes its flecks 4 times as strong (vk.c), so the look holds. The splash's life, its random numbers and
   its sound are the game's: only the drawing changes. Part: [+0xa0] + i * 0xf4 its live byte [verified: 0x4a2630, a first go took +0xa0 for the array and crashed]. */
static int splash_draw_hook(CPU *c)
{
    uint32_t o = c->ecx;
    {
        /* (reactive puddles throw their own) */
        extern int puddle_active(void);
        if (puddle_active()) {
            api_return(c, 1, 1);
            return 1;
        }
    }
    int keep = (int)(((o >> 4) * 2654435761u) >> 30);
    uint32_t parts = R32(o + 0xa0);
    uint8_t live[4];
    for (int i = 0; i < 4; i++) {
        live[i] = R8(parts + (uint32_t)i * 0xf4);
        if (i != keep)
            W8(parts + (uint32_t)i * 0xf4, 0);
    }
    F_004a2630_orig(c);
    for (int i = 0; i < 4; i++)
        W8(parts + (uint32_t)i * 0xf4, live[i]);
    return 1;
}

/* (timing only) */
static int puddle_hit_hook(CPU *c)
{
    double t = mono_ms();
    F_0049fe30_orig(c);
    if (frame_count > puddle_recent + 30)
        LOG(LOG_ALWAYS, "puddle hit at frame %lu: %.2f ms to make the splash", frame_count, mono_ms() - t);
    puddle_recent = frame_count;
    return 1;
}

/* Streamer mode: the paint jobs with the Confederate flag load the car's plain paint instead, for everyone in the race and
   in the menus' previews. bugNN is the car, pjK the paint, d and m its dirty and muddy versions [verified: the rip's cars.json,
   each of these looked at]: Blackfoot 3, 4; Comanche 3, 4; Bronco 3; Polecat 3; Firesole 5. */
static int texture_name_hook(CPU *c)
{
    if (!streamer_on)
        return 0;
    static const char *const flagged[] = {"bug15", "3", "bug15", "4", "bug05", "3", "bug05", "4", "bug14", "3", "bug20", "3", "bug17", "5"};
    uint32_t a = R32(c->esp + 4);
    const char *n = a ? gstr(a) : NULL;
    if (!n)
        return 0;
    const char *base = n;
    for (const char *p = n; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    const char *pj = strcasestr(base, "pj");
    if (!pj || !pj[2] || strncasecmp(base, "bug", 3))
        return 0;
    for (unsigned i = 0; i + 1 < sizeof flagged / sizeof *flagged; i += 2)
        if (!strncasecmp(base, flagged[i], 5) && pj[2] == flagged[i + 1][0]) {
            char o[256];
            size_t head = (size_t)(pj - n);
            snprintf(o, sizeof o, "%.*s%s", (int)head, n, pj + 3);
            static uint32_t buf;
            if (!buf)
                buf = galloc(256);
            gstrncpy(buf, o, 256);
            W32(c->esp + 4, buf);
            LOG(LOG_ALWAYS, "streamer mode: %s for %s", o, n);
            return 0;
        }
    return 0;
}

static int deterministic_run(void)
{
    extern int deterministic;
    return deterministic;
}

/* Frames between ticks. The game draws what its last update made and steps at 60 a second, with no interpolation of its own
   [verified: 0x4d6be0, 0x4d7690, 0x4db240]. So before each normal frame (which shows the tick as it is, the game's picture) come
   draw-only frames: camera, cars and wheels put between the last two ticks for the drawing, and put back; the update does nothing
   in them, and the times it hands the drawing are zero. The picture is up to a tick behind the game for it. Ticks stay the game's:
   the update and frame_timer_hook are untouched. Not in test runs, two-player races, or the race's menus. */
#define INT_OBJS 64
typedef struct { float prev[15], cur[15]; uint32_t o; int have; } IntObj;
static IntObj ints[INT_OBJS];
static int nints;
static float cam_prev[16], cam_cur[16];
static uint32_t cam_seen;
static int draw_only;
static uint32_t clip_saved[6];
static double t_tick;
static float int_alpha = 1.0f, tick_t;
/* how long a draw-only frame takes, watched so one never makes the normal frame late */
static double int_frame_ms = 6.0;
static void obj_read(uint32_t o, float *m)
{
    for (int i = 0; i < 15; i++)
        m[i] = RF32(o + 0x24 + 4 * (uint32_t)i);
}
static void obj_write(uint32_t o, const float *m)
{
    for (int i = 0; i < 15; i++) {
        union { float f; uint32_t u; } u = {m[i]};
        W32(o + 0x24 + 4 * (uint32_t)i, u.u);
    }
}
static const uint32_t cam_at[4] = {0x4, 0x14, 0x24, 0x44};
static void cam_read(uint32_t cam, float *m)
{
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++)
            m[i * 4 + k] = RF32(cam + cam_at[i] + 4 * (uint32_t)k);
}
static void cam_write(uint32_t cam, const float *m)
{
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) {
            union { float f; uint32_t u; } u = {m[i * 4 + k]};
            W32(cam + cam_at[i] + 4 * (uint32_t)k, u.u);
        }
    /* (the matrices made from them, each cached until cleared [verified: 0x4f03b0, 0x4f0cb0]) */
    W8(cam + 0x1d0, 0);
    W8(cam + 0xcc, 0);
}
/* rows blended and made unit length again, the position blended; (a cut, a respawn: no blend) */
static void mix_rows(float *o, const float *a, const float *b, float t, int rows)
{
    for (int r = 0; r < rows; r++) {
        float x = a[r * 4] + (b[r * 4] - a[r * 4]) * t, y = a[r * 4 + 1] + (b[r * 4 + 1] - a[r * 4 + 1]) * t, z = a[r * 4 + 2] + (b[r * 4 + 2] - a[r * 4 + 2]) * t;
        float l = sqrtf(x * x + y * y + z * z);
        float want = sqrtf(b[r * 4] * b[r * 4] + b[r * 4 + 1] * b[r * 4 + 1] + b[r * 4 + 2] * b[r * 4 + 2]);
        l = l > 0.0f ? want / l : 1.0f;
        o[r * 4] = x * l;
        o[r * 4 + 1] = y * l;
        o[r * 4 + 2] = z * l;
        o[r * 4 + 3] = b[r * 4 + 3];
    }
}
static int far_apart(const float *pa, const float *pb, float limit)
{
    float x = pa[0] - pb[0], y = pa[1] - pb[1], z = pa[2] - pb[2];
    return x * x + y * y + z * z > limit * limit;
}
static void int_note_object(uint32_t o)
{
    if (!o || o < 0x10000u)
        return;
    for (int i = 0; i < nints; i++)
        if (ints[i].o == o) {
            memcpy(ints[i].prev, ints[i].cur, sizeof ints[i].prev);
            obj_read(o, ints[i].cur);
            ints[i].have = 1;
            if (far_apart(ints[i].prev + 12, ints[i].cur + 12, 2000.0f))
                memcpy(ints[i].prev, ints[i].cur, sizeof ints[i].prev);
            return;
        }
    if (nints < INT_OBJS) {
        IntObj *e = &ints[nints++];
        e->o = o;
        obj_read(o, e->cur);
        memcpy(e->prev, e->cur, sizeof e->prev);
        e->have = 1;
    }
}
/* at a tick's end: the last two positions of everything that moves smoothly */
static void int_snapshot(void)
{
    extern const uint32_t *light_cars(int *n);
    int n;
    const uint32_t *cars = light_cars(&n);
    for (int i = 0; i < n; i++) {
        int_note_object(cars[i]);
        uint32_t nw = R32(cars[i] + 0x500);
        for (uint32_t w = 0; w < nw && w < 8; w++)
            int_note_object(R32(R32(cars[i] + 0x508) + 4 * w));
    }
    if (cam_seen) {
        memcpy(cam_prev, cam_cur, sizeof cam_prev);
        cam_read(cam_seen, cam_cur);
        if (far_apart(cam_prev + 12, cam_cur + 12, 800.0f))
            memcpy(cam_prev, cam_cur, sizeof cam_prev);
    }
    /* (the renderer's clip rectangle as the update leaves it: the HUD's drawing changes it, and only the update puts it back:
       a draw-only frame without this drew the world in the middle of the picture) */
    for (int i = 0; i < 6; i++)
        clip_saved[i] = R32(0x5d27b8 + 0x3ec + 4 * (uint32_t)i);
    t_tick = mono_ms();
}
void game_level_loaded(void)
{
    nints = 0;
    cam_seen = 0;
}
static int race_update_hook(CPU *c)
{
    if (draw_only) {
        api_return(c, 1, 0);
        return 1;
    }
    double t0 = mono_ms();
    F_004d7690_orig(c);
    t_sum[T_SIM] += mono_ms() - t0;
    if ((int)R32(0x5d1528 + 0x30) > 0) {
        extern void puddle_tick(void);
        puddle_tick();
    }
    if (int_on && !deterministic_run() && (int)R32(0x5d1528 + 0x30) > 0) {
        tick_t = RF32(0x5d1528 + 0x2c);
        int_snapshot();
    }
    return 1;
}
static int race_draw_hook(CPU *c)
{
    extern int pad_is_two_players(void);
    uint32_t state = c->ecx;
    if (draw_only || !int_on || deterministic_run() || pad_is_two_players() || R8(state + 0xfdd) || t_tick <= 0.0 || !cam_seen)
        return 0;
    double slot = 1000.0 / (double)(int_hz > 60 ? int_hz : 120), now = mono_ms();
    uint32_t arg = R32(c->esp + 4);
    uint32_t times[3] = {R32(0x5d1528 + 0x24), R32(0x5d1528 + 0x2c), R32(0x5d1528 + 0x30)};
    /* each shown a slot after the last: started a frame's time early, and placed for when it will show */
    double when = t_tick + slot;
    while (when + 1.0 < t_tick + 1000.0 / 60.0) {
        now = mono_ms();
        if (now < when - int_frame_ms) {
            struct timespec ts = {0, (long)((when - int_frame_ms - now) * 1e6)};
            nanosleep(&ts, NULL);
        } else if (now + int_frame_ms > t_tick + 1000.0 / 60.0) {
            break;
        }
        double t0 = mono_ms();
        int_alpha = (float)((t0 + int_frame_ms - t_tick) * 0.06);
        int_alpha = int_alpha > 1.0f ? 1.0f : int_alpha;
        if (getenv("ORR_INT_ALPHA"))
            int_alpha = (float)atof(getenv("ORR_INT_ALPHA"));
        W32(0x5d1528 + 0x24, 0);
        W32(0x5d1528 + 0x2c, 0);
        W32(0x5d1528 + 0x30, 0);
        draw_only = 1;
        for (int i = 0; i < 6; i++)
            W32(0x5d27b8 + 0x3ec + 4 * (uint32_t)i, clip_saved[i]);
        c->ecx = state;
        guest_call(c, 0x4d6be0, 1, arg);
        guest_call(c, 0x4fc450, 0);
        draw_only = 0;
        W32(0x5d1528 + 0x24, times[0]);
        W32(0x5d1528 + 0x2c, times[1]);
        W32(0x5d1528 + 0x30, times[2]);
        int_frame_ms += (mono_ms() - t0 - int_frame_ms) * 0.1;
        when += slot;
    }
    for (int i = 0; i < 6; i++)
        W32(0x5d27b8 + 0x3ec + 4 * (uint32_t)i, clip_saved[i]);
    int_frame_ms *= 0.998;         /* (one slow one must not stop them for good) */
    int_alpha = 1.0f;
    c->ecx = state;
    return 0;
}
/* the cars and wheels where they were at this draw-only moment: *back says how to put them back */
static void int_apply(int restore)
{
    float mixed[15];
    for (int i = 0; i < nints; i++) {
        IntObj *e = &ints[i];
        if (restore) {
            obj_write(e->o, e->cur);
        } else {
            mix_rows(mixed, e->prev, e->cur, int_alpha, 3);
            for (int k = 0; k < 3; k++)
                mixed[12 + k] = e->prev[12 + k] + (e->cur[12 + k] - e->prev[12 + k]) * int_alpha;
            obj_write(e->o, mixed);
        }
        W32(e->o + 0x68, R32(e->o + 0x68) + 1);    /* (its parts' world matrices are cached against this) */
    }
}

/* The right stick swings the camera round the car, for the drawing only: the stick's place is the angle, right to look
   right, all the way over to look back; up looks down from above. The game's camera is put back after the view, as its
   chase works from where it was. Camera: rows +0x4, +0x14, +0x24 (right, up, forward), position +0x44, the matrix of the
   view cached until +0x1d0 is cleared [verified: 0x4f0c30, 0x4f0cb0]. The player's car: 0x4b15f0 of the view's number
   0x5ce9c0, on [0x5d2550]+0x18, its position +0x54 [verified: 0x497d50, 0x52e300]. Not in a two-player race: both would turn. */
static void turn(uint32_t a, float cy, float sy, float cp, float sp, const float *right)
{
    float v[3] = {RF32(a), RF32(a + 4), RF32(a + 8)};
    float x = v[0] * cy + v[2] * sy, z = -v[0] * sy + v[2] * cy, y = v[1];
    /* then up and over, about the camera's right */
    float d = right[0] * x + right[1] * y + right[2] * z;
    float cx = right[1] * z - right[2] * y, cyy = right[2] * x - right[0] * z, cz = right[0] * y - right[1] * x;
    union { float f; uint32_t u; } o[3] = {{x * cp + cx * sp + right[0] * d * (1 - cp)}, {y * cp + cyy * sp + right[1] * d * (1 - cp)}, {z * cp + cz * sp + right[2] * d * (1 - cp)}};
    for (int i = 0; i < 3; i++)
        W32(a + 4 * (uint32_t)i, o[i].u);
}
/* Fog off (Lighting > Fog, default): from the land on, the fog object says the fog starts at 4 x the far plane, so nothing has any; no
   height mist. Fog object [verified: 0x507020, 0x53eb50]: +0 where it starts, +4 1 / (1 - start), +8 the two's product, +0x24 bit 0 the
   mist. The sky keeps the real values (its horizon comes from them: without them a black band; time_sky_hook). The land's overlay,
   all nothing now, is not drawn. */
static uint32_t fog_kept[4], fog_on_view;
static void fog_write(int off)
{
    uint32_t f = fog_on_view ? R32(fog_on_view + 0x20) : 0;
    if (!f)
        return;
    if (off) {
        union { float v; uint32_t u; } d = {4.0f}, p = {4.0f * RF32(f + 4)};
        W32(f, d.u);
        W32(f + 8, p.u);
        W32(f + 0x24, fog_kept[3] & ~1u);
    } else {
        W32(f, fog_kept[0]);
        W32(f + 8, fog_kept[2]);
        W32(f + 0x24, fog_kept[3]);
    }
}
static void fog_view_begin(uint32_t view)
{
    uint32_t f = R32(view + 0x20);
    fog_on_view = fog_off && f ? view : 0;
    if (fog_on_view) {
        fog_kept[0] = R32(f);
        fog_kept[2] = R32(f + 8);
        fog_kept[3] = R32(f + 0x24);
    }
}
static void fog_view_end(void)
{
    fog_write(0);
    fog_on_view = 0;
}

static int view_draw_hook(CPU *c)
{
    {
        extern void game_time_frame(void);
        if (getenv("ORR_TIME_LOG"))
            game_time_frame();
    }
    extern int pad_axis_value(int axis), pad_is_two_players(void);
    static float yaw, pitch;
    static double stick_t;
    uint32_t vcam = R32(R32(c->esp + 4) + 4);
    fog_view_begin(R32(c->esp + 4));
    if (int_on)
        cam_seen = vcam;        /* (the camera this race draws with: int_snapshot reads it) */
    double st_now = mono_ms();
    float st_dt = stick_t > 0.0 ? (float)(st_now - stick_t) : 16.67f;
    stick_t = st_now;
    float ease = 1.0f - expf(-st_dt * 0.0119f);         /* (0.18 a frame at 60: by time, as frames now vary) */
    float rx = (float)pad_axis_value(2) / 32768.0f, ry = (float)pad_axis_value(3) / 32768.0f;
    float mag = sqrtf(rx * rx + ry * ry);
    if (!stick_camera || pad_is_two_players() || mag < 0.18f)
        rx = ry = 0.0f;
    else {
        rx *= (mag - 0.18f) / 0.82f / mag;
        ry *= (mag - 0.18f) / 0.82f / mag;
    }
    yaw += (rx * 3.14159f - yaw) * ease;
    float want_p = ry < 0.0f ? -ry * 0.75f : -ry * 0.12f;
    pitch += (want_p - pitch) * ease;
    /* a draw-only frame: everything between the last two ticks */
    float cam_mid[16];
    if (draw_only) {
        mix_rows(cam_mid, cam_prev, cam_cur, int_alpha, 3);
        for (int k = 0; k < 4; k++)
            cam_mid[12 + k] = cam_prev[12 + k] + (cam_cur[12 + k] - cam_prev[12 + k]) * int_alpha;
        cam_write(vcam, cam_mid);
        int_apply(0);
    }
    if (fabsf(yaw) < 0.002f && fabsf(pitch) < 0.002f) {
        yaw = pitch = 0.0f;
        if (!draw_only)
            return 0;
        F_004daec0_orig(c);
        fog_view_end();
        cam_write(vcam, cam_cur);
        int_apply(1);
        return 1;
    }
    uint32_t view = R32(c->esp + 4), cam = R32(view + 4), keep = c->ecx;
    uint32_t saved[16];
    static const uint32_t at[4] = {0x4, 0x14, 0x24, 0x44};
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++)
            saved[i * 4 + k] = R32(cam + at[i] + 4 * (uint32_t)k);
    c->ecx = R32(R32(0x5d2550) + 0x18);
    uint32_t car = guest_call(c, 0x4b15f0, 1, R32(0x5ce9c0));
    c->ecx = keep;
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float px = RF32(cam + 0x44), py = RF32(cam + 0x48), pz = RF32(cam + 0x4c);
    float qx = px, qy = py, qz = pz;
    if (car) {
        qx = RF32(car + 0x54);
        qy = RF32(car + 0x58) + 60.0f;
        qz = RF32(car + 0x5c);
    }
    /* (a camera in or on the car turns where it is) */
    if ((px - qx) * (px - qx) + (pz - qz) * (pz - qz) < 300.0f * 300.0f) {
        qx = px;
        qy = py;
        qz = pz;
    }
    /* the right vector after the yaw, for the pitch */
    float r0 = RF32(cam + 4), r2 = RF32(cam + 0xc);
    float right[3] = {r0 * cy + r2 * sy, RF32(cam + 8), -r0 * sy + r2 * cy};
    float rl = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    for (int i = 0; i < 3 && rl > 0.0f; i++)
        right[i] /= rl;
    static uint32_t tmp;
    if (!tmp)
        tmp = galloc(16);
    union { float f; uint32_t u; } u;
    u.f = px - qx; W32(tmp, u.u);
    u.f = py - qy; W32(tmp + 4, u.u);
    u.f = pz - qz; W32(tmp + 8, u.u);
    turn(tmp, cy, sy, cp, sp, right);
    u.f = RF32(tmp) + qx; W32(cam + 0x44, u.u);
    u.f = RF32(tmp + 4) + qy; W32(cam + 0x48, u.u);
    u.f = RF32(tmp + 8) + qz; W32(cam + 0x4c, u.u);
    turn(cam + 0x4, cy, sy, cp, sp, right);
    turn(cam + 0x14, cy, sy, cp, sp, right);
    turn(cam + 0x24, cy, sy, cp, sp, right);
    W8(cam + 0x1d0, 0);
    W8(cam + 0xcc, 0);
    F_004daec0_orig(c);
    fog_view_end();
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++)
            W32(cam + at[i] + 4 * (uint32_t)k, saved[i * 4 + k]);
    W8(cam + 0x1d0, 0);
    W8(cam + 0xcc, 0);
    if (draw_only) {
        cam_write(vcam, cam_cur);
        int_apply(1);
    }
    return 1;
}

/* ORR_TIME_LOG=1: ms per frame of each piece, over the last 300 frames */
#define TIMED(name, id, fn)                                     \
    static int name(CPU *c)                                     \
    {                                                           \
        double t0 = mono_ms();                                  \
        fn(c);                                                  \
        t_sum[id] += mono_ms() - t0;                            \
        return 1;                                               \
    }
/* (the land and what follows: the sky dome before it takes its colours from the fog object) */
static int time_world_hook(CPU *c)
{
    double t0 = mono_ms();
    fog_write(1);
    F_0054ea20_orig(c);
    t_sum[T_WORLD] += mono_ms() - t0;
    return 1;
}
TIMED(time_walk_hook, T_WALK, native_walk_call)
TIMED(time_land_hook, T_LAND, native_land_call)
static int time_fog_hook(CPU *c)
{
    if (fog_on_view) {
        api_return(c, 1, 1);
        return 1;
    }
    double t0 = mono_ms();
    native_fog_call(c);
    t_sum[T_FOG] += mono_ms() - t0;
    return 1;
}
/* The clouds fan out to 3 x the far plane, split until no edge is over 3200 units [verified: 0x4ea330, 0x4e8250], so their
   CPU cost goes with the far plane squared, and they end in the fog anyway: drawn for the game's own far plane. */
static int time_sky_hook(CPU *c)
{
    double t0 = mono_ms();
    uint32_t cam = R32(R32(c->esp + 4) + 4), far_now = R32(cam + 0x250);
    union { float f; uint32_t u; } g = {RF32(cam + 0x250) / far_scale()};
    W32(cam + 0x250, g.u);
    fog_write(0);
    F_004ea330_orig(c);
    fog_write(1);
    W32(cam + 0x250, far_now);
    t_sum[T_SKY] += mono_ms() - t0;
    return 1;
}
void game_time_frame(void)
{
    static unsigned long n;
    if (++n % 300 == 0) {
        LOG(LOG_ALWAYS, "time: frame %lu sim %.2f world %.2f (walk %.2f land %.2f fog %.2f) sky %.2f", frame_count, t_sum[T_SIM] / 300, t_sum[T_WORLD] / 300,
            t_sum[T_WALK] / 300, t_sum[T_LAND] / 300, t_sum[T_FOG] / 300, t_sum[T_SKY] / 300);
        memset(t_sum, 0, sizeof t_sum);
    }
}

static int puff_draw_hook(CPU *c)
{
    if (!dust_on || !vk_on())
        return 0;
    uint32_t o = c->ecx, vt = R32(o);
    int exhaust = vt == 0x57abccu;       /* the car's Exhaust objects, 0x491070; the others the wheels' (surface colours) */
    if ((!exhaust && vt != 0x57ad34u) || !R8(o + 0x50))
        return 0;
    /* water's spray is hundreds of these at once, screen-filling (285 in a puddle against about 20 on dirt; 24 ms a frame on the
       890M at 1365 x 768, four times that at its native size: Ant's slowdown in water), and is meant as droplets. It stays
       the game's sprite. Its colour is fixed: 0.9, 0.9, 0.94 [verified: 0x4a42e0, +0x474 == 1] */
    if (!exhaust && (R32(o + 0x48) & 0xffffffu) == 0xe6e6f0u)
        return 0;
    extern const D3DState *d3d_last_state(void);
    const D3DState *ds = d3d_last_state();
    if (!ds)
        return 0;
    W8(o + 0x13, 0);                     /* (as the draw does, first thing, 0x53c28d) */
    uint32_t renderer = R32(c->esp + 4), cam = R32(renderer + 4), keep = c->ecx;
    static uint32_t buf;
    if (!buf)
        buf = galloc(64);
    c->ecx = cam;
    uint32_t mp = guest_call(c, 0x4f0cb0, 1, buf);
    c->ecx = keep;
    float m[16];
    for (int i = 0; i < 16; i++)
        m[i] = RF32(mp + 4 * (uint32_t)i);
    /* the camera's matrix is view and projection together: column 3 the eye depth, columns 0, 1 the screen's
       x, y before the divide [verified: 0x53c2ab; a first go that took it for a view matrix put puffs 100000 away] */
    float f = RF32(cam + 0x150), cx = RF32(cam + 0x25c), cy = RF32(cam + 0x260);
    float x = RF32(o + 0x2c), y = RF32(o + 0x30), z = RF32(o + 0x34);
    if (draw_only && !exhaust) {        /* (a puff moves by its velocity, a draw-only frame is before the tick's end) */
        float back = (1.0f - int_alpha) * tick_t;
        x -= back * RF32(o + 0x54);
        y -= back * RF32(o + 0x58);
        z -= back * RF32(o + 0x5c);
    }
    float vz = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (!(vz > 1.0f) || !(f > 0.0f)) {
        api_return(c, 1, 1);
        return 1;
    }
    float vx = ((m[0] * x + m[4] * y + m[8] * z + m[12]) / vz - cx) / f * vz,
          vy = (cy - (m[1] * x + m[5] * y + m[9] * z + m[13]) / vz) / f * vz;
    /* the size the game gives it: both grow by themselves (0x417e10, 0x419070 +0x68) */
    float hw = RF32(o + 0x3c), hh = RF32(o + 0x40);
    uint32_t alpha = R32(o + 0x48) >> 24;
    float age = (1.0f - (float)alpha / 255.0f) * 100.0f;
    static D3DState st;
    st = *ds;
    float k = widescreen_on() ? ws_aspect / (4.0f / 3.0f) : 1.0f;
    memset(&st.xf[XF_WORLD], 0, sizeof st.xf[XF_WORLD]);
    memset(&st.xf[XF_VIEW], 0, sizeof st.xf[XF_VIEW]);
    memset(&st.xf[XF_PROJ], 0, sizeof st.xf[XF_PROJ]);
    for (int i = 0; i < 4; i++)
        st.xf[XF_WORLD].m[i][i] = st.xf[XF_VIEW].m[i][i] = 1.0f;
    st.xf[XF_PROJ].m[0][0] = 1.25f / k;
    st.xf[XF_PROJ].m[1][1] = 1.25f * 4.0f / 3.0f;
    st.xf[XF_PROJ].m[2][2] = RF32(cam + 0x178);
    st.xf[XF_PROJ].m[2][3] = 1.0f;
    st.xf[XF_PROJ].m[3][2] = RF32(cam + 0x188);
    st.tex[0] = NULL;
    ws_world_aspect = st.xf[XF_PROJ].m[1][1] / st.xf[XF_PROJ].m[0][0];
    static unsigned long begun = ~0ul;
    if (begun != frame_count) {
        begun = frame_count;
        /* towards the sun, high and behind the start [guess: the track's own sun is not read yet] */
        const float s[3] = {0.35f, 0.85f, 0.39f};
        float fw = m[3] * s[0] + m[7] * s[1] + m[11] * s[2];
        VkSmokeView v = {{(m[0] * s[0] + m[4] * s[1] + m[8] * s[2] - cx * fw) / f,
                          (cy * fw - (m[1] * s[0] + m[5] * s[1] + m[9] * s[2])) / f, fw}, {1.0f, 0.96f, 0.88f}, 2};
        vk_smoke_view_begin(&v);
    }
    VkPuff p = {exhaust ? VK_PUFF_EXHAUST : VK_PUFF_DUST, o >> 3, 1.0f, age, 0.85f, -1e9f, -hh};
    const float q[4][3] = {{vx - hw, vy + hh, vz}, {vx + hw, vy + hh, vz}, {vx - hw, vy - hh, vz}, {vx + hw, vy - hh, vz}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    vk_puff = &p;
    /* the tailpipe fades by darkening its grey (0x417a30 makes +0x48 from the colour +0x7c..+0x84 times +0x98): as
       a volume that is its alpha, and thin: a light grey haze. The wheels' keep the game's surface colour */
    uint32_t diffuse = R32(o + 0x48);
    if (exhaust) {
        float most = RF32(o + 0x7c), got = (float)(diffuse >> 16 & 255);
        float left = most > 0.01f ? got / (most * 255.0f) : 0.0f;
        left = left > 1.0f ? 1.0f : left;
        /* (eases in as it starts to fade: a new one is at full strength) */
        float in = (1.0f - left) * 8.0f;
        in = in > 1.0f ? 1.0f : in;
        diffuse = (uint32_t)((in * 0.5f + 0.5f) * left * 0.22f * 255.0f) << 24 | 0x9a9a9au;
    } else {
        /* A wheel's puff is born whole, at its first size and strength, and removed while it still has some (0x419070: when
           +0x70 falls under +0x78). As a sprite that hardly showed; as a volume, at speed, with the car leaving each one
           behind in a few frames, lumps came and went about 15 times a second: Ant's smoke flicker [verified: ORR_SMOKE_DEBUG=1
           dumps, consecutive frames]. So it grows into its strength in a few ticks (its age from its size: 64 at birth, +0x68 a
           tick) and is gone by the time the game removes it. A slower ramp, 10 ticks, cost the dirt most of its look (Ant). */
        float rate = RF32(o + 0x68), left = RF32(o + 0x70) - RF32(o + 0x78);
        float in = rate > 0.01f ? (hw - 64.0f) / rate / 4.0f : 1.0f;
        float out = left / 0.12f;
        in = in < 0.0f ? 0.0f : in > 1.0f ? 1.0f : in;
        out = out < 0.0f ? 0.0f : out > 1.0f ? 1.0f : out;
        diffuse = (uint32_t)((float)(diffuse >> 24) * in * out) << 24 | (diffuse & 0xffffffu);
    }
    /* (grow 1: the renderer's size history is by serial, and the game reuses objects' addresses: 0 showed a new puff
       at the size of the one before, a full-screen haze for a frame [verified: frames 4008-4010]) */
    /* a puff at the camera: nothing to see into, so gone before it fills the screen */
    float nearby = (vz - hw) / (2.0f * hw + 200.0f);
    nearby = nearby < 0.0f ? 0.0f : nearby > 1.0f ? 1.0f : nearby;
    diffuse = (uint32_t)((float)(diffuse >> 24) * nearby) << 24 | (diffuse & 0xffffffu);
    vk_smoke_take(&st, q, 4, diffuse, uv);
    vk_puff = NULL;
    api_return(c, 1, 1);
    return 1;
}

/* The terrain's walk, land and fog: native_land.c does the whole call, or calls the original */
static int walk_hook(CPU *c) { native_walk_call(c); return 1; }
static int land_hook(CPU *c) { native_land_call(c); return 1; }
static int fog_hook(CPU *c) { native_fog_call(c); return 1; }

void game_init(void)
{
    if (!widescreen_allowed())
        return;
    native_land_init();
    hook_terrain_walk = walk_hook;
    hook_terrain_land = land_hook;
    hook_terrain_fog = fog_hook;
    hook_puff_draw = puff_draw_hook;
    hook_frame_timer = frame_timer_hook;
    extern int puddle_draw_hook(CPU *c), puddle_sky_hook(CPU *c);
    hook_puddle_draw = puddle_draw_hook;
    hook_puddle_sky = puddle_sky_hook;
    hook_race_update = race_update_hook;
    hook_race_draw = race_draw_hook;
    hook_splash_draw = splash_draw_hook;
    hook_time_sky = time_sky_hook;
    if (getenv("ORR_TIME_LOG")) {
        hook_time_world = time_world_hook;
        hook_terrain_walk = time_walk_hook;
        hook_terrain_land = time_land_hook;
        hook_terrain_fog = time_fog_hook;
    }
    hook_view_draw = view_draw_hook;
    extern int light_car_hook(CPU *c);
    hook_car_draw = light_car_hook;
    hook_car_available = car_available_hook;
    hook_texture_name = texture_name_hook;
    hook_puddle_hit = puddle_hit_hook;
    hook_terrain_begin = terrain_begin_hook;
    hook_level_load = level_load_hook;
    extern int light_terrain_hook(CPU *c);
    hook_terrain_light = light_terrain_hook;
    game_options();
}
