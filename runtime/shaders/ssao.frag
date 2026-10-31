/* Off-Road's ambient occlusion (vk.c ssao_passes), from the depth buffer alone: the game hands over finished
   screen coordinates, so there is nothing to trace. Drawn as one triangle (present.vert) after the world and
   before the HUD; push constants a.z is the pass:
   0  into a small image (a pixel for each S x S): the surface's position from its depth (z = b.x + b.y / w, the
      game's projection, focal length b.z, centre c.xy), its face normal from its neighbours, and 8 samples
      on a disc of c.z game units turned per pixel: how much of the hemisphere round the normal
      they close off. Out: the light left (r), the eye distance (g).
   1, 2  blur across, then down: 5 taps, by how near each lies in depth.
   3  over the view: the small image's 4 nearest pixels, by place and depth; multiplied into the colour. */
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(push_constant) uniform PC {
    uvec4 a;                    /* x: the depth's slot, y: the small image's, z: pass, w: S */
    vec4 b;                     /* x, y: z = x + y / w; z: focal length in pixels; w: strength */
    vec4 c;                     /* x, y: the view's centre in pixels; z: radius, game units; w: unused */
    vec4 f;                     /* fades out from x to y (eye distance) */
} pc;
layout(constant_id = 0) const int SPEC_PASS = -1;
layout(location = 0) out vec4 o_color;

float depth_at(ivec2 p) { return texelFetch(sampler2D(textures[pc.a.x], samplers[0]), p, 0).r; }
float eye_of(float z) { return pc.b.y / (z - pc.b.x); }
vec3 view_at(ivec2 p)
{
    float w = eye_of(depth_at(p));
    return vec3((vec2(p) + 0.5 - pc.c.xy) / pc.b.z * w, w);
}
vec4 small_at(ivec2 p) { return texelFetch(sampler2D(textures[pc.a.y], samplers[0]), p, 0); }

void main()
{
    ivec2 frag = ivec2(gl_FragCoord.xy);
    if (SPEC_PASS == 0) {
        int S = int(pc.a.w);
        ivec2 p = frag * S;
        float z = depth_at(p);
        float w = eye_of(z);
        if (w <= 0.0 || w >= pc.f.y) {
            o_color = vec4(1.0, 1e6, 0.0, 1.0);
            return;
        }
        vec3 me = view_at(p);
        /* the nearer neighbour each way: an edge's far side says nothing of this surface */
        vec3 l = view_at(p - ivec2(S, 0)), r = view_at(p + ivec2(S, 0)), u = view_at(p - ivec2(0, S)), d = view_at(p + ivec2(0, S));
        vec3 dx = abs(l.z - me.z) < abs(r.z - me.z) ? me - l : r - me;
        vec3 dy = abs(u.z - me.z) < abs(d.z - me.z) ? me - u : d - me;
        vec3 n = normalize(cross(dx, dy));
        if (n.z > 0.0)
            n = -n;
        float R = pc.c.z, px = clamp(R * pc.b.z / w, 2.0 * float(S), 120.0);
        /* interleaved gradient noise: the blur cancels its pattern */
        float turn = 6.2831853 * fract(52.9829189 * fract(dot(vec2(frag), vec2(0.06711056, 0.00583715))));
        float closed = 0.0;
        for (int i = 0; i < 8; i++) {
            float t = turn + 6.2831853 * float(i) / 8.0, rr = px * sqrt((float(i) + 0.5) / 8.0);
            ivec2 q = p + ivec2(vec2(cos(t), sin(t)) * rr);
            vec3 v = view_at(q) - me;
            float vv = dot(v, v);
            if (vv < 1e-3)
                continue;
            float fall = clamp(1.0 - vv / (R * R), 0.0, 1.0);
            closed += max(dot(n, v) / sqrt(vv) - 0.15, 0.0) * fall;
        }
        float fade = clamp((pc.f.y - w) / (pc.f.y - pc.f.x), 0.0, 1.0);
        o_color = vec4(1.0 - clamp(pc.b.w * closed / 8.0, 0.0, 0.85) * fade, w, 0.0, 1.0);
    } else if (SPEC_PASS == 1 || SPEC_PASS == 2) {
        ivec2 dir = SPEC_PASS == 1 ? ivec2(1, 0) : ivec2(0, 1);
        vec4 me = small_at(frag);
        float sum = 0.0, wsum = 0.0;
        for (int i = -2; i <= 2; i++) {
            vec4 s = small_at(frag + dir * i);
            float k = (i == 0 ? 2.0 : 1.0) * clamp(1.0 - abs(s.g - me.g) / (me.g * 0.03 + 2.0), 0.0, 1.0);
            sum += s.r * k;
            wsum += k;
        }
        o_color = vec4(sum / max(wsum, 1e-3), me.g, 0.0, 1.0);
    } else {
        float S = float(pc.a.w);
        vec2 pos = gl_FragCoord.xy / S - 0.5;
        ivec2 q = ivec2(floor(pos));
        vec2 t = pos - vec2(q);
        float w = eye_of(depth_at(frag)), sum = 0.0, wsum = 0.0;
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++) {
                vec4 s = small_at(q + ivec2(x, y));
                float k = (x == 1 ? t.x : 1.0 - t.x) * (y == 1 ? t.y : 1.0 - t.y) *
                          clamp(1.0 - abs(s.g - w) / (w * 0.03 + 2.0), 0.001, 1.0);
                sum += s.r * k;
                wsum += k;
            }
        o_color = vec4(vec3(sum / max(wsum, 1e-4)), 1.0);
    }
}
