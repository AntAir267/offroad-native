/* Water simulation (runtime/water.c, vk.c vkr_water_*): shallow water on a staggered grid, one step a game tick, written from the
   general method. A cell 25 game units (cm), its image coordinates the world's cell number modulo the image's size, so nothing is copied
   when the simulated area moves. Images: A r depth, g ground, b rest level (where water comes back to; -1e9 none), a wet and foam packed;
   V r flow across the cell's +x side, g across its +z side (cm/s). Pass 0: the flow, from the slope of the surface, with friction and
   the cars' wheels. Pass 1: the depth, from what crosses each side: what leaves one cell enters the next, so the volume is kept
   exactly; a side's flow is limited to a quarter of a cell a step, so no cell can give more than it has. Then, apart from that step, each
   puddle's slow refill and the thin film's drain. Pass 2: the scene's distance, for the reflections. */
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(std430, set = 0, binding = 6) readonly buffer LightBlock { vec4 v[]; } lb;
layout(push_constant) uniform PC {
    ivec4 a;        /* x A, y V, z pass, w base of the cars in the block */
    ivec4 r;        /* x0, z0, size x, size z of the simulated cells, world cell numbers */
    ivec4 p;        /* the same a step ago (size 0: none) */
    ivec4 n;        /* image size, cars, puddles, sampler */
    vec4 ht;        /* height map: lowest, range, cell, slot */
    vec4 m;         /* game ticks, lake (1: below 0 is the lake), recover (1), z's slot; pass 2: a, b of z = a + b / w, slot, _ */
} pc;
layout(location = 0) out vec4 o;

const float DX = 25.0, DT = 1.0 / 60.0, G = 980.0, HMAX = 200.0, UMAX = DX / (4.0 * DT), DRY = 0.05;

#define S sampler2D(textures[pc.a.x], samplers[pc.n.w])
#define SV sampler2D(textures[pc.a.y], samplers[pc.n.w])

ivec2 mask = ivec2(pc.n.x - 1);
vec4 fa(ivec2 c) { return texelFetch(S, c & mask, 0); }
vec2 fv(ivec2 c) { return texelFetch(SV, c & mask, 0).rg; }
bool inr(ivec4 q, ivec2 c) { return c.x >= q.x && c.y >= q.y && c.x < q.x + q.z && c.y < q.y + q.w; }
/* a cell that has a state: simulated now and a step ago */
bool live(ivec2 c) { return inr(pc.r, c) && inr(pc.p, c); }

float hash2(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float vnoise(vec2 p)
{
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);
}

float ground_at(vec2 w)
{
    int h = int(pc.ht.w), smp = pc.n.w;
    vec2 cf = w / pc.ht.z;
    ivec2 ci = ivec2(floor(cf));
    ivec2 ts = textureSize(sampler2D(textures[h], samplers[smp]), 0);
    vec2 fr = cf - vec2(ci);
    ivec2 c0 = clamp(ci, ivec2(0), ts - 2);
    float h00 = texelFetch(sampler2D(textures[h], samplers[smp]), c0, 0).r,
          h10 = texelFetch(sampler2D(textures[h], samplers[smp]), c0 + ivec2(1, 0), 0).r,
          h11 = texelFetch(sampler2D(textures[h], samplers[smp]), c0 + ivec2(1, 1), 0).r,
          h01 = texelFetch(sampler2D(textures[h], samplers[smp]), c0 + ivec2(0, 1), 0).r;
    float hg = fr.x >= fr.y ? h00 + (h10 - h00) * fr.x + (h11 - h10) * fr.y : h00 + (h11 - h01) * fr.x + (h01 - h00) * fr.y;
    return pc.ht.x + hg * pc.ht.y;
}

/* The block at w: a car 6 x vec4: pos, half reach; velocity (cm/s); wheels 4 x (xyz, r). Then 8 puddles x 2: cx, cz, hx, hz; level. */
const int CAR = 6;
const uint PUDS = 72u;

