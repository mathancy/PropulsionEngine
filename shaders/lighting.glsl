// Lights, environment and shadows, shared by shaders/basic.frag and shaders/sky.frag.
// Define LIGHTING_SET (the descriptor set the lighting is bound at) before including this.
// The block must match GpuLighting in include/rendering/LightingData.h.
//
// World: Z up, forward +Y, right +X. The environment is an equirectangular panorama whose middle looks along +Y
// (see include/rendering/EnvironmentMap.h, which does the same mapping on the CPU).

#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

// Hybrid RT: define HYBRID_RT to also get ray-traced shadows and occlusion (the scene's acceleration structure is bound at binding 5)
#ifdef HYBRID_RT
#extension GL_EXT_ray_query : require
#endif

#define MAX_LIGHTS 16
#define MAX_POINT_SHADOWS 4
#define SHADOW_CASCADES 4

struct Light {
    vec4 posType;     // directional: xyz = direction TO the light, w = 0.  point: xyz = position, w = 1
    vec4 colorRange;  // rgb = color * intensity, w = range (point lights)
    vec4 params;      // x = specular scale, y = shadow slot (-1: none), z = light size for soft shadows, w = reach (point lights)
};

layout(set = LIGHTING_SET, binding = 0) uniform LightingData {
    vec4 envParams;  // x = environment intensity, y = environment yaw (radians), z = highest mip of the environment, w = light count
    vec4 sh[9];      // the sky's diffuse lighting (cosine-convolved, already divided by pi)
    Light lights[MAX_LIGHTS];
    mat4 dirShadowMatrix[SHADOW_CASCADES];  // world to each of the sun's shadow maps (nearest the camera first)
    vec4 dirShadowParams[SHADOW_CASCADES];  // per map: x = 1 / map size, y = depth range (world units), z = map width (world units)
    vec4 dirShadowSplits;   // how far from the camera each map reaches (along the view direction)
    vec4 dirShadowGlobal;   // x = number of maps, y = 1 if the sun has a shadow, z = fraction of a map's depth it fades into the next over,
                            // w = how far shadows are drawn
    mat4 pointShadow[MAX_POINT_SHADOWS * 6];  // [slot * 6 + face]: world to that face of a point light's shadow
    vec4 shadowGlobal;      // x = a point shadow face's size in texels, y = their near plane
    vec4 rayParams;         // Hybrid RT: x = shadow rays per light, y = occlusion rays, z = occlusion radius, w = occlusion strength
} lighting;

layout(set = LIGHTING_SET, binding = 1) uniform sampler2D envMap;
layout(set = LIGHTING_SET, binding = 2) uniform sampler2DArray dirShadowMap;
layout(set = LIGHTING_SET, binding = 3) uniform sampler2DArray pointShadowMap;
layout(set = LIGHTING_SET, binding = 4) uniform sampler2D aoMap;
#ifdef HYBRID_RT
layout(set = LIGHTING_SET, binding = 5) uniform accelerationStructureEXT sceneTlas;
#endif

const float ENV_PI = 3.14159265359;

