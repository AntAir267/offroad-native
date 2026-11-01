/* Direct3D draws: positions arrive in Vulkan clip space (the CPU did the D3D 7 transform,
   lighting and viewport mapping, including the half-pixel offset), so this only passes through.
   Colours go out twice: smooth for Gouraud and flat for D3DSHADE_FLAT (first vertex).
   Compiled again with MOTION (temporal anti-aliasing's scene, vk.c): the position without the
   frame's jitter and where the vertex was in the previous frame (motion.glsl). */
#version 450
#extension GL_EXT_nonuniform_qualifier : require
#ifdef MOTION
#extension GL_GOOGLE_include_directive : require
#define MOTION_VERTEX
#include "motion.glsl"
#endif
layout(push_constant) uniform PC {
    uvec4 stage;
    ivec4 texsmp;
    uvec4 misc;
    vec4 fog;
    uvec4 flags;
    uvec4 tnl;                  /* x: the draw's Motion block */
    uvec4 shadow;
    uvec4 light;                /* baked lighting (draw.frag): x on, y the view's block */
} pc;
layout(std430, set = 0, binding = 6) readonly buffer LightBlock { vec4 v[]; } lb;
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec4 a_diffuse;
layout(location = 2) in vec4 a_specular;
layout(location = 3) in vec2 a_tc0;
layout(location = 4) in vec2 a_tc1;
layout(location = 0) out vec4 v_diffuse;
layout(location = 1) out vec4 v_specular;
layout(location = 2) flat out vec4 f_diffuse;
layout(location = 3) flat out vec4 f_specular;
layout(location = 4) out vec4 v_tc;
layout(location = 5) out vec4 v_dsh;          /* (ray-traced shadows: as tnl.vert; nothing to split here) */
layout(location = 6) out vec4 v_ssh;
layout(location = 7) flat out vec4 f_dsh;
layout(location = 8) flat out vec4 f_ssh;
layout(location = 9) out vec3 v_wpos;
layout(location = 13) flat out uint v_fade;   /* (tnl.vert's instanced copies' fade: none here) */
layout(location = 14) out vec3 v_lw;          /* baked lighting: the world position */
layout(location = 15) out vec3 v_lr;          /* ... and for what is blended, the light here (draw.frag) */

void main()
{
    gl_Position = a_pos;
    gl_PointSize = 1.0;
    v_fade = 0u;
    v_diffuse = a_diffuse;
    f_diffuse = a_diffuse;
    v_specular = a_specular;
    f_specular = a_specular;
    v_tc = vec4(a_tc0, a_tc1);
    v_dsh = a_diffuse;
    f_dsh = a_diffuse;
    v_ssh = a_specular;
    f_ssh = a_specular;
    v_wpos = vec3(0.0);
    v_lw = vec3(0.0);
    v_lr = vec3(1.0);
    if (pc.light.x != 0u) {
        /* back from clip space to the game's screen x and y times w, and through the camera's matrix backwards: what
           the fragment gets is linear in view space, so correct across a triangle */
        uint b = pc.light.y;
        vec4 pl = lb.v[b + 4u];
        vec3 q = vec3((a_pos.x - pl.y * a_pos.w) / pl.x, (a_pos.y - pl.w * a_pos.w) / pl.z, a_pos.w) - lb.v[b + 3u].xyz;
        v_lw = vec3(dot(lb.v[b].xyz, q), dot(lb.v[b + 1u].xyz, q), dot(lb.v[b + 2u].xyz, q));
        if (pc.light.x == 6u) {
            /* the grid at the height off the ground (draw.frag's light_ratio, for a surface half towards the sun) */
            vec4 sz = lb.v[b + 9u], mis = lb.v[b + 10u];
            int smp = int(mis.x);
            ivec2 ts = textureSize(sampler2D(textures[pc.light.w >> 16], samplers[smp]), 0);
            ivec2 c0 = clamp(ivec2(v_lw.xz / sz.w + 0.5), ivec2(0), ts - 1);
            float dy = v_lw.y - (mis.y + texelFetch(sampler2D(textures[pc.light.w >> 16], samplers[smp]), c0, 0).r * mis.z);
            float layers = sz.z, ly = clamp(dy / lb.v[b + 5u].w - 0.5, 0.0, layers - 1.001), l0 = floor(ly);
            vec2 uv = v_lw.xz * sz.xy;
            ivec2 gs = textureSize(sampler2D(textures[pc.light.w & 0xffffu], samplers[smp]), 0);
            bool across = mis.w > 0.5;
            float tile = across ? uv.x : uv.y, tn = across ? float(gs.x) : float(gs.y);
            float vt = clamp(tile, 0.5 * layers / tn, 1.0 - 0.5 * layers / tn);
            vec2 g0 = across ? vec2((l0 + vt) / layers, uv.y) : vec2(uv.x, (l0 + vt) / layers);
            vec3 g = textureLod(sampler2D(textures[pc.light.w & 0xffffu], samplers[smp]), g0, 0.0).rgb;
            vec3 sunc = lb.v[b + 6u].rgb, amb = lb.v[b + 7u].rgb;
            vec3 was = amb + sunc * 0.6;
            vec3 now = amb * mix(1.0, g.g, lb.v[b + 7u].w) + sunc * 0.6 * g.r + lb.v[b + 8u].rgb * g.b * 3.0;
            was /= max(1.0, max(was.r, max(was.g, was.b)));
            now /= max(1.0, max(now.r, max(now.g, now.b)));
            v_lr = clamp(now / max(was, vec3(0.001)), vec3(0.0), vec3(1.6));
        }
    }
#ifdef MOTION
    motion_out(a_pos, pc.tnl.x);    /* (the previous frame's matrix takes this frame's clip space) */
#endif
}
