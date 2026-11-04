/* Temporal anti-aliasing (vk.c "temporal anti-aliasing"): the upscaled scene into the back buffer
   (the view's area), drawn over a triangle covering the target (present.vert). idx: x the source
   image, y its sampler, z what to show (0 the source; 1 the motion vectors, 2 the masks, 3 the
   depth: debug views of the scene image's inputs), w the scene's colour image (debug views). uv:
   the target's pixel -> the source's UV (scale, offset). q (vk.c "colour precision"): x the
   dither's blue-noise pattern for a 16-bit scene going into the 8-bit back buffer (-1: none), y 1:
   the source's alpha (the 16-bit scene without FSR 2), else 1. z 4: FXAA (fxaa()) on the way, the source the target's size. */
#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(push_constant) uniform PC {
    vec4 uv;                    /* source UV = gl_FragCoord.xy * uv.xy + uv.zw */
    ivec4 idx;
    vec4 k;                     /* x: the debug views' motion scale (UV -> shown), y: sharpening 0..1 (the source
                                   the target's size: ANTI-ALIASING TAA's history; FSR 2 sharpens its own) */
    ivec4 q;
} pc;
layout(location = 0) out vec4 o_color;

/* About one level of triangular dither before the 8-bit target rounds: the pattern's rank (uniform 0..1,
   blue noise, the same every frame) turned into a triangular spread of -1..1 level with its order kept, so
   it stays blue; less of it towards 0 and 1 (none at black and white, which it could only push one way) */
vec3 dither(vec3 c)
{
    float n = texelFetch(sampler2D(textures[pc.q.x], samplers[pc.idx.y]), ivec2(gl_FragCoord.xy) & 63, 0).r;
    float t = n < 0.5 ? sqrt(2.0 * n) - 1.0 : 1.0 - sqrt(2.0 - 2.0 * n);
    c = clamp(c, 0.0, 1.0);
    vec3 a = clamp(255.0 * min(c, 1.0 - c) + 0.5, 0.5, 1.0);
    return c + t * a * (1.0 / 255.0);
}

/* robust contrast-adaptive sharpening as FSR's RCAS does it: the four neighbours' lobe, as much as stays
   within what they and the pixel span */
vec3 sharpen(vec3 e)
{
    ivec2 p = ivec2(gl_FragCoord.xy), m = textureSize(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), 0) - 1;
    vec3 b = texelFetch(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), clamp(p + ivec2(0, -1), ivec2(0), m), 0).rgb;
    vec3 d = texelFetch(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), clamp(p + ivec2(-1, 0), ivec2(0), m), 0).rgb;
    vec3 f = texelFetch(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), clamp(p + ivec2(1, 0), ivec2(0), m), 0).rgb;
    vec3 h = texelFetch(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), clamp(p + ivec2(0, 1), ivec2(0), m), 0).rgb;
    vec3 mn = min(min(b, d), min(min(f, h), e)), mx = max(max(b, d), max(max(f, h), e));
    vec3 lo = -mn / (4.0 * mx + 1e-4), hi = (1.0 - mx) / (4.0 * mn - 4.0 - 1e-4);
    vec3 l = max(lo, hi);
    float lobe = max(-0.1875, min(max(l.r, max(l.g, l.b)), 0.0)) * pc.k.y;
    return clamp((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), 0.0, 1.0);
}

