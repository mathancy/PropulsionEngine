#pragma once

/**
 * EnvironmentMap.h
 * Everything about an environment (an HDRI, or the default sky) that needs no GPU: how a direction maps to a spot in
 * an equirectangular panorama, the diffuse lighting the sky gives (9 spherical harmonic coefficients), and the chain
 * of blurrier copies that shiny surfaces reflect. The renderer uploads what this makes (rendering/SceneLighting.h).
 *
 * Conventions (the engine's world): Z is up, forward is +Y, right is +X. The middle of the panorama is the +Y
 * direction, so a camera that looks along +Y at the start sees the middle of the picture; turning toward +X moves
 * right across it. The top row of the picture is straight up (+Z).
 *
 *   u = 0.5 + atan2(x, y) / 2pi        v = acos(z) / pi
 *
 * Images are linear RGB floats. Row 0 is the top row, as in the file.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <vector>

namespace Propulsion {
namespace Environment {

constexpr float kPi = 3.14159265358979323846f;

struct Image {
    int width = 0;
    int height = 0;
    std::vector<float> rgb;  // width * height * 3

    Image() = default;
    Image(int w, int h) : width(w), height(h), rgb(static_cast<size_t>(w) * static_cast<size_t>(h) * 3, 0.0f) {}

    glm::vec3 get(int x, int y) const {
        const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
        return glm::vec3(rgb[i], rgb[i + 1], rgb[i + 2]);
    }
    void set(int x, int y, const glm::vec3& c) {
        const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
        rgb[i] = c.x;
        rgb[i + 1] = c.y;
        rgb[i + 2] = c.z;
    }
};

// ---- Direction <-> panorama -------------------------------------------------------------------------------------

inline glm::vec2 directionToUv(const glm::vec3& direction) {
    const glm::vec3 n = glm::normalize(direction);
    const float u = 0.5f + std::atan2(n.x, n.y) / (2.0f * kPi);
    const float v = std::acos(std::clamp(n.z, -1.0f, 1.0f)) / kPi;
    return glm::vec2(u, v);
}

inline glm::vec3 uvToDirection(const glm::vec2& uv) {
    const float phi = (uv.x - 0.5f) * 2.0f * kPi;
    const float theta = uv.y * kPi;
    const float s = std::sin(theta);
    return glm::vec3(s * std::sin(phi), s * std::cos(phi), std::cos(theta));
}

/// Turn a direction about Z (up) by `radians`, counter-clockwise seen from above.
inline glm::vec3 rotateZ(const glm::vec3& v, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return glm::vec3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

/// The panorama in a direction, blended between the four nearest pixels (wrapping sideways, clamped at the poles).
/// `yawRadians` turns the environment itself about Z, so the sky content seen in direction d is the content at
/// rotateZ(d, -yaw).
inline glm::vec3 sample(const Image& img, const glm::vec3& direction, float yawRadians = 0.0f) {
    if (img.width <= 0 || img.height <= 0) return glm::vec3(0.0f);
    const glm::vec2 uv = directionToUv(yawRadians != 0.0f ? rotateZ(direction, -yawRadians) : direction);
    const float fx = uv.x * static_cast<float>(img.width) - 0.5f;
    const float fy = uv.y * static_cast<float>(img.height) - 0.5f;
    const float x0f = std::floor(fx), y0f = std::floor(fy);
    const float tx = fx - x0f, ty = fy - y0f;
    auto wrapX = [&](int x) { return ((x % img.width) + img.width) % img.width; };
    auto clampY = [&](int y) { return std::clamp(y, 0, img.height - 1); };
    const int x0 = wrapX(static_cast<int>(x0f)), x1 = wrapX(static_cast<int>(x0f) + 1);
    const int y0 = clampY(static_cast<int>(y0f)), y1 = clampY(static_cast<int>(y0f) + 1);
    const glm::vec3 top = glm::mix(img.get(x0, y0), img.get(x1, y0), tx);
    const glm::vec3 bottom = glm::mix(img.get(x0, y1), img.get(x1, y1), tx);
    return glm::mix(top, bottom, ty);
}

// ---- Building images ---------------------------------------------------------------------------------------------

/// Half the size in each direction, averaging each 2x2 block.
inline Image downsample2x(const Image& src) {
    Image out((std::max)(1, src.width / 2), (std::max)(1, src.height / 2));
    for (int y = 0; y < out.height; y++) {
        for (int x = 0; x < out.width; x++) {
            const int x0 = (std::min)(x * 2, src.width - 1), x1 = (std::min)(x * 2 + 1, src.width - 1);
            const int y0 = (std::min)(y * 2, src.height - 1), y1 = (std::min)(y * 2 + 1, src.height - 1);
            out.set(x, y, (src.get(x0, y0) + src.get(x1, y0) + src.get(x0, y1) + src.get(x1, y1)) * 0.25f);
        }
    }
    return out;
}

/// The panorama again at another size, looked up by direction so it works for any aspect ratio.
inline Image resampleTo(const Image& src, int width, int height) {
    if (src.width == width && src.height == height) return src;
    Image out(width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const glm::vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                               (static_cast<float>(y) + 0.5f) / static_cast<float>(height));
            out.set(x, y, sample(src, uvToDirection(uv)));
        }
    }
    return out;
}

struct SkyGradient {
    glm::vec3 zenith{0.30f, 0.45f, 0.70f};
    glm::vec3 horizon{0.65f, 0.70f, 0.75f};
    glm::vec3 ground{0.18f, 0.17f, 0.16f};
};

/// The default sky: blue overhead, pale at the horizon, a dim ground below.
inline Image makeGradientSky(int width, const SkyGradient& g = SkyGradient()) {
    Image out(width, (std::max)(1, width / 2));
    for (int y = 0; y < out.height; y++) {
        const float z = std::cos((static_cast<float>(y) + 0.5f) / static_cast<float>(out.height) * kPi);
        glm::vec3 c;
        if (z >= 0.0f) c = glm::mix(g.horizon, g.zenith, std::pow(z, 0.6f));
        else c = glm::mix(g.horizon, g.ground, std::pow(-z, 0.4f));
        for (int x = 0; x < out.width; x++) out.set(x, y, c);
    }
    return out;
}

// ---- Diffuse lighting from the sky: spherical harmonics ----------------------------------------------------------

struct Sh9 {
    glm::vec3 c[9]{};  // already multiplied by the cosine lobe and divided by pi: evalDiffuseSh gives irradiance / pi
};

inline void shBasis(const glm::vec3& n, float out[9]) {
    out[0] = 0.282095f;
    out[1] = 0.488603f * n.y;
    out[2] = 0.488603f * n.z;
    out[3] = 0.488603f * n.x;
    out[4] = 1.092548f * n.x * n.y;
    out[5] = 1.092548f * n.y * n.z;
    out[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    out[7] = 1.092548f * n.x * n.z;
    out[8] = 0.546274f * (n.x * n.x - n.y * n.y);
}

/// The diffuse lighting the picture gives, for a sky turned by `yawRadians`. A small image (64 pixels wide) is plenty.
inline Sh9 projectDiffuseSh(const Image& img, float yawRadians = 0.0f) {
    double acc[9][3] = {};
    for (int y = 0; y < img.height; y++) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(img.height);
        const float solidAngle = (2.0f * kPi / static_cast<float>(img.width)) * (kPi / static_cast<float>(img.height)) * std::sin(v * kPi);
        for (int x = 0; x < img.width; x++) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(img.width);
            const glm::vec3 d = rotateZ(uvToDirection(glm::vec2(u, v)), yawRadians);
            float basis[9];
            shBasis(d, basis);
            const glm::vec3 radiance = img.get(x, y);
            for (int i = 0; i < 9; i++) {
                acc[i][0] += static_cast<double>(radiance.x * basis[i] * solidAngle);
                acc[i][1] += static_cast<double>(radiance.y * basis[i] * solidAngle);
                acc[i][2] += static_cast<double>(radiance.z * basis[i] * solidAngle);
            }
        }
    }
    // The cosine lobe: bands 0, 1, 2 are scaled by pi, 2pi/3, pi/4; dividing by pi leaves 1, 2/3, 1/4.
    const float band[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    Sh9 out;
    for (int i = 0; i < 9; i++) {
        out.c[i] = glm::vec3(static_cast<float>(acc[i][0]), static_cast<float>(acc[i][1]), static_cast<float>(acc[i][2])) * band[i];
    }
    return out;
}

/// The light a perfectly diffuse white surface facing `normal` receives, divided by pi (so multiply by the albedo).
inline glm::vec3 evalDiffuseSh(const Sh9& sh, const glm::vec3& normal) {
    float basis[9];
    shBasis(glm::normalize(normal), basis);
    glm::vec3 result(0.0f);
    for (int i = 0; i < 9; i++) result += sh.c[i] * basis[i];
    return glm::max(result, glm::vec3(0.0f));
}

// ---- Reflections: a chain of blurrier copies ---------------------------------------------------------------------

/// Level 0 is the sky itself; each next level is half the size and blurrier, level i standing for roughness i/(n-1).
/// A shader looks up level (roughness * (n-1)) for a reflection.
struct Chain {
    std::vector<Image> levels;
    int levelCount() const { return static_cast<int>(levels.size()); }
};

constexpr int kMaxBaseWidth = 4096;
constexpr int kMinBaseWidth = 16;
constexpr int kLastLevelWidth = 8;

inline int floorPowerOfTwo(int v) {
    int p = 1;
    while (p * 2 <= v) p *= 2;
    return p;
}

inline float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

/// One level of the reflection chain: every pixel is the sky blurred by a GGX lobe of `roughness`, read from the
/// matching size in `boxChain` (importance sampling that reads smaller copies for wider lobes, so a bright sun
/// doesn't leave sparkles).
inline Image ggxFilter(const std::vector<Image>& boxChain, int width, int height, float roughness, int samples) {
    Image out(width, height);
    const float a = roughness * roughness;
    const float a2 = a * a;
    const Image& base = boxChain[0];
    const float texelSolidAngle = 4.0f * kPi / (static_cast<float>(base.width) * static_cast<float>(base.height));
    const int lastLod = static_cast<int>(boxChain.size()) - 1;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const glm::vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                               (static_cast<float>(y) + 0.5f) / static_cast<float>(height));
            const glm::vec3 n = uvToDirection(uv);
            const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
            const glm::vec3 t = glm::normalize(glm::cross(up, n));
            const glm::vec3 b = glm::cross(n, t);

            glm::vec3 sum(0.0f);
            float weight = 0.0f;
            for (int s = 0; s < samples; s++) {
                const float xi1 = (static_cast<float>(s) + 0.5f) / static_cast<float>(samples);
                const float xi2 = radicalInverse(static_cast<uint32_t>(s));
                const float phi = 2.0f * kPi * xi2;
                const float cosTheta = std::sqrt((1.0f - xi1) / (1.0f + (a2 - 1.0f) * xi1));
                const float sinTheta = std::sqrt((std::max)(0.0f, 1.0f - cosTheta * cosTheta));
                const glm::vec3 h = t * (sinTheta * std::cos(phi)) + b * (sinTheta * std::sin(phi)) + n * cosTheta;
                const glm::vec3 l = h * (2.0f * glm::dot(n, h)) - n;
                const float nDotL = glm::dot(n, l);
                if (nDotL <= 0.0f) continue;

                // With the view along the normal, the density of the sampled directions is D / 4.
                const float denom = cosTheta * cosTheta * (a2 - 1.0f) + 1.0f;
                const float d = a2 / (kPi * denom * denom);
                const float pdf = d * 0.25f;
                const float sampleSolidAngle = 1.0f / (static_cast<float>(samples) * pdf + 1e-4f);
                const float lod = std::clamp(0.5f * std::log2(sampleSolidAngle / texelSolidAngle) + 1.0f, 0.0f, static_cast<float>(lastLod));
                sum += sample(boxChain[static_cast<size_t>(lod + 0.5f)], l) * nDotL;
                weight += nDotL;
            }
            out.set(x, y, weight > 0.0f ? sum / weight : sample(base, n));
        }
    }
    return out;
}

/// What the renderer needs from a sky: the chain to reflect, and a small copy to light diffuse surfaces from.
struct Prepared {
    Chain chain;
    Image diffuseSource;  // about 64 pixels wide; project it with projectDiffuseSh
};

inline Prepared prepare(const Image& source, int maxBaseWidth = kMaxBaseWidth) {
    Prepared out;
    int width = floorPowerOfTwo((std::max)(1, (std::min)(source.width, maxBaseWidth)));
    width = (std::max)(width, kMinBaseWidth);
    const int height = width / 2;

    std::vector<Image> box;
    box.push_back(resampleTo(source, width, height));
    while (box.back().width > kLastLevelWidth) box.push_back(downsample2x(box.back()));
    const int count = static_cast<int>(box.size());

    out.chain.levels.reserve(box.size());
    out.chain.levels.push_back(box[0]);
    for (int i = 1; i < count; i++) {
        const float roughness = static_cast<float>(i) / static_cast<float>(count - 1);
        // The wide copies are only a little blurred; averaging pixels is close enough. From 512 wide down, filter properly.
        if (box[i].width > 512) {
            out.chain.levels.push_back(box[i]);
        } else {
            const int samples = box[i].width >= 256 ? 32 : 64;
            out.chain.levels.push_back(ggxFilter(box, box[i].width, box[i].height, roughness, samples));
        }
    }

    out.diffuseSource = box.back();
    for (const Image& level : box) {
        if (level.width <= 64) {
            out.diffuseSource = level;
            break;
        }
    }
    return out;
}

// ---- Half floats (what the GPU image is stored as) ---------------------------------------------------------------

inline uint16_t floatToHalf(float value) {
    uint32_t x;
    std::memcpy(&x, &value, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFFu) == 0xFFu) return static_cast<uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));  // infinity, NaN
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u);                                          // too big
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<uint16_t>(sign);  // too small
        mantissa |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        uint32_t half = mantissa >> shift;
        if ((mantissa >> (shift - 1)) & 1u) half++;
        return static_cast<uint16_t>(sign | half);
    }
    uint32_t half = sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
    if (mantissa & 0x1000u) half++;  // round to nearest; a carry correctly bumps the exponent
    return static_cast<uint16_t>(half);
}

inline float halfToFloat(uint16_t h) {
    const uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;
    uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {  // a subnormal half is a normal float
            exponent = 127 - 15 + 1;
            while (!(mantissa & 0x400u)) {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x3FFu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

/// RGBA half floats for the GPU. Values are kept inside what a half float can hold (a noon sun in an HDRI can be
/// far above it), and a bad pixel (NaN, negative) becomes black.
inline std::vector<uint16_t> toRgba16f(const Image& img) {
    std::vector<uint16_t> out(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4);
    auto convert = [](float v) { return floatToHalf(v >= 0.0f ? (std::min)(v, 65000.0f) : 0.0f); };
    const uint16_t one = floatToHalf(1.0f);
    for (size_t i = 0, pixels = static_cast<size_t>(img.width) * static_cast<size_t>(img.height); i < pixels; i++) {
        out[i * 4 + 0] = convert(img.rgb[i * 3 + 0]);
        out[i * 4 + 1] = convert(img.rgb[i * 3 + 1]);
        out[i * 4 + 2] = convert(img.rgb[i * 3 + 2]);
        out[i * 4 + 3] = one;
    }
    return out;
}

} // namespace Environment
} // namespace Propulsion
