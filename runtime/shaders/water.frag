/* Water: the surface. What is under it, bent and darkened by the depth, from a copy of the picture; reflections marched through the
   picture and the scene's distances along the mirrored ray (the sky's the same picture when the ray gets away); brighter at low angles;
   the sun, the cars' headlights and the track's lamps on it; foam where the simulation has it, lit as the ground is; ground it has crossed,
   wet. The spray's pass 1. */
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(std430, set = 0, binding = 6) readonly buffer LightBlock { vec4 v[]; } lb;
layout(push_constant) uniform PC {
    mat4 clip;
    ivec4 a;
    ivec4 r;
    vec4 c;
    ivec4 d;
} pc;
layout(location = 0) in vec3 v_w;
layout(location = 1) in vec3 v_n;
layout(location = 2) in vec3 v_hfw;
layout(location = 3) in vec4 v_flow;
layout(location = 0) out vec4 o;

#define SMP samplers[int(pc.c.z)]
#define PIC sampler2D(textures[int(pc.c.w)], SMP)
#define DIST sampler2D(textures[pc.d.z], SMP)

float hash2(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float vnoise(vec2 p)
{
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main()
{
    uint b = uint(pc.a.w);
    float k = lb.v[b + 8u].w;
    vec3 amb = lb.v[b + 7u].rgb, sunc = lb.v[b + 6u].rgb, sun = lb.v[b + 5u].xyz;
    vec4 sz = lb.v[b + 9u];
    vec2 luv = v_w.xz * sz.xy;
    int smp = int(pc.c.z);
    float vis = texture(sampler2D(textures[pc.d.w & 0xffff], samplers[smp]), luv).r;
    vec2 gamb = texture(sampler2D(textures[pc.d.w >> 16], samplers[smp]), luv).rg;
    vec3 ground_light = (amb * mix(1.0, gamb.r, lb.v[b + 7u].w) + sunc * max(sun.y, 0.0) * vis) * k;
    /* what lands on it from the cars and the track's lamps */
    vec3 hl = vec3(0.0);
    vec4 lmp = lb.v[b + 36u];
    if (lmp.z > 0.0)
        hl += vec3(1.0, 0.86, 0.62) * 2.2 * lmp.z * texture(sampler2D(textures[int(lmp.x)], samplers[smp]), luv).r;
    if (pc.d.y == 1) {
        /* drops in a headlight's beam shine */
        int nc = int(lb.v[b + 11u].w);
        for (int i = 0; i < nc; i++) {
            vec4 cp = lb.v[b + 12u + uint(i) * 2u];
            if (cp.w <= 0.0)
                continue;
            vec3 cf = lb.v[b + 13u + uint(i) * 2u].xyz;
            vec3 d = v_w - (cp.xyz + cf * 170.0 + vec3(0.0, 70.0, 0.0));
            float dl = length(d);
            hl += vec3(1.0, 0.95, 0.82) * cp.w * smoothstep(0.55, 0.9, dot(d / max(dl, 1.0), cf)) / (1.0 + dl * dl / (1100.0 * 1100.0)) * 1.2;
            /* and round the car, in its lamps' spill */
            hl += vec3(0.7, 0.6, 0.5) * cp.w * 0.25 / (1.0 + dl * dl / (300.0 * 300.0));
        }
        float r = dot(v_n.xy, v_n.xy);
        if (r > 1.0 || v_hfw.x <= 0.0)
            discard;
        o = vec4(ground_light * 0.95 + hl * 0.5 + vec3(0.02) * k, (1.0 - r) * (1.0 - r) * min(v_hfw.x * 1.0, 0.7));
        return;
    }
    float h = v_hfw.x, foam_in = v_hfw.y, wet = v_hfw.z;
    if (pc.d.y == 2) {          /* ORR_WATER_DEBUG: the depth, foam, wet */
        o = vec4(clamp(h / 20.0, 0.0, 1.0), foam_in, wet, 1.0);
        return;
    }
    float water = smoothstep(0.15, 2.0, h) * clamp(v_flow.z, 0.0, 1.0);
    if (water <= 0.002) {
        /* ground it has crossed: darker, a little shine */
        float w_ = wet * clamp(v_flow.z, 0.0, 1.0);
        if (w_ <= 0.01)
            discard;
        o = vec4(0.0, 0.0, 0.0, w_ * 0.32);
        return;
    }
    vec3 eye = lb.v[b + 11u].xyz;
    vec3 vd = normalize(eye - v_w);
    /* the small ripples no grid holds, carried on the flow */
    vec2 fl = v_flow.xy;
    float t = pc.c.y;
    vec2 p0 = v_w.xz / 32.0 + fl * 0.004 + vec2(t * 0.4, t * 0.3), p1 = v_w.xz / 11.0 - fl * 0.006 + vec2(-t * 0.7, t * 0.5);
    float e = 0.35;
    float n00 = vnoise(p0), n01 = vnoise(p1);
    vec2 rip = vec2(vnoise(p0 + vec2(e, 0.0)) - n00, vnoise(p0 + vec2(0.0, e)) - n00) + 0.6 * vec2(vnoise(p1 + vec2(e, 0.0)) - n01, vnoise(p1 + vec2(0.0, e)) - n01);
    float rough = 0.05 + min(length(fl) / 700.0, 0.2);
    vec3 n = normalize(vec3(v_n.x * 2.2 - rip.x * rough, v_n.y, v_n.z * 2.2 - rip.y * rough));
    float cosv = max(dot(n, vd), 0.0);
    float fres = 0.025 + 0.975 * pow(1.0 - cosv, 5.0);
    vec3 refl = reflect(-vd, n);
    if (refl.y < 0.02)
        refl = normalize(vec3(refl.x, 0.02, refl.z));
    /* along the mirrored ray through the picture */
    vec2 pic = vec2(textureSize(PIC, 0));
    vec2 scr = gl_FragCoord.xy / pic;
    vec3 rcol = vec3(0.0);
    float got = 0.0, dist = 40.0, jitter = hash2(gl_FragCoord.xy) * 0.5;
    vec3 sky = textureLod(PIC, vec2(clamp(0.5 + refl.x * 0.3, 0.05, 0.95), 0.02), 0.0).rgb;
    for (int i = 0; i < 14 && got < 0.5; i++) {
        float l = dist * (1.0 + jitter * 0.3);
        vec4 cp = pc.clip * vec4(v_w + refl * l, 1.0);
        dist *= 1.42;
        if (cp.w <= 1.0)
            break;
        vec2 u = 0.5 * (cp.xy / cp.w) + 0.5;
        if (u.x < 0.0 || u.y < 0.0 || u.x > 1.0 || u.y > 1.0)
            break;
        float sd = textureLod(DIST, u, 0.0).r;
        if (sd < cp.w && cp.w - sd < 100.0 + l * 0.45) {
            float edge = min(min(u.x, 1.0 - u.x), min(u.y, 1.0 - u.y));
            rcol = textureLod(PIC, u, 0.0).rgb;
            got = smoothstep(0.0, 0.06, edge);
        }
    }
    if (got < 1.0) {
        /* no meeting: where it goes in the distance, if that is the sky in the picture */
        vec4 cp = pc.clip * vec4(v_w + refl * 200000.0, 1.0);
        vec3 far_ = sky;
        if (cp.w > 1.0) {
            vec2 u = 0.5 * (cp.xy / cp.w) + 0.5;
            if (u.x > 0.0 && u.y > 0.0 && u.x < 1.0 && u.y < 1.0 && textureLod(DIST, u, 0.0).r > 1e6)
                far_ = textureLod(PIC, u, 0.0).rgb;
        }
        rcol = mix(far_, rcol, got);
    }
    /* what is under it, bent; not what stands in front of it */
    float dep = min(h, 200.0);
    vec2 bend = n.xz * clamp(dep * 0.0012 + 0.002, 0.0, 0.03);
    vec2 uu = scr + bend;
    float vw = gl_FragCoord.w > 0.0 ? 1.0 / gl_FragCoord.w : 1.0;
    if (textureLod(DIST, uu, 0.0).r < vw - 10.0)
        uu = scr;
    vec3 under = textureLod(PIC, uu, 0.0).rgb;
    /* muddy shallow water to a darker body with depth */
    vec3 ext = vec3(0.018, 0.012, 0.009);
    vec3 tint = exp(-ext * dep);
    vec3 body = (vec3(0.10, 0.14, 0.13) * (amb + sunc * 0.25 * vis)) * k;
    float murk = 1.0 - exp(-dep * 0.012);
    under = mix(under * tint, body, murk);
    vec3 col = mix(under, rcol, fres);
    /* the sun */
    float spec = pow(max(dot(refl, sun), 0.0), 900.0) * 3.0 + pow(max(dot(refl, sun), 0.0), 60.0) * 0.08;
    col += sunc * spec * vis * k;
    /* headlights and tail lights on it */
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
        float cone = smoothstep(0.75, 0.97, dot(dn, cf)) / (1.0 + dl * dl / (1300.0 * 1300.0));
        col += vec3(1.0, 0.95, 0.82) * cp.w * cone * (0.06 + pow(max(dot(refl, -dn), 0.0), 40.0) * 2.5);
        /* the car's own lamps seen in the water */
        vec3 lamp = normalize(hp - v_w);
        col += vec3(1.0, 0.95, 0.82) * cp.w * pow(max(dot(refl, lamp), 0.0), 500.0) * 1.5 * step(0.0, dot(-lamp, cf));
    }
    col += hl * (0.12 + fres * 0.9);
    /* foam */
    float fleck = 0.5 * vnoise(v_w.xz / 22.0 - fl * 0.012) + 0.32 * vnoise(v_w.xz / 8.0 - fl * 0.02) + 0.18 * vnoise(v_w.xz / 3.0 + vec2(t * 0.8, -t * 0.6));
    float foam = smoothstep(0.1, 0.95, foam_in) * smoothstep(0.66, 0.42, fleck - foam_in * 0.3);
    col = mix(col, ground_light * 0.92 + hl * 0.4 + vec3(0.015) * k, foam * 0.72);
    o = vec4(col, max(water, foam * water));
}
