/* Water: the moving surface, one vertex every two cells of the simulation's grid (pass 0), raised by the depth and lit from the flow; the
   spray, small quads flung from cells whose water is fast and breaking, chosen by hash with no CPU (pass 1). No vertex buffer. */
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(std430, set = 0, binding = 6) readonly buffer LightBlock { vec4 v[]; } lb;
layout(push_constant) uniform PC {
    mat4 clip;      /* world to clip */
    ivec4 a;        /* x A now, y A a step ago, z V now, w the light block */
    ivec4 r;        /* simulated area: x0, z0, nx, nz cells */
    vec4 c;         /* between the steps 0..1, time, sampler, the picture's copy */
    ivec4 d;        /* image size, pass, scene distance's slot, sun | ambient << 16 */
} pc;
layout(location = 0) out vec3 v_w;
layout(location = 1) out vec3 v_n;
layout(location = 2) out vec3 v_hfw;
layout(location = 3) out vec4 v_flow;

const float DX = 25.0;
#define SA sampler2D(textures[pc.a.x], samplers[int(pc.c.z)])
#define SP sampler2D(textures[pc.a.y], samplers[int(pc.c.z)])
#define SV sampler2D(textures[pc.a.z], samplers[int(pc.c.z)])

float rnd(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return float(x & 0xffffu) / 65536.0;
}

void main()
{
    ivec2 mask = ivec2(pc.d.x - 1);
    if (pc.d.y == 1) {
        uint id = uint(gl_VertexIndex) / 6u, corner = uint(gl_VertexIndex) % 6u;
        const vec2 cs[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
        vec2 q = cs[corner];
        float rate = 1.1 + rnd(id) * 1.2, life = fract(pc.c.y * rate + rnd(id + 91u));
        uint flight = uint(floor(pc.c.y * rate + rnd(id + 91u)));
        ivec2 cell;
        vec2 r2 = vec2(rnd(id * 7u + flight * 131u), rnd(id * 13u + flight * 257u));
        if (pc.d.z > 0) {
            /* (spray: a[1] the cars' block, d[2] how many) most of it round a wheel, where the water is thrown */
            int car = int(id) % pc.d.z;
            vec4 wh = lb.v[uint(pc.a.y) + uint(car) * 6u + 2u + (id & 3u)];
            float ang = r2.x * 6.2831853, rad = 40.0 + r2.y * 170.0;
            cell = ivec2(floor((wh.xz + vec2(cos(ang), sin(ang)) * rad) / DX));
            cell = clamp(cell, pc.r.xy, pc.r.xy + pc.r.zw - 1);
        } else
            cell = pc.r.xy + ivec2(vec2(pc.r.zw) * r2);
        vec4 s = texelFetch(SA, cell & mask, 0);
        vec2 fl = texelFetch(SV, cell & mask, 0).rg;
        float foam = fract(s.a) / 0.99, strength = foam > 0.3 && s.r > 0.3 ? foam : 0.0;
        vec3 start = vec3((vec2(cell) + 0.5) * DX, 0.0).xzy;
        start.y = s.r + s.g;
        /* flung on the water's flow, up, and falling */
        float t = life * 0.6;
        vec3 vel = vec3(fl.x * 1.6, 170.0 + 280.0 * rnd(id + 3u), fl.y * 1.6) + vec3(rnd(id + 5u) - 0.5, 0.0, rnd(id + 9u) - 0.5) * 140.0;
        vec3 p = start + vel * t - vec3(0.0, 490.0 * t * t, 0.0);
        vec4 cp = pc.clip * vec4(p, 1.0);
        float size = strength > 0.0 && cp.w > 300.0 ? (3.0 + 7.0 * life) * (0.7 + rnd(id + 17u)) : 0.0;
        cp.xy += q * size * vec2(pc.clip[0][0] * 0.0 + length(pc.clip[0].xyz), length(pc.clip[1].xyz));
        gl_Position = cp;
        v_w = p;
        v_n = vec3(q, 0.0);
        v_hfw = vec3(strength * (1.0 - life), 0.0, 0.0);
        v_flow = vec4(0.0);
        return;
    }
    const int STEP = 2;
    int nx = pc.r.z / STEP, quad = gl_VertexIndex / 6, corner = gl_VertexIndex % 6;
    const ivec2 cs[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));
    ivec2 cell = pc.r.xy + (ivec2(quad % nx, quad / nx) + cs[corner]) * STEP;
    cell = min(cell, pc.r.xy + pc.r.zw - 1);
    vec4 n0 = texelFetch(SA, cell & mask, 0), n1 = texelFetch(SP, cell & mask, 0);
    float h = mix(n1.r, n0.r, pc.c.x), g = n0.g, eta = g + h;
    /* the slope, from the cells next to it (a dry one has no level of its own) */
    ivec2 lo = pc.r.xy, hi = pc.r.xy + pc.r.zw - 1;
    vec4 xa = texelFetch(SA, clamp(cell + ivec2(-STEP, 0), lo, hi) & mask, 0), xb = texelFetch(SA, clamp(cell + ivec2(STEP, 0), lo, hi) & mask, 0);
    vec4 za = texelFetch(SA, clamp(cell + ivec2(0, -STEP), lo, hi) & mask, 0), zb = texelFetch(SA, clamp(cell + ivec2(0, STEP), lo, hi) & mask, 0);
    float e0 = n0.r + n0.g;
    float exa = xa.r > 0.2 ? xa.r + xa.g : e0, exb = xb.r > 0.2 ? xb.r + xb.g : e0, eza = za.r > 0.2 ? za.r + za.g : e0, ezb = zb.r > 0.2 ? zb.r + zb.g : e0;
    v_n = normalize(vec3(-(exb - exa) / (4.0 * float(STEP) * DX / 2.0), 1.0, -(ezb - eza) / (4.0 * float(STEP) * DX / 2.0)));
    float wet = floor(n0.a) / 4095.0, foam = fract(n0.a) / 0.99;
    v_hfw = vec3(h, foam, wet);
    vec2 fl = texelFetch(SV, cell & mask, 0).rg;
    /* how far in from the area's edge */
    ivec2 ed = min(cell - lo, hi - cell);
    v_flow = vec4(fl, float(min(ed.x, ed.y)) / 16.0, 0.0);
    /* (a dry spot with dry neighbours has nothing to draw) */
    bool nothing = h <= 0.05 && wet <= 0.0 && xa.r + xb.r + za.r + zb.r <= 0.05 && floor(xa.a + xb.a + za.a + zb.a) <= 0.0;
    vec3 w = vec3((vec2(cell) + 0.5) * DX, 0.0).xzy;
    w.y = h > 0.05 ? eta : g + 0.8;
    v_w = w;
    gl_Position = nothing && pc.d.y != 2 ? vec4(0.0, 0.0, 2.0, 1.0) : pc.clip * vec4(w, 1.0);
}
