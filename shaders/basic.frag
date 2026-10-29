#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The lights and the environment come from the lighting set (see SceneLighting).
#define LIGHTING_SET 1
#include "lighting.glsl"

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
    ivec4 textureSlots;         // places in the texture table: x=base colour, y=metallic, z=roughness, w=normal (the flags say which are used)
    ivec4 textureSlots2;        // x=occlusion, y=emissive, z=height, w=opacity (-1: none)
    vec4 surfaceParams;         // x=blend mode (0 opaque, 1 masked, 2 translucent), y=opacity, z=mask cutoff, w=1 if two sided
} ubo;

// Every material texture, in one array (see rendering/TextureTable.h). The object's block says which slots it uses. Colours (base
// colour, emissive) are read through slots whose image is decoded from sRGB; data (everything else) through slots that are read as stored.
layout(set = 2, binding = 0) uniform sampler2D textures[];

vec4 sampleSlot(int slot, vec2 uv) {
    return texture(textures[nonuniformEXT(slot)], uv);
}

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
    vec3 map = sampleSlot(ubo.textureSlots.w, texcoord).xyz;
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
    float height = sampleSlot(ubo.textureSlots2.z, texCoords).r;
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
    // A two-sided surface seen from behind faces the other way
    if (ubo.surfaceParams.w > 0.5 && !gl_FrontFacing) N = -N;
    vec3 Ngeo = N;  // the surface itself (before a normal map), for shadows
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
        albedo = sampleSlot(ubo.textureSlots.x, texCoord).rgb;  // already decoded from sRGB by the texture's view
    } else {
        albedo = pow(fragColor, vec3(2.2)); // sRGB to linear
    }

    // Sample metallic
    float metallic;
    if (ubo.textureFlags.y > 0.5) {
        metallic = sampleSlot(ubo.textureSlots.y, texCoord).r;
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
        roughness = sampleSlot(ubo.textureSlots.z, texCoord).r;
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
        ao = sampleSlot(ubo.textureSlots2.x, texCoord).r;
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
    float NdotV = max(dot(N, V), 0.0001);

    // How much of the surface is there (masked: a point is either there or not) and how much light it stops (translucent).
    // The alpha comes from the Opacity texture, else from the base colour texture's alpha channel.
    const int blendMode = int(ubo.surfaceParams.x + 0.5);
    float alpha = 1.0;
    if (blendMode != 0) {
        if (ubo.textureSlots2.w >= 0) alpha = sampleSlot(ubo.textureSlots2.w, texCoord).r;
        else if (ubo.textureFlags.x > 0.5) alpha = sampleSlot(ubo.textureSlots.x, texCoord).a;
        if (blendMode == 1) {
            if (alpha < ubo.surfaceParams.z) discard;  // cut away: not drawn, not in the depth, seen through
            alpha = 1.0;
        } else {
            alpha *= ubo.surfaceParams.y;
        }
    }

    // Reflectance equation: every light in the scene (or the default rig when it has none)
    vec3 LoDiffuse = vec3(0.0);   // the light the surface scatters (stopped by a translucent surface's opacity)
    vec3 LoSpecular = vec3(0.0);  // the light it reflects (a glass surface reflects however clear it is)
    int lightCount = int(lighting.envParams.w);
    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        Light light = lighting.lights[i];
        vec3 L;
        vec3 radiance = light.colorRange.rgb;
        if (light.posType.w < 0.5) {
            // Directional: the same everywhere
            L = normalize(light.posType.xyz);
        } else {
            // Point: falls off with the square of the distance, and is gone at its range
            vec3 toLight = light.posType.xyz - fragWorldPos;
            float dist2 = max(dot(toLight, toLight), 0.0001);
            L = toLight * inversesqrt(dist2);
            float attenuation = 1.0 / dist2;
            float range = light.colorRange.w;
            if (range > 0.0) {
                float r2 = dist2 / (range * range);
                float window = clamp(1.0 - r2 * r2, 0.0, 1.0);
                attenuation *= window * window;
            }
            radiance *= attenuation;
        }

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Shadows (only lights that were given a shadow map)
        if (light.params.y > -0.5) {
            float rotation = shadowNoise(gl_FragCoord.xy) * 6.2831853;
            float visibility;
#ifdef HYBRID_RT
            visibility = rtLightVisibility(light, fragWorldPos, Ngeo, rotation + float(i) * 2.399963);
#else
            if (light.posType.w < 0.5) {
                visibility = sunShadow(fragWorldPos, Ngeo, L, light.params.z, rotation, -(ubo.view * vec4(fragWorldPos, 1.0)).z);
            } else {
                visibility = pointLightShadow(int(light.params.y + 0.5), light.posType.xyz, light.params.w, light.params.z, fragWorldPos, Ngeo, L, rotation);
            }
#endif
            radiance *= visibility;
        }

        vec3 H = normalize(V + L);

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * NdotV * NdotL + 0.0001;
        vec3 specular = numerator / denominator * light.params.x;

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        LoDiffuse += kD * albedo / PI * radiance * NdotL;
        LoSpecular += specular * radiance * NdotL;
    }

    // Image-based lighting: the sky lights the surface from every direction (diffuse), and shiny surfaces reflect it
    vec3 Fr = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
    vec3 kDEnv = (vec3(1.0) - Fr) * (1.0 - metallic);
    vec3 diffuseIBL = envIrradiance(N) * albedo * kDEnv;

    vec3 R = reflect(-V, N);
    vec3 reflected = envRadiance(R, roughness * lighting.envParams.z);
    vec3 specularIBL = reflected * envBrdfApprox(F0, roughness, NdotV);

    // Occlusion dims the sky's diffuse light fully, and its reflections a little less (Lagarde's specular occlusion)
    float specularAO = clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
    // Ambient occlusion from the depth buffer (white where there is none)
#ifdef HYBRID_RT
    // Hybrid RT: occlusion from rays, where the normal view reads the depth-based one
    float screenOcclusion = rtAmbientOcclusion(fragWorldPos, Ngeo, shadowNoise(gl_FragCoord.xy + vec2(17.0, 3.0)) * 6.2831853);
#else
    float screenOcclusion = texture(aoMap, gl_FragCoord.xy / vec2(textureSize(aoMap, 0))).r;
#endif
    vec3 ambientDiffuse = diffuseIBL * ao * screenOcclusion;
    vec3 ambientSpecular = specularIBL * specularAO * screenOcclusion;

    // Add emissive
    vec3 emissive = vec3(0.0);
    if (ubo.textureFlags2.y > 0.5) {
        // Emissive from texture, use emissiveColor.a as strength multiplier
        emissive = sampleSlot(ubo.textureSlots2.y, texCoord).rgb * ubo.emissiveColor.a;
    } else if (length(ubo.emissiveColor.rgb) > 0.001) {
        // Emissive from solid color (emissiveColor.a not used here, color is already the desired intensity)
        emissive = pow(ubo.emissiveColor.rgb, vec3(2.2));
    }

    // A translucent surface is blended over what is behind it with its colour already multiplied by the opacity (so reflections
    // keep their strength); everything else is just added up.
    vec3 color;
    if (blendMode == 2) {
        color = (ambientDiffuse + LoDiffuse + emissive) * alpha + ambientSpecular + LoSpecular;
    } else {
        color = ambientDiffuse + ambientSpecular + LoDiffuse + LoSpecular + emissive;
    }

    // Apply exposure
    color *= fragExposure;

    // Tone mapping (ACES approximation)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, blendMode == 2 ? alpha : 1.0);
}
