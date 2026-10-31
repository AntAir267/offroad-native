/* Off-Road's own fixes and hooks (builds/orr_zoom.py HOOKS).

   Draw distance: VIDEO's Draw Distance row picks a far plane of 16000, 21000 or 26000 at the race's load
   (0x4d8630: 0x5d0e78), which the cameras take from there (0x4a54b0 from the track's camera file, 0x4d94b0 the
   race's own) [verified: decompile]. The More Options row raises what they read. Graphics only: what the
   game's physics and AI see does not use it [guess: to be checked against a run]. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"
#include "gfx.h"
#include "vk.h"

static int far_plane_hook(CPU *c)
{
    (void)c;
    const char *v = option_str("draw_distance", "far");
    float k = !strcasecmp(v, "far") ? 1.5f : !strcasecmp(v, "extreme") ? 2.5f : 1.0f;
    static float base;
    float now = RF32(0x5d0e78);
    if (now != base * 1.5f && now != base * 2.5f)
        base = now;                      /* (the game set it afresh for this race) */
    union { float f; uint32_t u; } u = {base * k};
    W32(0x5d0e78, u.u);
    return 0;
}

/* Volumetric dust. The game's puffs are objects of a sprite class whose draw (0x53c260) makes one camera-facing
   quad: +0x2c world position, +0x3c, +0x40 half width and height, +0x48 colour (ARGB, its alpha fades), +0x50 shown,
   +0x64 age [verified: 0x53c0b0, 0x417a30, disassembly of 0x53c260]. The wheels' is 0x57abcc (0x49132d, over 1000 in a
   lap on dirt), the exhausts' 0x57acf4 (0x4bc717) [verified: spawn log]. With the option on, a puff of these
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
    int exhaust = vt == 0x57acf4u;
    if ((vt != 0x57abccu && !exhaust) || !R8(o + 0x50))
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
    /* its sprite is about 30 units (30 cm) across, wisps as a volume: a cloud grows as it ages. Graphics only */
    static float grow = -1.0f, spread;
    if (grow < 0.0f) {
        const char *e = getenv("ORR_DUST_GROW"), *e2 = getenv("ORR_DUST_START");
        grow = e ? (float)atof(e) : 0.12f;
        spread = e2 ? (float)atof(e2) : 2.0f;
    }
    float age = RF32(o + 0x64);
    float size = (exhaust ? 1.0f : spread + grow * (age > 0.0f ? age : 0.0f));
    float hw = RF32(o + 0x3c) * size, hh = RF32(o + 0x40) * size;
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
    VkPuff p = {exhaust ? VK_PUFF_EXHAUST : VK_PUFF_DUST, o >> 3, 0.0f, RF32(o + 0x64), 0.85f, -1e9f, -hh};
    const float q[4][3] = {{vx - hw, vy + hh, vz}, {vx + hw, vy + hh, vz}, {vx - hw, vy - hh, vz}, {vx + hw, vy - hh, vz}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    vk_puff = &p;
    /* the wheels' fade by darkening their colour (0x417a30 makes +0x48 from +0x7c..+0x84, the colour, times
       +0x98, what is left): as a volume that is its alpha */
    uint32_t diffuse = R32(o + 0x48);
    if (!exhaust) {
        float r = RF32(o + 0x7c), gr = RF32(o + 0x80), bl = RF32(o + 0x84), most = r > gr ? (r > bl ? r : bl) : (gr > bl ? gr : bl);
        float got = (float)((diffuse >> 16 & 255) > (diffuse >> 8 & 255) ? ((diffuse >> 16 & 255) > (diffuse & 255) ? diffuse >> 16 & 255 : diffuse & 255)
                                                                         : ((diffuse >> 8 & 255) > (diffuse & 255) ? diffuse >> 8 & 255 : diffuse & 255));
        float left = most > 0.01f ? got / (most * 255.0f) : 0.0f;
        left = left > 1.0f ? 1.0f : left;
        /* the sprite's grey (0xff4f4f4f, 0x491070) is what its texture is tinted by: a volume has no texture, so
           the earth's colour instead [guess: one colour for every surface; the track's is not read yet] */
        (void)r; (void)gr; (void)bl;
        diffuse = (uint32_t)(left * 255.0f) << 24 | 0x94785au;
    }
    vk_smoke_take(&st, q, 4, diffuse, uv);
    vk_puff = NULL;
    api_return(c, 1, 1);
    return 1;
}

void game_init(void)
{
    if (!widescreen_allowed())
        return;
    hook_cameras_file = far_plane_hook;
    hook_race_cameras = far_plane_hook;
    hook_puff_draw = puff_draw_hook;
    game_options();
}
