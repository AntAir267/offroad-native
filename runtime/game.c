/* Off-Road's own fixes and hooks (builds/orr_zoom.py HOOKS).

   Draw distance: VIDEO's Draw Distance row picks a far plane of 16000, 21000 or 26000 at the race's load
   (0x4d8630: 0x5d0e78), which the cameras take from there [verified: decompile]. The More Options row raises how
   far they cull: game_camera_set. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"
#include "gfx.h"
#include "vk.h"

/* A camera's far plane, +0x250, comes through 0x4f1350 from the race's setup and again from the camera's own stored
   values, so a raised one comes back in. 0x4f1350 makes its depth mapping (+0x178, +0x188) from it, and the sky is
   drawn at a fixed depth that expects the game's (raising 0x5d0e78 outright put the clouds over the banks
   [verified: frame 4800]). So the mapping is always made from the game's number, and what culls is raised:
   game_far_base, game_far (widescreen.c viewport_hook). Only numbers the game's own range can give are raised. */
#define GAME_FAR_MAX 26000.0f
static float far_scale(void)
{
    const char *v = option_str("draw_distance", "far");
    return !strcasecmp(v, "far") ? 1.5f : !strcasecmp(v, "extreme") ? 2.5f : 1.0f;
}
float game_far_base(float far)
{
    return far > GAME_FAR_MAX ? GAME_FAR_MAX : far;
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
static int dust_on;

void game_options(void)
{
    dust_on = widescreen_allowed() && !strcasecmp(option_str("volumetric_dust", "on"), "on");
}

static int puff_draw_hook(CPU *c)
{
    if (!dust_on || !vk_on())
        return 0;
    uint32_t o = c->ecx, vt = R32(o);
    int exhaust = vt == 0x57abccu;       /* the car's Exhaust objects, 0x491070; the others the wheels' (surface colours) */
    if ((!exhaust && vt != 0x57ad34u) || !R8(o + 0x50))
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
        diffuse = (uint32_t)(left * 0.22f * 255.0f) << 24 | 0x9a9a9au;
    }
    /* (grow 1: the renderer's size history is by serial, and the game reuses objects' addresses: 0 showed a new puff
       at the size of the one before, a full-screen haze for a frame [verified: frames 4008-4010]) */
    /* a puff at the camera: nothing to see into, so gone before it fills the screen */
    float nearby = (vz - hw) / (hw + 60.0f);
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
    game_options();
}
