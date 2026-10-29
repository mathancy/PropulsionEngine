#version 450
#extension GL_GOOGLE_include_directive : require

// Averages the noisy occlusion over a 5x5 neighborhood, but only across pixels at about the same depth, so occlusion
// doesn't bleed over the edge of an object onto what is behind it.

#include "ssao.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out float outAo;

layout(set = 0, binding = 0) uniform sampler2D aoRaw;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

void main() {
    ivec2 size = textureSize(depthTex, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float centerDepth = texelFetch(depthTex, pixel, 0).r;
    if (centerDepth >= SSAO_BACKGROUND_DEPTH) {
        outAo = 1.0;
        return;
    }
    float centerZ = viewPositionFromDepth(uv, centerDepth).z;

    float sum = 0.0;
    float weight = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            ivec2 q = clamp(pixel + ivec2(x, y), ivec2(0), size - ivec2(1));
            float d = texelFetch(depthTex, q, 0).r;
            if (d >= SSAO_BACKGROUND_DEPTH) continue;
            float z = viewPositionFromDepth((vec2(q) + 0.5) / vec2(size), d).z;
            // Same surface: within a few percent of the distance.
            float w = abs(z - centerZ) < 0.05 + 0.03 * abs(centerZ) ? 1.0 : 0.0;
            sum += texelFetch(aoRaw, q, 0).r * w;
            weight += w;
        }
    }
    outAo = weight > 0.0 ? sum / weight : texelFetch(aoRaw, pixel, 0).r;
}
