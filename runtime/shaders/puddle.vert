/* Reactive puddles: the surface, a grid in world space raised by the height grid (pass 0), or the spray: small quads thrown up from
   cells whose foam is fast, chosen by hash with no CPU (pass 1). No vertex buffer. */
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(push_constant) uniform PC {
    mat4 clip;      /* world to clip */
    uvec4 a;        /* x grid, y ground, z pass, w the light block */
    vec4 rect;      /* x0, z0, sx, sz */
    vec4 lev;       /* level, time, sampler, grid cells */
} pc;
layout(location = 0) out vec3 v_w;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec2 v_q;

float rnd(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return float(x & 0xffffu) / 65536.0;
}

void main()
{
    int smp = int(pc.lev.z);
    if (pc.a.z == 1u) {
        uint id = uint(gl_VertexIndex) / 6u, corner = uint(gl_VertexIndex) % 6u;
        const vec2 cs[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
        vec2 q = cs[corner];
        float life = fract(pc.lev.y * (0.7 + rnd(id) * 0.8) + rnd(id + 91u));
        /* a spot that holds still for its flight */
        uint flight = uint(floor(pc.lev.y * (0.7 + rnd(id) * 0.8) + rnd(id + 91u)));
        vec2 uv = vec2(rnd(id * 7u + flight * 131u), rnd(id * 13u + flight * 257u));
        vec4 s = textureLod(sampler2D(textures[pc.a.x], samplers[smp]), uv, 0.0);
        float strength = s.a * step(0.15, s.a);
        vec2 up = pc.rect.xy + uv * pc.rect.zw;
        float g = life * (1.0 - life) * 4.0;
        vec3 c = vec3(up.x + (rnd(id + 5u) - 0.5) * 60.0 * life, pc.lev.x + s.r + g * (40.0 + 90.0 * rnd(id + 3u)), up.y + (rnd(id + 9u) - 0.5) * 60.0 * life);
        vec4 cp = pc.clip * vec4(c, 1.0);
        /* (none at the camera: it would fill the screen) */
        float size = strength > 0.0 && cp.w > 400.0 ? 5.0 + 10.0 * life : 0.0;
        /* (it faces the screen: the size in clip units) */
        cp.xy += q * size * vec2(pc.clip[0][0], -pc.clip[1][1] * 0.0 + abs(pc.clip[1][1])) * 0.9;
        gl_Position = cp;
        v_w = c;
        v_uv = vec2(strength * (1.0 - life), 0.0);
        v_q = q;
        return;
    }
    const int N = 48;
    int quad = gl_VertexIndex / 6, corner = gl_VertexIndex % 6;
    ivec2 ij = ivec2(quad % N, quad / N);
    const ivec2 cs[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));
    vec2 uv = vec2(ij + cs[corner]) / float(N);
    float h = textureLod(sampler2D(textures[pc.a.x], samplers[smp]), uv, 0.0).r;
    vec3 w = vec3(pc.rect.x + uv.x * pc.rect.z, pc.lev.x + h, pc.rect.y + uv.y * pc.rect.w);
    gl_Position = pc.clip * vec4(w, 1.0);
    v_w = w;
    v_uv = uv;
    v_q = vec2(0.0);
}
