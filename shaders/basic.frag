#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragExposure;
layout(location = 4) in float fragIsSelected;
layout(location = 5) in vec2 fragTexCoord;
layout(location = 6) in float fragUseAlbedoTexture;

layout(location = 0) out vec4 outColor;

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

// PBR texture samplers
layout(binding = 1) uniform sampler2D albedoTexture;
layout(binding = 2) uniform sampler2D metallicTexture;
layout(binding = 3) uniform sampler2D roughnessTexture;
layout(binding = 4) uniform sampler2D normalTexture;
layout(binding = 5) uniform sampler2D aoTexture;
layout(binding = 6) uniform sampler2D emissiveTexture;
layout(binding = 7) uniform sampler2D heightTexture;

const float PI = 3.14159265359;

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0001);
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / max(denom, 0.0001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel Function (Schlick)
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Calculate TBN matrix using screen-space derivatives for normal mapping
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    // Get edge vectors of the pixel triangle
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    // Solve the linear system
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // Construct a scale-invariant frame
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

vec3 perturbNormal(vec3 N, vec3 V, vec2 texcoord, float strength, bool invert) {
    vec3 map = texture(normalTexture, texcoord).xyz;
    map = map * 2.0 - 1.0;
    
    // Invert normal map (flip green channel for DirectX/OpenGL convention swap)
    if (invert) {
        map.y = -map.y;
    }
    
    // Apply strength - lerp between flat normal (0,0,1) and sampled normal
    map.xy *= strength;
    map = normalize(map);
    
    mat3 TBN = cotangentFrame(N, -V, texcoord);
    return normalize(TBN * map);
}

// Simple Parallax Mapping - offsets texture coordinates based on height map
// Uses a single sample approach which is artifact-free but less accurate at extreme angles
vec2 parallaxMapping(vec2 texCoords, vec3 viewDir, float heightScale, bool invertHeight) {
    // Scale down the height scale for subtle effect
    // User input of 0.0-1.0 maps to reasonable displacement
    float actualScale = heightScale * 0.02;
    
    // Sample height map
    float height = texture(heightTexture, texCoords).r;
    if (invertHeight) {
        height = 1.0 - height;
    }
    
    // Offset is based on view angle and height
    // We shift UVs in the opposite direction of view to create depth illusion
    vec2 offset = viewDir.xy * (height * actualScale);
    
    return texCoords - offset;
}

void main() {
    // Calculate view direction for parallax mapping
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    
    // Apply parallax mapping if height texture is enabled
    vec2 texCoord = fragTexCoord;
    if (ubo.textureFlags2.z > 0.5) {
        // Calculate tangent-space view direction using cotangent frame
        mat3 TBN = cotangentFrame(N, -V, fragTexCoord);
        mat3 TBN_inv = transpose(TBN); // Inverse of orthonormal matrix is its transpose
        vec3 tangentViewDir = normalize(TBN_inv * V);
        
        float heightScale = ubo.heightParams.x;
        bool invertHeight = ubo.heightParams.y > 0.5;
        texCoord = parallaxMapping(fragTexCoord, tangentViewDir, heightScale, invertHeight);
        
        // Discard fragments outside texture bounds (prevents edge artifacts)
        if (texCoord.x > 1.0 || texCoord.y > 1.0 || texCoord.x < 0.0 || texCoord.y < 0.0) {
            discard;
        }
    }
    
    // Sample albedo (base color)
    vec3 albedo;
    if (ubo.textureFlags.x > 0.5) {
        albedo = pow(texture(albedoTexture, texCoord).rgb, vec3(2.2)); // sRGB to linear
    } else {
        albedo = pow(fragColor, vec3(2.2)); // sRGB to linear
    }

    // Sample metallic
    float metallic;
    if (ubo.textureFlags.y > 0.5) {
        metallic = texture(metallicTexture, texCoord).r;
        if (ubo.invertFlags.x > 0.5) {
            metallic = 1.0 - metallic;
        }
        // Apply strength (lerp between default and sampled value)
        metallic = mix(ubo.materialParams.x, metallic, ubo.textureStrengths.x);
    } else {
        metallic = ubo.materialParams.x;
    }

    // Sample roughness
    float roughness;
    if (ubo.textureFlags.z > 0.5) {
        roughness = texture(roughnessTexture, texCoord).r;
        if (ubo.invertFlags.y > 0.5) {
            roughness = 1.0 - roughness;
        }
        // Apply strength (lerp between default and sampled value)
        roughness = mix(ubo.materialParams.y, roughness, ubo.textureStrengths.y);
    } else {
        roughness = ubo.materialParams.y;
    }
    roughness = max(roughness, 0.04); // Prevent divide by zero artifacts

    // Sample ambient occlusion
    float ao;
    if (ubo.textureFlags2.x > 0.5) {
        ao = texture(aoTexture, texCoord).r;
        if (ubo.invertFlags.w > 0.5) {
            ao = 1.0 - ao;
        }
        // Apply strength (lerp between 1.0 (no occlusion) and sampled value)
        ao = mix(1.0, ao, ubo.textureStrengths.w);
    } else {
        ao = ubo.materialParams.z;
    }

    // Calculate normal (with optional normal map)
    // N and V were already calculated at the start for parallax mapping
    if (ubo.textureFlags.w > 0.5) {
        float normalStrength = ubo.textureStrengths.z;
        bool invertNormal = ubo.invertFlags.z > 0.5;
        N = perturbNormal(N, V, texCoord, normalStrength, invertNormal);
    }

    // Calculate reflectance at normal incidence
    // For dielectrics use 0.04, for metals use albedo color
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // Reflectance equation
    vec3 Lo = vec3(0.0);

    // Main directional light
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 lightColor = vec3(1.0, 0.98, 0.95) * 3.0; // Slightly warm, bright

    vec3 L = lightDir;
    vec3 H = normalize(V + L);
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);

    Lo += (kD * albedo / PI + specular) * lightColor * NdotL;

    // Fill light (softer, from opposite direction)
    vec3 fillLightDir = normalize(vec3(-0.3, 0.5, -0.5));
    vec3 fillLightColor = vec3(0.4, 0.5, 0.6) * 0.5; // Cool fill

    vec3 L2 = fillLightDir;
    vec3 H2 = normalize(V + L2);
    float NdotL2 = max(dot(N, L2), 0.0);
    
    // Simplified fill (just diffuse)
    Lo += kD * albedo / PI * fillLightColor * NdotL2;

    // Ambient lighting (simple hemisphere approximation)
    vec3 ambient = vec3(0.03) * albedo * ao;

    // Add emissive
    vec3 emissive = vec3(0.0);
    if (ubo.textureFlags2.y > 0.5) {
        // Emissive from texture, use emissiveColor.a as strength multiplier
        emissive = pow(texture(emissiveTexture, texCoord).rgb, vec3(2.2)) * ubo.emissiveColor.a;
    } else if (length(ubo.emissiveColor.rgb) > 0.001) {
        // Emissive from solid color (emissiveColor.a not used here, color is already the desired intensity)
        emissive = pow(ubo.emissiveColor.rgb, vec3(2.2));
    }

    vec3 color = ambient + Lo + emissive;

    // Apply exposure
    color *= fragExposure;

    // Tone mapping (ACES approximation)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
