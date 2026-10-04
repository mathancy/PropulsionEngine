#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Convert sRGB color to linear space
    vec3 linearColor = pow(fragColor, vec3(2.2));
    
    // Simple three-point lighting for nice thumbnail appearance
    vec3 normal = normalize(fragNormal);
    
    // Key light (main light from upper right)
    vec3 keyLightDir = normalize(vec3(0.5, 0.7, 0.8));
    float keyDiff = max(dot(normal, keyLightDir), 0.0);
    
    // Fill light (softer light from left)
    vec3 fillLightDir = normalize(vec3(-0.6, 0.3, 0.5));
    float fillDiff = max(dot(normal, fillLightDir), 0.0) * 0.4;
    
    // Rim light (from behind for edge definition)
    vec3 rimLightDir = normalize(vec3(0.0, -0.5, -0.8));
    float rimDiff = max(dot(normal, rimLightDir), 0.0) * 0.2;
    
    // Ambient
    vec3 ambient = linearColor * 0.25;
    
    // Combine lighting
    vec3 result = ambient + linearColor * (keyDiff * 0.6 + fillDiff + rimDiff);
    
    // Apply gamma correction
    result = pow(result, vec3(1.0/2.2));
    
    outColor = vec4(result, 1.0);
}
