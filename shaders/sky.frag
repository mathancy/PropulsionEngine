#version 450
#extension GL_GOOGLE_include_directive : require

// The sky behind the scene: the environment, seen in the direction each pixel looks.

#define LIGHTING_SET 0
#include "lighting.glsl"

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    mat4 invViewProjection;
    vec4 params;  // x = exposure
} pc;

void main() {
    // Two points on the pixel's ray, one nearer than the other: the ray's direction.
    vec4 nearPoint = pc.invViewProjection * vec4(ndc, 0.0, 1.0);
    vec4 farPoint = pc.invViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);

    vec3 color = envRadiance(dir, 0.0) * pc.params.x;

    // Same exposure, tone mapping and gamma as the scene's shader.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
