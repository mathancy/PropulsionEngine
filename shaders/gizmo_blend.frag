#version 450

layout(binding = 0) uniform sampler2D gizmoTexture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    // Sample the resolved gizmo texture and output it
    // The pipeline handles alpha blending with the composite
    outColor = texture(gizmoTexture, fragTexCoord);
}
