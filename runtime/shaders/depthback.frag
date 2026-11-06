/* The scene's depth into the back buffer's at another size (vk.c scene_depth_back, where depth blits are not to be had): each pixel
   takes the nearest of the scene's; the colour is kept (blend zero, one). */
#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];
layout(push_constant) uniform PC {
    ivec4 idx;                  /* x the scene's depth, y the nearest sampler */
    vec4 k;                     /* xy: target pixel -> the scene's UV */
} pc;
layout(location = 0) out vec4 o;

void main()
{
    gl_FragDepth = textureLod(sampler2D(textures[pc.idx.x], samplers[pc.idx.y]), gl_FragCoord.xy * pc.k.xy, 0.0).r;
    o = vec4(0.0);
}