/* FXAA, written from the method (Lottes' fast approximate anti-aliasing): where the luma's local contrast is high, find
   whether the edge runs across or along, walk along it both ways to its ends, and take the colour from across the edge
   by how far this pixel is from the nearer end; plus a little of the 3 x 3 average for edges finer than a pixel. */
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 fx(vec2 uv) { return textureLod(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), uv, 0.0).rgb; }
vec4 fxaa(vec2 uv)
{
    vec2 px = 1.0 / vec2(textureSize(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), 0));
    vec4 m4 = textureLod(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), uv, 0.0);
    vec3 cm = m4.rgb;
    float lm = luma(cm), ln = luma(fx(uv + vec2(0.0, -px.y))), ls = luma(fx(uv + vec2(0.0, px.y))),
          le = luma(fx(uv + vec2(px.x, 0.0))), lw = luma(fx(uv + vec2(-px.x, 0.0)));
    float lo = min(lm, min(min(ln, ls), min(le, lw))), hi = max(lm, max(max(ln, ls), max(le, lw)));
    float range = hi - lo;
    if (range < max(0.0312, hi * 0.125))
        return m4;
    float lnw = luma(fx(uv + vec2(-px.x, -px.y))), lne = luma(fx(uv + vec2(px.x, -px.y))),
          lsw = luma(fx(uv + vec2(-px.x, px.y))), lse = luma(fx(uv + vec2(px.x, px.y)));
    /* the edge's direction: the second differences across x and y */
    float ex = abs(lnw + lsw - 2.0 * lw) + 2.0 * abs(ln + ls - 2.0 * lm) + abs(lne + lse - 2.0 * le);
    float ey = abs(lnw + lne - 2.0 * ln) + 2.0 * abs(lw + le - 2.0 * lm) + abs(lsw + lse - 2.0 * ls);
    bool horiz = ey >= ex;                  /* the edge runs along x: step across in y */
    float l1 = horiz ? ln : lw, l2 = horiz ? ls : le;
    float g1 = abs(l1 - lm), g2 = abs(l2 - lm);
    bool first = g1 >= g2;
    float step_ = horiz ? px.y : px.x;
    float lside = first ? l1 : l2, grad = max(g1, g2) * 0.25;
    if (first) step_ = -step_;
    vec2 e = uv;
    if (horiz) e.y += step_ * 0.5; else e.x += step_ * 0.5;
    vec2 dir = horiz ? vec2(px.x, 0.0) : vec2(0.0, px.y);
    float lavg = 0.5 * (lm + lside);
    /* along the edge both ways, until the luma there leaves the edge's */
    vec2 a = e - dir, b = e + dir;
    float la = luma(fx(a)) - lavg, lb = luma(fx(b)) - lavg;
    bool da = abs(la) >= grad, db = abs(lb) >= grad;
    const float steps[10] = float[10](1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0);
    for (int i = 0; i < 10 && !(da && db); i++) {
        if (!da) { a -= dir * steps[i]; la = luma(fx(a)) - lavg; da = abs(la) >= grad; }
        if (!db) { b += dir * steps[i]; lb = luma(fx(b)) - lavg; db = abs(lb) >= grad; }
    }
    float dA = horiz ? uv.x - a.x : uv.y - a.y, dB = horiz ? b.x - uv.x : b.y - uv.y;
    bool nearA = dA < dB;
    float dn = min(dA, dB), len = dA + dB;
    /* (only where the far end's luma goes the other way from this pixel's: else the pixel is not on the step) */
    bool right_way = ((nearA ? la : lb) < 0.0) != (lm - lavg < 0.0);
    float off = right_way ? 0.5 - dn / len : 0.0;
    /* sub-pixel: the 3 x 3 average's contrast with the middle */
    float avg = (2.0 * (ln + ls + le + lw) + lnw + lne + lsw + lse) / 12.0;
    float sub = clamp(abs(avg - lm) / range, 0.0, 1.0);
    sub = (-2.0 * sub + 3.0) * sub * sub;
    off = max(off, sub * sub * 0.75);
    vec2 f = uv;
    if (horiz) f.y += off * step_; else f.x += off * step_;
    return vec4(fx(f), m4.a);
}

void main()
{
    vec2 uv = gl_FragCoord.xy * pc.uv.xy + pc.uv.zw;
    vec4 s = pc.idx.z == 4 ? fxaa(uv) : textureLod(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), uv, 0.0);
    if (pc.idx.z == 0 && pc.k.y > 0.0)
        s.rgb = sharpen(s.rgb);
    if (pc.idx.z == 0 || pc.idx.z == 4) {
        o_color = vec4(pc.q.x >= 0 ? dither(s.rgb) : s.rgb, pc.q.y != 0 ? s.a : 1.0);
        return;
    }
    vec3 base = textureLod(sampler2D(textures[pc.idx.w], samplers[pc.idx.y]), uv, 0.0).rgb;
    float grey = dot(base, vec3(0.3, 0.59, 0.11)) * 0.5;
    if (pc.idx.z == 1)          /* motion: red right, green down, blue up-left (towards the previous place) */
        o_color = vec4(grey + max(s.x * pc.k.x, 0.0), grey + max(s.y * pc.k.x, 0.0),
                       grey + max(-(s.x + s.y) * 0.5 * pc.k.x, 0.0), 1.0);
    else if (pc.idx.z == 2)     /* reactive red, transparency and composition green */
        o_color = vec4(grey + s.r, grey + s.g, grey, 1.0);
    else                        /* depth (near bright) */
        o_color = vec4(vec3(pow(clamp(1.0 - s.r, 0.0, 1.0), 0.25)), 1.0);
}
