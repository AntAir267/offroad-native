/* Reactive puddles (runtime/puddle.c, vk.c vkr_puddle_*): one step of a puddle's height grid, drawn into its next image.
   r height over the rest level (game units), g its speed, b how wet the ground is, a foam. A plain wave equation with damping,
   the bank a wall the water can climb, and each car pushing: down under its wheels, up in ridges to the sides.
   pass (a.z): 0 the ground's height at each cell, into a second image, once; 1 a step. */
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(std430, set = 0, binding = 6) readonly buffer LightBlock { vec4 v[]; } lb;
layout(push_constant) uniform PC {
    uvec4 a;        /* x this grid, y the ground image, z pass, w base of the cars in the block */
    vec4 rect;      /* x0, z0, 1 / size x, 1 / size z in world per uv... x0, z0, sx, sz */
    vec4 lev;       /* rest level, cell size, cars, height map's sampler */
    vec4 ht;        /* height map: lowest, range, cell, slot */
} pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;

float ground_at(vec2 w)
{
    int h = int(pc.ht.w), smp = int(pc.lev.w);
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

void main()
{
    vec2 w = pc.rect.xy + v_uv * pc.rect.zw;
    if (pc.a.z == 0u) {
        o = vec4(ground_at(w));
        return;
    }
    int smp = int(pc.lev.w);
    ivec2 p = ivec2(gl_FragCoord.xy), sz = textureSize(sampler2D(textures[pc.a.x], samplers[smp]), 0);
    vec4 c = texelFetch(sampler2D(textures[pc.a.x], samplers[smp]), p, 0);
    float g = texelFetch(sampler2D(textures[pc.a.y], samplers[smp]), p, 0).r;
    float lap = 0.0;
    const ivec2 nb[4] = ivec2[4](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1));
    float level = pc.lev.x;
    for (int i = 0; i < 4; i++) {
        ivec2 q = clamp(p + nb[i], ivec2(0), sz - 1);
        float hn = texelFetch(sampler2D(textures[pc.a.x], samplers[smp]), q, 0).r;
        float gn = texelFetch(sampler2D(textures[pc.a.y], samplers[smp]), q, 0).r;
        /* a neighbour above the water is a wall: the wave comes back, and climbs as far as it has height */
        lap += (level + hn > gn ? hn : c.r) - c.r;
    }
    float vel = (c.g + 0.22 * lap) * 0.992;
    float h = c.r;
    /* the cars */
    uint b = pc.a.w;
    int n = int(pc.lev.z);
    for (int i = 0; i < n; i++) {
        vec4 cp = lb.v[b + uint(i) * 2u], cd = lb.v[b + uint(i) * 2u + 1u];
        vec2 pos = cp.xz, dir = cd.xz;
        float speed = cd.w;
        vec2 d = w - pos;
        if (dot(d, d) > 600.0 * 600.0 || speed < 0.2 || level + 40.0 < cp.y - 90.0)
            continue;
        vec2 side = vec2(dir.y, -dir.x);
        float s = min(speed / 30.0, 1.6);
        /* four wheels */
        for (int wx = -1; wx <= 1; wx += 2)
            for (int wz = -1; wz <= 1; wz += 2) {
                vec2 wp = d - side * float(wx) * 85.0 - dir * float(wz) * 125.0;
                float along = dot(wp, dir), across = dot(wp, side);
                vel -= 1.6 * s * exp(-(along * along + across * across) / (42.0 * 42.0));
                /* thrown aside: a ridge on the outside, more behind the wheel */
                float out_ = across * float(wx);
                vel += 1.1 * s * exp(-(along * along) / (70.0 * 70.0)) * exp(-(out_ - 55.0) * (out_ - 55.0) / (24.0 * 24.0));
            }
        /* the trough the body leaves behind */
        float al = dot(d, dir), ac = dot(d, side);
        if (al < -100.0)
            vel -= 0.25 * s * exp(-(ac * ac) / (90.0 * 90.0)) * exp(-(al + 200.0) * (al + 200.0) / (110.0 * 110.0));
    }
    h += vel;
    h *= 0.9985;        /* back to the rest level, slowly */
    h = clamp(h, -30.0, 40.0);
    float depth = level + h - g;
    float wet = depth > 0.0 ? 1.0 : max(c.b - 0.0009, 0.0);
    float foam = max(c.a * 0.965, clamp(abs(vel) * 0.55 - 0.2, 0.0, 1.0));
    if (depth < -6.0) {         /* (dry ground has no speed of its own) */
        h *= 0.8;
        vel = 0.0;
    }
    o = vec4(h, vel, wet, foam);
}
