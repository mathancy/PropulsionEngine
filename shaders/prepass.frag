#version 450

// The pass before the scene pass: writes each surface's normal (in view space, packed into two numbers) so ambient
// occlusion can be worked out from depth and normals. Paired with basic.vert, so it shares the per-object uniforms.

layout(location = 0) in vec3 fragNormal;

layout(location = 0) out vec2 outNormal;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 colorAndExposure;
    vec4 materialParams;
    vec4 textureFlags;
    vec4 textureFlags2;
    vec4 invertFlags;
    vec4 textureStrengths;
    vec4 emissiveColor;
    vec4 heightParams;
    vec3 cameraPos;
    float padding;
} ubo;

// Octahedral packing: a unit vector in two numbers.
vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = n.xy;
    if (n.z < 0.0) {
        e = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return e;
}

void main() {
    vec3 normal = normalize(mat3(ubo.view) * normalize(fragNormal));
    outNormal = octEncode(normal);
}
