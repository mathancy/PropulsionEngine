// Tests for material transparency settings (include/core/MaterialSettings.h) and how they are kept at the end of a material file
// (include/project/MaterialSettingsFile.h): the round trip, files from before the settings existed, and damaged values. No window or GPU.
//
//   cmake --build . --target MaterialSettingsTest --config Debug
//   build/tests/MaterialSettingsTest.exe

#include "project/MaterialSettingsFile.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>

using namespace Propulsion;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static void testDefaults() {
    const MaterialSettings s;
    CHECK(s.blendMode == BlendMode::Opaque && s.isOpaque());
    CHECK(s.opacity == 1.0f && s.maskCutoff == 0.5f && !s.twoSided && s.ior == 1.5f && !s.thinWalled);
    CHECK(MaterialSettings() == s);
}

static void testRoundTrip() {
    MaterialSettings in;
    in.blendMode = BlendMode::Translucent;
    in.opacity = 0.25f;
    in.maskCutoff = 0.8f;
    in.twoSided = true;
    in.ior = 1.33f;
    in.thinWalled = true;
    in.absorptionDistance = 12.5f;

    std::stringstream stream;
    MaterialSettingsFile::write(stream, in);
    MaterialSettings out;
    CHECK(MaterialSettingsFile::read(stream, out));
    CHECK(out == in);
    CHECK(!out.isOpaque());

    // Each blend mode on its own
    for (int i = 0; i < kBlendModeCount; i++) {
        MaterialSettings one;
        one.blendMode = blendModeFromInt(i);
        std::stringstream s;
        MaterialSettingsFile::write(s, one);
        MaterialSettings back;
        CHECK(MaterialSettingsFile::read(s, back) && back.blendMode == one.blendMode);
    }
}

static void testOlderFiles() {
    // A file from before the settings: nothing follows the splitter ratio.
    std::stringstream empty;
    MaterialSettings out;
    out.blendMode = BlendMode::Masked;  // whatever was there must be replaced
    CHECK(!MaterialSettingsFile::read(empty, out));
    CHECK(out == MaterialSettings());

    // Something else there (not our tag): not ours, defaults.
    std::stringstream other("some other trailing data");
    CHECK(!MaterialSettingsFile::read(other, out) && out == MaterialSettings());

    // Cut off in the middle: defaults, not half a material.
    std::stringstream whole;
    MaterialSettings in;
    in.blendMode = BlendMode::Masked;
    MaterialSettingsFile::write(whole, in);
    const std::string bytes = whole.str();
    for (size_t keep = 0; keep < bytes.size(); keep++) {
        std::stringstream cut(bytes.substr(0, keep));
        MaterialSettings partial;
        partial.opacity = 0.1f;
        CHECK(!MaterialSettingsFile::read(cut, partial));
        CHECK(partial == MaterialSettings());
    }
    CHECK(bytes.size() == 4 + 4 + 4 + 4 + 4 + 1 + 4 + 1 + 4);

    // A newer file with more fields after ours still reads (the extra bytes are left unread).
    std::stringstream newer(bytes + std::string("extra fields from a later version"));
    CHECK(MaterialSettingsFile::read(newer, out) && out.blendMode == BlendMode::Masked);
}

static void testDamagedValues() {
    MaterialSettings bad;
    bad.opacity = 7.0f;
    bad.maskCutoff = -2.0f;
    bad.ior = 0.2f;
    bad.absorptionDistance = std::numeric_limits<float>::quiet_NaN();
    bad.blendMode = static_cast<BlendMode>(42);
    std::stringstream stream;
    MaterialSettingsFile::write(stream, bad);
    MaterialSettings out;
    CHECK(MaterialSettingsFile::read(stream, out));
    CHECK(out.opacity == 1.0f && out.maskCutoff == 0.0f);
    CHECK(out.ior == MaterialSettings::kMinIor);
    CHECK(out.absorptionDistance == MaterialSettings::kMinDistance);
    CHECK(out.blendMode == BlendMode::Opaque);

    MaterialSettings huge;
    huge.ior = 1e9f;
    huge.absorptionDistance = 1e9f;
    huge.opacity = std::numeric_limits<float>::quiet_NaN();
    huge.sanitize();
    CHECK(huge.ior == MaterialSettings::kMaxIor && huge.absorptionDistance == MaterialSettings::kMaxDistance && huge.opacity == 0.0f);
}

static void testNames() {
    CHECK(std::string(blendModeName(BlendMode::Opaque)) == "Opaque");
    CHECK(std::string(blendModeName(BlendMode::Masked)) == "Masked");
    CHECK(std::string(blendModeName(BlendMode::Translucent)) == "Translucent");
    CHECK(blendModeFromInt(-1) == BlendMode::Opaque && blendModeFromInt(3) == BlendMode::Opaque);
}

int main() {
    testDefaults();
    testRoundTrip();
    testOlderFiles();
    testDamagedValues();
    testNames();

    if (failures == 0) std::printf("MaterialSettingsTest: all passed\n");
    else std::printf("MaterialSettingsTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
