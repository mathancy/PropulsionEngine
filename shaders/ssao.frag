#version 450
#extension GL_GOOGLE_include_directive : require

// Screen-space ambient occlusion: how much of the sky above a point is hidden by nearby geometry. Samples the
// hemisphere around the surface's normal (turned a different way at every pixel; the blur pass averages that noise
// away) and counts the samples that fall behind what the depth buffer shows.

#include "ssao.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out float outAo;

layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(set = 0, binding = 1) uniform sampler2D normalTex;

vec3 octDecode(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    float depth = texture(depthTex, uv).r;
    if (depth >= SSAO_BACKGROUND_DEPTH) {
        outAo = 1.0;
        return;
    }

    vec3 position = viewPositionFromDepth(uv, depth);
    vec3 normal = octDecode(texture(normalTex, uv).rg);

    // A frame around the normal, turned by a different amount at each pixel.
    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;
    vec3 randomVec = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float radius = ao.params.x;
    float bias = ao.params.y;
    float occlusion = 0.0;
    for (int i = 0; i < SSAO_SAMPLES; i++) {
        vec3 samplePos = position + (tbn * ao.kernel[i].xyz) * radius;

        vec4 clip = ao.projection * vec4(samplePos, 1.0);
        vec2 sampleUv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (clip.w <= 0.0 || any(lessThan(sampleUv, vec2(0.0))) || any(greaterThan(sampleUv, vec2(1.0)))) continue;

        float sceneDepth = texture(depthTex, sampleUv).r;
        if (sceneDepth >= SSAO_BACKGROUND_DEPTH) continue;
        vec3 scenePos = viewPositionFromDepth(sampleUv, sceneDepth);

        // Something in front of the sample hides it, unless it is far in front (then it is a different surface).
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(position.z - scenePos.z));
        occlusion += (scenePos.z >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float visibility = clamp(1.0 - occlusion / float(SSAO_SAMPLES), 0.0, 1.0);
    outAo = mix(1.0, pow(visibility, ao.params.w), ao.params.z);
}
