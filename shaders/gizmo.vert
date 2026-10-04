#version 450

// Per-frame uniform buffer (view/projection stay constant)
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 projection;
} ubo;

// Per-draw push constants (model matrix and color change per axis)
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;  // rgb = color, a = alpha (0.4 default, 0.8 highlighted)
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragAlpha;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = ubo.projection * ubo.view * worldPos;
    fragNormal = mat3(transpose(inverse(push.model))) * inNormal;
    fragColor = push.color.rgb;
    fragAlpha = push.color.a;
}