/* what a car's wheels do at the middle of a side: how far they press the surface down, and the water they carry along */
void wheels(vec2 w, float surface, inout float press, inout vec2 carry, inout float hold)
{
    uint b = uint(pc.a.w);
    for (int i = 0; i < pc.n.y; i++) {
        uint q = b + uint(i * CAR);
        vec4 cp = lb.v[q];
        if (abs(w.x - cp.x) > cp.w || abs(w.y - cp.z) > cp.w)
            continue;
        vec4 cv = lb.v[q + 1u];
        vec2 vel = cv.xz;
        float sp = length(vel);
        /* from where the wheel was last tick to where it is: at speed it crosses several cells a tick */
        vec2 stp = vel * DT;
        for (uint k = 0u; k < 4u; k++) {
            vec4 wh = lb.v[q + 2u + k];
            if (wh.y - wh.w > surface + 20.0)
                continue;
            vec2 d = w - wh.xz;
            float t = dot(stp, stp) > 1.0 ? clamp(dot(d, -stp) / dot(stp, stp), 0.0, 1.0) : 0.0;
            vec2 off = d + stp * t;
            float dd = dot(off, off), r = max(wh.w * 0.8, 28.0);
            if (dd > 9.0 * r * r)
                continue;
            float f = exp(-dd / (r * r));
            float sf = min(sp / 800.0, 1.0);
            press += f * (6.0 + 34.0 * sf);
            /* out from the wheel's line, and along with it */
            vec2 outw = off / max(sqrt(dd), 1.0);
            carry += f * (outw * (120.0 + 0.45 * min(sp, 2500.0)) + vel * 0.08);
            hold = max(hold, f * (0.25 + 0.6 * sf));
        }
    }
}

float side(float hl, float gl, float hr, float gr, float u, float dp)
{
    float el = hl + gl, er = hr + gr;
    if (hl <= DRY && hr <= DRY)
        return 0.0;
    if (hr <= DRY && el <= gr + DRY)
        return 0.0;
    if (hl <= DRY && er <= gl + DRY)
        return 0.0;
    /* past a dry edge the water only sees the ground drop to its own level */
    if (hr <= DRY && gr < el) er = gr;
    if (hl <= DRY && gl < er) el = gl;
    u -= G * DT * clamp((er - el + dp) / DX, -3.0, 3.0);
    float h = max(max(hl, hr), 1.5);
    u /= 1.0 + DT * 0.05 * abs(u) / h;
    return clamp(u * 0.9985, -UMAX, UMAX);
}

float wave(vec2 w)
{
    const float TAU = 6.2831853;
    return 12.5 * sin(TAU * 0.007 * (0.52925 * w.x + pc.m.x)) + 18.75 * sin(TAU * 0.005 * (0.2385 * w.x + 0.465 * w.y + pc.m.x));
}

