#version 450

// Vertical dilation pass
// Spreads the mask vertically using a max filter

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D inputMask;

layout(push_constant) uniform PushConstants {
    vec2 texelSize;  // 1.0 / viewportSize
    int radiusX;     // horizontal radius (for reference, not used here)
    int radiusY;     // vertical radius (adjusted for aspect ratio)
} pc;

void main() {
    float maxVal = 0.0;
    
    for (int y = -pc.radiusY; y <= pc.radiusY; ++y) {
        vec2 offset = vec2(0.0, float(y)) * pc.texelSize;
        maxVal = max(maxVal, texture(inputMask, fragTexCoord + offset).r);
    }
    
    outColor = vec4(maxVal, maxVal, maxVal, 1.0);
}
