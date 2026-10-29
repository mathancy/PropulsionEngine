// Shared by ssao.frag and ssao_blur.frag. Must match AoUniforms in src/rendering/AmbientOcclusion.cpp.

#ifndef SSAO_GLSL
#define SSAO_GLSL

#define SSAO_SAMPLES 16

layout(set = 0, binding = 2) uniform AoParams {
    mat4 projection;
    mat4 inverseProjection;
    vec4 params;                      // x = radius (world units), y = bias, z = strength (0..1), w = power
    vec4 kernel[SSAO_SAMPLES];        // xyz = a sample offset in the hemisphere around +Z
} ao;

// A point on the screen and its stored depth, back to a position in view space.
vec3 viewPositionFromDepth(vec2 uv, float depth) {
    vec4 v = ao.inverseProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return v.xyz / v.w;
}

// Anything this deep is the empty background.
const float SSAO_BACKGROUND_DEPTH = 0.99999;

#endif
