#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;      // The rendered scene
layout(binding = 1) uniform sampler2D selectionMask;   // Original mask
layout(binding = 2) uniform sampler2D dilatedMask;     // Dilated mask

layout(push_constant) uniform PushConstants {
    vec2 texelSize;
    float intensity;
    float _pad;
    vec4 outlineColor;
} pc;

void main() {
    vec3 sceneCol = texture(sceneColor, fragTexCoord).rgb;
    
    // Sample original mask with slight blur
    float src = 0.0;
    src += texture(selectionMask, fragTexCoord + vec2(-pc.texelSize.x, -pc.texelSize.y)).r * 1.0;
    src += texture(selectionMask, fragTexCoord + vec2( 0.0,            -pc.texelSize.y)).r * 2.0;
    src += texture(selectionMask, fragTexCoord + vec2( pc.texelSize.x, -pc.texelSize.y)).r * 1.0;
    src += texture(selectionMask, fragTexCoord + vec2(-pc.texelSize.x,  0.0           )).r * 2.0;
    src += texture(selectionMask, fragTexCoord + vec2( 0.0,             0.0           )).r * 4.0;
    src += texture(selectionMask, fragTexCoord + vec2( pc.texelSize.x,  0.0           )).r * 2.0;
    src += texture(selectionMask, fragTexCoord + vec2(-pc.texelSize.x,  pc.texelSize.y)).r * 1.0;
    src += texture(selectionMask, fragTexCoord + vec2( 0.0,             pc.texelSize.y)).r * 2.0;
    src += texture(selectionMask, fragTexCoord + vec2( pc.texelSize.x,  pc.texelSize.y)).r * 1.0;
    src /= 16.0;
    
    // Sample dilated mask with blur to round corners
    float dilated = 0.0;
    float totalWeight = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            float dist = length(vec2(x, y));
            if (dist <= 2.5) {
                float weight = 1.0 - (dist / 3.0);
                weight = weight * weight;
                vec2 offset = vec2(float(x) * pc.texelSize.x, float(y) * pc.texelSize.y);
                dilated += texture(dilatedMask, fragTexCoord + offset).r * weight;
                totalWeight += weight;
            }
        }
    }
    dilated /= totalWeight;
    
    // Ring mask: pixels added by dilation
    float outlineMask = clamp(dilated - src, 0.0, 1.0);
    
    // Smooth edge for anti-aliasing
    outlineMask = smoothstep(0.1, 0.8, outlineMask);
    
    // Blend outline color over scene
    vec3 finalCol = mix(sceneCol, pc.outlineColor.rgb, outlineMask * pc.intensity);
    
    outColor = vec4(finalCol, 1.0);
}