vec3 envRotateZ(vec3 v, float a) {
    float c = cos(a);
    float s = sin(a);
    return vec3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

vec2 envDirectionToUv(vec3 d) {
    float phi = atan(d.x, d.y);  // from +Y toward +X
    return vec2(0.5 + phi / (2.0 * ENV_PI), acos(clamp(d.z, -1.0, 1.0)) / ENV_PI);
}

// The sky seen in a direction. `lod` 0 is the sharp panorama; higher is blurrier (for rougher reflections).
vec3 envRadiance(vec3 dir, float lod) {
    vec3 local = envRotateZ(dir, -lighting.envParams.y);
    return textureLod(envMap, envDirectionToUv(local), lod).rgb * lighting.envParams.x;
}

// The light a diffuse white surface facing n receives from the sky, divided by pi (multiply by albedo).
vec3 envIrradiance(vec3 n) {
    vec3 result = lighting.sh[0].rgb * 0.282095
                + lighting.sh[1].rgb * (0.488603 * n.y)
                + lighting.sh[2].rgb * (0.488603 * n.z)
                + lighting.sh[3].rgb * (0.488603 * n.x)
                + lighting.sh[4].rgb * (1.092548 * n.x * n.y)
                + lighting.sh[5].rgb * (1.092548 * n.y * n.z)
                + lighting.sh[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0))
                + lighting.sh[7].rgb * (1.092548 * n.x * n.z)
                + lighting.sh[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(result, vec3(0.0)) * lighting.envParams.x;
}

// Karis' fit of the split-sum environment BRDF: scale and bias for the specular color.
vec3 envBrdfApprox(vec3 specularColor, float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * ab.x + ab.y;
}

// ---- Soft shadows ---------------------------------------------------------------------------------------------------
// Percentage-closer soft shadows: look for what blocks the light near the point, work out how wide a penumbra a blocker
// that far away makes from a light of that size, then average the shadow map over that width. Shadows are sharp where
// something touches the ground and soften with distance. The sample pattern is turned a different amount at each pixel
// (noise instead of banding).

const int SHADOW_BLOCKER_TAPS = 12;
const int SHADOW_FILTER_TAPS = 24;

float shadowNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec2 shadowDisk(int i, int count, float rotation) {
    float r = sqrt((float(i) + 0.5) / float(count));
    float a = float(i) * 2.39996323 + rotation;
    return r * vec2(cos(a), sin(a));
}

// The sun's light through one of its maps (one layer of dirShadowMap): how much of it reaches a point (1 is all, 0 is none).
// `lightSize` is the width its disc spreads per unit of distance (so a blocker 10 units above the ground makes a penumbra 10
// times that wide). `covered` says whether the point is inside the map (with a margin for the filter to sample within).
float sunShadowMap(int c, vec3 worldPos, vec3 normal, vec3 toLight, float lightSize, float rotation, out bool covered) {
    covered = false;
    float range = lighting.dirShadowParams[c].y;
    float width = lighting.dirShadowParams[c].z;
    float texelWorld = width * lighting.dirShadowParams[c].x;

    float nDotL = clamp(dot(normal, toLight), 0.0, 1.0);
    float sinTheta = sqrt(1.0 - nDotL * nDotL);
    float slope = min(sinTheta / max(nDotL, 0.1), 8.0);

    // Start a little off the surface, along its normal, so a surface doesn't shade itself.
    vec3 p = worldPos + normal * texelWorld * (1.5 + 3.0 * sinTheta);
    vec4 clip = lighting.dirShadowMatrix[c] * vec4(p, 1.0);
    vec3 proj = vec3(clip.xy * 0.5 + 0.5, clip.z);
    const float margin = 0.03;
    if (any(lessThan(proj.xy, vec2(margin))) || any(greaterThan(proj.xy, vec2(1.0 - margin)))) return 1.0;
    covered = true;
    if (proj.z > 1.0 || proj.z < 0.0) return 1.0;

    float layer = float(c);
    float receiver = proj.z;
    float bias = texelWorld * (1.0 + slope) / range;

    float searchWorld = clamp(lightSize * receiver * range, 2.0 * texelWorld, 48.0 * texelWorld);
    float blockerSum = 0.0;
    float blockers = 0.0;
    for (int i = 0; i < SHADOW_BLOCKER_TAPS; i++) {
        vec2 offset = shadowDisk(i, SHADOW_BLOCKER_TAPS, rotation) * (searchWorld / width);
        float d = texture(dirShadowMap, vec3(proj.xy + offset, layer)).r;
        if (d < receiver - bias) {
            blockerSum += d;
            blockers += 1.0;
        }
    }
    if (blockers < 0.5) return 1.0;

    float averageBlocker = blockerSum / blockers;
    float penumbraWorld = (receiver - averageBlocker) * range * lightSize;
    float filterWorld = clamp(penumbraWorld, 1.5 * texelWorld, 64.0 * texelWorld);
    // A wide filter over a tilted surface sees depths that differ across it: allow for that.
    float filterBias = bias + filterWorld * min(slope, 4.0) * 0.5 / range;

    float lit = 0.0;
    for (int i = 0; i < SHADOW_FILTER_TAPS; i++) {
        vec2 offset = shadowDisk(i, SHADOW_FILTER_TAPS, rotation + 1.7) * (filterWorld / width);
        float d = texture(dirShadowMap, vec3(proj.xy + offset, layer)).r;
        lit += (d >= receiver - filterBias) ? 1.0 : 0.0;
    }
    return lit / float(SHADOW_FILTER_TAPS);
}

// The sun: how much of its light reaches a point. The point is looked up in the map for how far it is from the camera
// (`viewDepth`, along the view direction): close up that is a map with small texels, so shadows are sharp near you and
// coarser farther off. Near the end of a map's reach it fades into the next one, and shadows fade out toward the shadow distance.
float sunShadow(vec3 worldPos, vec3 normal, vec3 toLight, float lightSize, float rotation, float viewDepth) {
    if (lighting.dirShadowGlobal.y < 0.5) return 1.0;
    int count = int(lighting.dirShadowGlobal.x);
    float distanceLimit = lighting.dirShadowGlobal.w;
    if (viewDepth >= distanceLimit) return 1.0;

    // The nearest map that reaches this far (and that the point is inside; otherwise try the next, wider one).
    int c = 0;
    while (c < count - 1 && viewDepth > lighting.dirShadowSplits[c]) c++;
    bool covered;
    float visibility = sunShadowMap(c, worldPos, normal, toLight, lightSize, rotation, covered);
    if (!covered && c < count - 1) {
        c++;
        visibility = sunShadowMap(c, worldPos, normal, toLight, lightSize, rotation, covered);
    }

    // Fade into the next map over the last part of this one's reach.
    if (covered && c < count - 1) {
        float start = c > 0 ? lighting.dirShadowSplits[c - 1] : 0.0;
        float blendWidth = lighting.dirShadowGlobal.z * (lighting.dirShadowSplits[c] - start);
        float t = (viewDepth - (lighting.dirShadowSplits[c] - blendWidth)) / max(blendWidth, 1e-4);
        if (t > 0.0) {
            bool nextCovered;
            float next = sunShadowMap(c + 1, worldPos, normal, toLight, lightSize, rotation, nextCovered);
            if (nextCovered) visibility = mix(visibility, next, smoothstep(0.0, 1.0, t));
        }
    }

    // Shadows end gently at the shadow distance, not at a line.
    return mix(visibility, 1.0, smoothstep(0.9 * distanceLimit, distanceLimit, viewDepth));
}

// Which of a point light's six faces a direction from the light falls in (the axis it is most along); matches pointFaceOf in
// ShadowMath.h.
int pointFaceOf(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0 : 1;
    if (a.y >= a.z) return d.y > 0.0 ? 2 : 3;
    return d.z > 0.0 ? 4 : 5;
}

// A world position in the point light's shadow: the map coordinates and layer in xyz, and the distance from the light
// along that face's axis in w.
vec4 pointShadowCoords(int slot, vec3 lightPos, vec3 p) {
    int face = pointFaceOf(p - lightPos);
    int layer = slot * 6 + face;
    vec4 clip = lighting.pointShadow[layer] * vec4(p, 1.0);
    return vec4((clip.xy / clip.w) * 0.5 + 0.5, float(layer), clip.w);
}

float pointShadowLinearDepth(float depth, float nearPlane, float farPlane) {
    return nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
}

// A point light: how much of its light reaches a point. `lightSize` is the bulb's diameter, `reach` how far it shines.
float pointLightShadow(int slot, vec3 lightPos, float reach, float lightSize, vec3 worldPos, vec3 normal, vec3 toLight, float rotation) {
    float nearPlane = lighting.shadowGlobal.y;
    float mapSize = lighting.shadowGlobal.x;

    float distanceToLight = length(worldPos - lightPos);
    if (distanceToLight >= reach) return 1.0;

    float nDotL = clamp(dot(normal, toLight), 0.0, 1.0);
    float sinTheta = sqrt(1.0 - nDotL * nDotL);
    float slope = min(sinTheta / max(nDotL, 0.1), 8.0);

    // One texel of a face, as wide as this at this distance.
    float texelWorld = 2.0 * distanceToLight / mapSize;
    vec3 p = worldPos + normal * texelWorld * (1.5 + 3.0 * sinTheta);

    vec4 center = pointShadowCoords(slot, lightPos, p);
    float receiver = center.w;
    float bias = texelWorld * (1.5 + slope);

    // Directions across the surface (perpendicular to the way the light comes), for spreading the samples.
    vec3 dir = normalize(p - lightPos);
    vec3 t = normalize(cross(dir, abs(dir.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(dir, t);

    float searchWorld = clamp(lightSize, 2.0 * texelWorld, 0.5 * receiver);
    float blockerSum = 0.0;
    float blockers = 0.0;
    for (int i = 0; i < SHADOW_BLOCKER_TAPS; i++) {
        vec2 o = shadowDisk(i, SHADOW_BLOCKER_TAPS, rotation) * searchWorld;
        vec4 c = pointShadowCoords(slot, lightPos, p + t * o.x + b * o.y);
        float blocker = pointShadowLinearDepth(texture(pointShadowMap, c.xyz).r, nearPlane, reach);
        if (blocker < c.w - bias) {
            blockerSum += blocker;
            blockers += 1.0;
        }
    }
    if (blockers < 0.5) return 1.0;

    float averageBlocker = blockerSum / blockers;
    float penumbraWorld = (receiver - averageBlocker) * lightSize / averageBlocker;
    float filterWorld = clamp(penumbraWorld, 1.5 * texelWorld, 48.0 * texelWorld);
    float filterBias = bias + filterWorld * min(slope, 4.0) * 0.5;

    float lit = 0.0;
    for (int i = 0; i < SHADOW_FILTER_TAPS; i++) {
        vec2 o = shadowDisk(i, SHADOW_FILTER_TAPS, rotation + 1.7) * filterWorld;
        vec4 c = pointShadowCoords(slot, lightPos, p + t * o.x + b * o.y);
        float blocker = pointShadowLinearDepth(texture(pointShadowMap, c.xyz).r, nearPlane, reach);
        lit += (blocker >= c.w - filterBias) ? 1.0 : 0.0;
    }
    return lit / float(SHADOW_FILTER_TAPS);
}

#ifdef HYBRID_RT
// ---- Ray-traced shadows and occlusion -------------------------------------------------------------------------------------
// Instead of looking in a shadow map, shoot rays from the surface toward the light and see how many get there. A light has a
// size (the sun's disc, a bulb's sphere), and the rays go to different points on it, so edges are soft in the right way: sharp
// where something touches the ground, wider the farther the blocker is. The points are spread over the light's disc and turned a
// different amount at each pixel, so what is left of the sampling shows as fine noise, not bands.

// Is anything between the origin and origin + dir * tMax?
bool rtOccluded(vec3 origin, vec3 dir, float tMax) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, sceneTlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.0, dir, tMax);
    while (rayQueryProceedEXT(query)) {}
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

// Two directions perpendicular to n (and to each other)
void rtBasis(vec3 n, out vec3 t, out vec3 b) {
    vec3 up = abs(n.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// How much of a light reaches a point: 1 is all of it, 0 none. `normal` is the surface's own (not mapped) normal.
float rtLightVisibility(Light light, vec3 worldPos, vec3 normal, float rotation) {
    int rays = clamp(int(lighting.rayParams.x + 0.5), 1, 64);
    float size = light.params.z;
    vec3 origin = worldPos + normal * 0.003;  // off the surface, so it doesn't shadow itself

    float lit = 0.0;
    if (light.posType.w < 0.5) {
        // The sun: rays toward points on its disc
        vec3 L = normalize(light.posType.xyz);
        vec3 t, b;
        rtBasis(L, t, b);
        for (int i = 0; i < rays; i++) {
            vec2 d = shadowDisk(i, rays, rotation) * (0.5 * size);
            lit += rtOccluded(origin, normalize(L + t * d.x + b * d.y), 100000.0) ? 0.0 : 1.0;
        }
    } else {
        // A bulb: rays toward points on a disc facing the surface, as wide as the bulb (size is its diameter)
        vec3 toCenter = light.posType.xyz - origin;
        vec3 L = normalize(toCenter);
        vec3 t, b;
        rtBasis(L, t, b);
        for (int i = 0; i < rays; i++) {
            vec2 d = shadowDisk(i, rays, rotation) * (0.5 * size);
            vec3 toPoint = toCenter + t * d.x + b * d.y;
            float dist = length(toPoint);
            lit += rtOccluded(origin, toPoint / dist, dist - 0.01) ? 0.0 : 1.0;
        }
    }
    return lit / float(rays);
}

// Ambient occlusion: how much of the sky above a point is open within the occlusion radius (1 is fully open). Rays are spread
// over the hemisphere, more of them near the normal (cosine weighted, as the sky's light is).
float rtAmbientOcclusion(vec3 worldPos, vec3 normal, float rotation) {
    int rays = clamp(int(lighting.rayParams.y + 0.5), 1, 64);
    float radius = lighting.rayParams.z;
    float strength = lighting.rayParams.w;
    vec3 origin = worldPos + normal * 0.003;
    vec3 t, b;
    rtBasis(normal, t, b);

    float open = 0.0;
    for (int i = 0; i < rays; i++) {
        vec2 d = shadowDisk(i, rays, rotation);
        vec3 dir = t * d.x + b * d.y + normal * sqrt(max(1.0 - dot(d, d), 0.0));
        open += rtOccluded(origin, dir, radius) ? 0.0 : 1.0;
    }
    return mix(1.0, open / float(rays), strength);
}
#endif

#endif
