#version 450

// Push constants for model matrix and color
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} push;

// Uniform buffer for view/projection
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragNormal = mat3(transpose(inverse(push.model))) * inNormal;
    fragColor = push.color.rgb;
}
