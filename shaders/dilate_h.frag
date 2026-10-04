#version 450

// Horizontal dilation pass
// Spreads the mask horizontally using a max filter

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D inputMask;

layout(push_constant) uniform PushConstants {
    vec2 texelSize;  // 1.0 / viewportSize
    int radiusX;     // horizontal radius (adjusted for aspect ratio)
    int radiusY;     // vertical radius (for reference, not used here)
} pc;

void main() {
    float maxVal = 0.0;
    
    for (int x = -pc.radiusX; x <= pc.radiusX; ++x) {
        vec2 offset = vec2(float(x), 0.0) * pc.texelSize;
        maxVal = max(maxVal, texture(inputMask, fragTexCoord + offset).r);
    }
    
    outColor = vec4(maxVal, maxVal, maxVal, 1.0);
}