void main()
{
    if (pc.a.z == 2) {
        vec2 sz = vec2(textureSize(sampler2D(textures[int(pc.m.z)], samplers[pc.n.w]), 0));
        float z = textureLod(sampler2D(textures[int(pc.m.z)], samplers[pc.n.w]), gl_FragCoord.xy / vec2(pc.r.xy), 0.0).r;
        o = vec4(z >= 0.99999 ? 1e7 : pc.m.y / max(z - pc.m.x, 1e-6));
        return;
    }
    ivec2 t = ivec2(gl_FragCoord.xy);
    /* the world cell of this image cell */
    ivec2 c = pc.r.xy + ((t - pc.r.xy) & mask);
    vec2 w = (vec2(c) + 0.5) * DX;
    bool fresh = !inr(pc.p, c);
    if (pc.a.z == 0) {
        if (fresh) {
            o = vec4(0.0);
            return;
        }
        vec4 a = fa(c);
        ivec2 cx = c + ivec2(1, 0), cz = c + ivec2(0, 1);
        vec2 uv = fv(c);
        float u = 0.0, v = 0.0;
        bool lx = live(cx), lz = live(cz);
        vec4 ax = lx ? fa(cx) : a, az = lz ? fa(cz) : a;
        float wetnear = max(a.r, max(ax.r, az.r));
        if (wetnear <= DRY) {
            o = vec4(0.0);
            return;
        }
        /* carried by its own flow, from upstream */
        if (abs(uv.x) + abs(uv.y) > 2.0) {
            ivec2 cu = c + ivec2(uv.x > 0.0 ? -1 : 1, 0), cw = c + ivec2(0, uv.y > 0.0 ? -1 : 1);
            vec2 ux = live(cu) ? fv(cu) : uv, uz = live(cw) ? fv(cw) : uv;
            uv -= DT / DX * (abs(uv.x) * (uv - ux) + abs(uv.y) * (uv - uz));
        }
        float press0 = 0.0, press1 = 0.0, press2 = 0.0, hold = 0.0;
        vec2 carry = vec2(0.0), unused = vec2(0.0);
        float hold2 = 0.0;
        if (pc.n.y > 0) {
            wheels(w, a.r + a.g, press0, carry, hold);
            wheels(w + vec2(DX, 0.0), ax.r + ax.g, press1, unused, hold2);
            wheels(w + vec2(0.0, DX), az.r + az.g, press2, unused, hold2);
        }
        if (lx)
            u = side(a.r, a.g, ax.r, ax.g, uv.x, press1 - press0);
        if (lz)
            v = side(a.r, a.g, az.r, az.g, uv.y, press2 - press0);
        if (hold > 0.0) {
            if (lx) u = clamp(mix(u, carry.x, min(hold, 0.85)), -UMAX, UMAX);
            if (lz) v = clamp(mix(v, carry.y, min(hold, 0.85)), -UMAX, UMAX);
        }
        o = vec4(u, v, 0.0, 0.0);
        return;
    }
    /* pass 1 */
    if (fresh) {
        float g = ground_at(w), rest = -1e9;
        /* each puddle's place, a rough oval, with a low invisible lip: water stays until a wave tops it */
        uint b = uint(pc.a.w) + PUDS;
        float rough = vnoise(w / 260.0) * 0.32 + vnoise(w / 90.0) * 0.1 - 0.2;
        for (int i = 0; i < pc.n.z; i++) {
            vec4 pu = lb.v[b + uint(i) * 2u];
            float lev = lb.v[b + uint(i) * 2u + 1u].x;
            vec2 q = (w - pu.xy) / pu.zw;
            float rr = dot(q, q) + rough;
            if (rr < 1.0 && g < lev)
                rest = lev;
            if (rr < 1.35 && g < lev + 3.0)
                g = mix(g, lev + 3.0, smoothstep(0.82, 1.0, rr) * (1.0 - smoothstep(1.12, 1.35, rr)));
        }
        if (pc.m.y > 0.5 && g < 0.0)
            rest = 0.0;
        o = vec4(max(rest - g, 0.0), g, rest, 0.0);
        return;
    }
    vec4 a = fa(c);
    vec2 uv = texelFetch(SV, t, 0).rg;
    float ql = 0.0, qd = 0.0, qr = 0.0, qu = 0.0;
    float hc = min(a.r, HMAX);
    ivec2 cl = c - ivec2(1, 0), cd = c - ivec2(0, 1), cr = c + ivec2(1, 0), cu = c + ivec2(0, 1);
    /* (what crosses a side is its flow by the depth upstream of it: both cells compute the same number) */
    if (uv.x != 0.0 && live(cr)) qr = uv.x * (uv.x > 0.0 ? hc : min(fa(cr).r, HMAX));
    if (uv.y != 0.0 && live(cu)) qu = uv.y * (uv.y > 0.0 ? hc : min(fa(cu).r, HMAX));
    float ul = live(cl) ? fv(cl).x : 0.0, ud = live(cd) ? fv(cd).y : 0.0;
    if (ul != 0.0) ql = ul * (ul > 0.0 ? min(fa(cl).r, HMAX) : hc);
    if (ud != 0.0) qd = ud * (ud > 0.0 ? min(fa(cd).r, HMAX) : hc);
    float h = a.r + DT / DX * (ql - qr + qd - qu);
    float dh = abs(h - a.r);
    h = max(h, 0.0);
    float rest = a.b, g = a.g;
    if (pc.m.w > 0.5) {
        float eta = h + g;
        if (rest > -1e8) {
            /* back to its level: a seep, a twentieth of what is missing a second, and a little always */
            if (eta < rest)
                h += min(rest - eta, (rest - eta) * 0.05 * DT + 0.01);
        }
        /* a film away from any rest level soaks in */
        if (rest <= -1e8 || eta > rest + 0.5)
            h = max(h - 0.012, 0.0);
    }
    /* the lake at the area's edge: the game's waves, so the two meet and its waves come in */
    if (pc.m.y > 0.5 && rest > -1e8 && g < 0.0) {
        ivec2 e = min(c - pc.r.xy, pc.r.xy + pc.r.zw - 1 - c);
        int ed = min(e.x, e.y);
        if (ed < 24)
            h = mix(h, max(rest + wave(w) * 0.3 - g, 0.0), (24.0 - float(ed)) / 24.0 * 0.1);
    }
    /* wet, foam (one number, for one channel) */
    float wf = a.a;
    float wet = floor(wf) / 4095.0, foam = fract(wf) / 0.99;
    float speed = length(uv) + length(vec2(ul, ud));
    wet = h > 0.5 ? 1.0 : max(wet - 1.0 / (60.0 * 50.0), 0.0);
    float make = clamp((speed - 200.0) / 350.0, 0.0, 1.0) * smoothstep(0.5, 5.0, h) + clamp(dh * 0.35 - 0.25, 0.0, 1.0);
    foam = max(foam * 0.965, clamp(make, 0.0, 1.0));
    if (h <= DRY)
        foam *= 0.8;
    o = vec4(h, g, rest, floor(wet * 4095.0) + foam * 0.99);
}
