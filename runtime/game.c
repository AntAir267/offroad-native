/* Off-Road's own fixes and hooks (builds/orr_zoom.py HOOKS).

   Draw distance: VIDEO's Draw Distance row picks a far plane of 16000, 21000 or 26000 at the race's load
   (0x4d8630: 0x5d0e78), which the cameras take from there [verified: decompile]. The More Options row raises how
   far they cull: game_camera_set. */
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
static int dust_on, streamer_on;

void game_options(void)
{
    dust_on = widescreen_allowed() && !strcasecmp(option_str("volumetric_dust", "on"), "on");
    streamer_on = !strcasecmp(option_str("streamer_mode", "off"), "on");
    extern void light_options(void), mix_options(void);
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

/* (timing only) */
static int puddle_hit_hook(CPU *c)
{
    /* One splash in three: the game makes one per wheel per tick, and with what the sheets cost it to clip and us to draw the
       frame went over its time, with Extreme worst (game CPU 12 ms, 21 in the spray) [verified: frame-trace.txt, Ant's play] */
    static unsigned every;
    if (every++ % 3) {
        puddle_recent = frame_count;
        api_return(c, 0, 6);
        return 1;
    }
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

void game_init(void)
{
    if (!widescreen_allowed())
        return;
    hook_puff_draw = puff_draw_hook;
    hook_frame_timer = frame_timer_hook;
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
