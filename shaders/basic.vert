#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 colorAndExposure;      // RGB = color, A = exposure
    vec4 materialParams;        // x=metallic, y=roughness, z=ao, w=unused
    vec4 textureFlags;          // x=useAlbedo, y=useMetallic, z=useRoughness, w=useNormal
    vec4 textureFlags2;         // x=useAO, y=useEmissive, z=useHeight, w=unused
    vec4 invertFlags;           // x=invertMetallic, y=invertRoughness, z=invertNormal, w=invertAO
    vec4 textureStrengths;      // x=metallicStr, y=roughnessStr, z=normalStr, w=aoStr
    vec4 emissiveColor;         // RGB = emissive color, A = emissive texture strength
    vec4 heightParams;          // x=heightScale, y=invertHeight, z=unused, w=unused
    vec3 cameraPos;             // Camera position for specular calculations
    float padding;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out float fragExposure;
layout(location = 4) out float fragIsSelected;
layout(location = 5) out vec2 fragTexCoord;
layout(location = 6) out float fragUseAlbedoTexture;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    gl_Position = ubo.projection * ubo.view * worldPos;
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
    fragColor = ubo.colorAndExposure.rgb;
    fragExposure = ubo.colorAndExposure.a;
    fragIsSelected = 0.0; // No longer used
    fragTexCoord = inTexCoord;
    fragUseAlbedoTexture = ubo.textureFlags.x;
}
