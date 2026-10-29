#pragma once

/**
 * MaterialSettingsFile.h
 * Reads and writes MaterialSettings (core/MaterialSettings.h) at the very end of a .pmat file, after the splitter ratio. It is a
 * tagged block: a material file without it (every file made before it existed) reads as the defaults, and an engine that doesn't know
 * about it never reads that far. Header only, so a test can use it without the engine.
 *
 *   "MSET"  int version (1)  int blend mode  float opacity  float mask cutoff  u8 two sided  float ior  u8 thin walled
 *   float absorption distance
 */

#include "core/MaterialSettings.h"

#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>

namespace Propulsion {
namespace MaterialSettingsFile {

constexpr int kVersion = 1;

inline void write(std::ostream& out, const MaterialSettings& s) {
    auto put = [&](const auto& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    out.write("MSET", 4);
    put(kVersion);
    const int blend = static_cast<int>(s.blendMode);
    put(blend);
    put(s.opacity);
    put(s.maskCutoff);
    const uint8_t twoSided = s.twoSided ? 1 : 0;
    put(twoSided);
    put(s.ior);
    const uint8_t thin = s.thinWalled ? 1 : 0;
    put(thin);
    put(s.absorptionDistance);
}

/// Reads the block if there is one at the stream's position. Returns whether there was; otherwise `out` is the defaults. Values
/// are pulled back into range, so a damaged file can't make a material invisible or its index of refraction nonsense.
inline bool read(std::istream& in, MaterialSettings& out) {
    out = MaterialSettings();
    char tag[4] = {};
    if (!in.read(tag, 4) || std::memcmp(tag, "MSET", 4) != 0) return false;

    auto get = [&](auto& value) { return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value))); };
    int version = 0;
    if (!get(version) || version < 1) return false;

    MaterialSettings s;
    int blend = 0;
    uint8_t twoSided = 0, thin = 0;
    // Fields are only ever added on the end, so a newer file's extra bytes are simply not read.
    if (!get(blend) || !get(s.opacity) || !get(s.maskCutoff) || !get(twoSided) || !get(s.ior) || !get(thin) || !get(s.absorptionDistance)) {
        return false;
    }
    s.blendMode = blendModeFromInt(blend);
    s.twoSided = twoSided != 0;
    s.thinWalled = thin != 0;
    s.sanitize();
    out = s;
    return true;
}

} // namespace MaterialSettingsFile
} // namespace Propulsion
