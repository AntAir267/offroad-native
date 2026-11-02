/* Reactive puddles: the surface. Its normal from the height grid; the ground seen through it, bent, from a copy of the picture; the
   sky and bright lights in it, more at low angles; foam; wet ground past the water's edge. Light as the baked lighting has it. */
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(std430, set = 0, binding = 6) readonly buffer LightBlock { vec4 v[]; } lb;
layout(push_constant) uniform PC {
    mat4 clip;
    uvec4 a;        /* x grid, y ground, z pass, w the light block */
    vec4 rect;
    vec4 lev;       /* level, time, sampler, screen copy's slot */
    vec4 scr;       /* 1 / width, 1 / height, puddle's softness */
} pc;
layout(location = 0) in vec3 v_w;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec2 v_q;
layout(location = 0) out vec4 o;

float hash2(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float vnoise(vec2 p)
{
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main()
{
    uint b = pc.a.w;
    float k = lb.v[b + 8u].w;
    int smp = int(pc.lev.z);
    vec3 amb = lb.v[b + 7u].rgb, sunc = lb.v[b + 6u].rgb, sun = lb.v[b + 5u].xyz;
    if (pc.a.z == 1u) {
        float r = dot(v_q, v_q);
        if (r > 1.0 || v_uv.x <= 0.0)
            discard;
        o = vec4((amb + sunc * 0.5) * k + vec3(0.1) * k, (1.0 - r) * v_uv.x * 0.35);
        return;
    }
    vec2 uv = v_uv;
    ivec2 sz = textureSize(sampler2D(textures[pc.a.x], samplers[smp]), 0);
    vec2 e = 1.0 / vec2(sz);
    vec4 c = texture(sampler2D(textures[pc.a.x], samplers[smp]), uv);
    float hx = texture(sampler2D(textures[pc.a.x], samplers[smp]), uv + vec2(e.x, 0.0)).r - texture(sampler2D(textures[pc.a.x], samplers[smp]), uv - vec2(e.x, 0.0)).r;
    float hz = texture(sampler2D(textures[pc.a.x], samplers[smp]), uv + vec2(0.0, e.y)).r - texture(sampler2D(textures[pc.a.x], samplers[smp]), uv - vec2(0.0, e.y)).r;
    float g = texture(sampler2D(textures[pc.a.y], samplers[smp]), uv).r;
    vec2 cell = pc.rect.zw * e * 2.0;
    vec3 n = normalize(vec3(-hx / cell.x * 2.5, 1.0, -hz / cell.y * 2.5));
    float depth = pc.lev.x + c.r - g;
    if (pc.lev.w > 0.5) {               /* ORR_PUDDLE_DEBUG: height, speed, foam */
        o = vec4(0.5 + c.r * 0.04, 0.5 + c.g * 0.25, c.a, 1.0);
        return;
    }
    /* the puddle's soft outline: where the game's decal was */
    vec2 ell = (uv - 0.5) * 2.0;
    float rr = dot(ell, ell);
    float shape = 1.0 - smoothstep(0.45, 0.95, rr);
    float water = smoothstep(0.0, 6.0, depth) * shape;
    /* past its edge: ground the waves have wet, drying */
    float wetground = c.b * (1.0 - water) * (1.0 - smoothstep(0.75, 1.0, rr));
    if (water <= 0.001) {
        if (wetground <= 0.01)
            discard;
        o = vec4(0.0, 0.0, 0.0, wetground * 0.28);
        return;
    }
    vec3 eye = lb.v[b + 11u].xyz;
    vec3 vd = normalize(eye - v_w);
    float cosv = max(dot(n, vd), 0.0);
    float fres = 0.07 + 0.93 * pow(1.0 - cosv, 4.0);
    vec3 refl = reflect(-vd, n);
    /* the sky: the light the bake takes from it */
    vec3 sky = mix(amb * 1.1, amb * 1.5 + sunc * 0.3, clamp(refl.y, 0.0, 1.0));
    float spec = pow(max(dot(refl, sun), 0.0), 1200.0) * 1.5;
    /* the ground through it, bent, from the picture; darker with depth */
    vec2 suv = gl_FragCoord.xy * pc.scr.xy + n.xz * clamp(depth * 0.006 + abs(c.r) * 0.003, 0.0, 0.05);
    vec3 under = texture(sampler2D(textures[int(pc.scr.z)], samplers[smp]), suv).rgb;
    under *= mix(vec3(1.0), vec3(0.62, 0.66, 0.62), clamp(depth / 40.0, 0.0, 1.0));
    vec3 col = mix(under, sky * k, fres) + sunc * min(spec, 0.5) * k;
    /* headlights and tail lights on the water: what is lit by them */
    int ncar = int(lb.v[b + 11u].w);
    for (int i = 0; i < ncar; i++) {
        vec4 cp = lb.v[b + 12u + uint(i) * 2u];
        if (cp.w <= 0.0)
            continue;
        vec3 cf = lb.v[b + 13u + uint(i) * 2u].xyz;
        vec3 hp = cp.xyz + cf * 170.0 + vec3(0.0, 70.0, 0.0);
        vec3 d = v_w - hp;
        float dl = length(d);
        vec3 dn = d / max(dl, 1.0);
        float cone = smoothstep(0.80, 0.97, dot(dn, cf)) / (1.0 + dl * dl / (1100.0 * 1100.0));
        col += vec3(1.0, 0.95, 0.82) * cp.w * cone * (0.08 + pow(max(dot(refl, -dn), 0.0), 60.0) * 2.0);
    }
    /* foam */
    float fleck = 0.6 * vnoise(v_w.xz / 14.0) + 0.4 * vnoise(v_w.xz / 5.0);
    float foam = smoothstep(0.1, 0.9, c.a) * smoothstep(0.45, 0.8, fleck + c.a * 0.25);
    col = mix(col, (amb + sunc * 0.6) * k, foam * 0.8);
    o = vec4(col, water);
}
