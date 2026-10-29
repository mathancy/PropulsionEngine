#version 450

// The vertex shader of the pre-pass: only what the normal needs (see prepass.frag). Shares the scene shader's
// per-object uniforms.

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

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 fragNormal;

void main() {
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
}
