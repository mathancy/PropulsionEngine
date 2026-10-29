// Tests for the project file (.pproj): which asset each asset-editor tab shows must survive a save and reopen, files
// from before that was recorded must still open, and a file from a newer engine must be refused.
//
//   cmake --build . --target ProjectFileTest --config Debug
//   build/tests/ProjectFileTest.exe <path to tests/fixtures>

#include "project/ProjectSerializer.h"
#include "ui/ContentItem.h"
#include "ui/TabSystem.h"
#include "Engine.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace Propulsion;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static fs::path scratchDir() {
    fs::path dir = fs::temp_directory_path() / ("propulsion_project_test_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

static void testTabsRememberTheirAssets() {
    ProjectData in;
    in.tabs.push_back(Tab("Untitled Scene", TabType::Scene));

    Tab object("Thing", TabType::AssetEditor, 4);
    object.assetId = "object-guid-1";
    in.tabs.push_back(object);

    // A Material with the same name as the Object: both tabs are named "Thing", and must stay two different tabs.
    Tab material("Thing", TabType::AssetEditor, 7);
    material.assetId = "material-guid-2";
    in.tabs.push_back(material);
    in.activeTabIndex = 2;

    const fs::path dir = scratchDir();
    const std::string file = (dir / "Proj" / "Proj.pproj").string();
    CHECK(ProjectSerializer::saveProject(file, in));

    ProjectData out;
    CHECK(ProjectSerializer::loadProject(file, out));
    CHECK(out.fileVersion == 7);
    CHECK(out.tabs.size() == 3);
    if (out.tabs.size() == 3) {
        CHECK(out.tabs[0].type == TabType::Scene && out.tabs[0].assetId.empty());
        CHECK(out.tabs[1].name == "Thing" && out.tabs[1].assetId == "object-guid-1");
        CHECK(out.tabs[2].name == "Thing" && out.tabs[2].assetId == "material-guid-2");
        CHECK(out.tabs[1].assetId != out.tabs[2].assetId);
    }
    CHECK(out.activeTabIndex == 2);
    fs::remove_all(dir);
}

static void testAnOlderProjectStillOpens(const fs::path& fixtures) {
    // project_v3.pproj was written by the engine before tabs remembered their asset.
    ProjectData data;
    CHECK(ProjectSerializer::loadProject((fixtures / "project_v3.pproj").string(), data));
    CHECK(data.fileVersion == 3);
    CHECK(data.tabs.size() == 2);
    if (data.tabs.size() == 2) {
        CHECK(data.tabs[0].type == TabType::Scene);
        CHECK(data.tabs[1].type == TabType::AssetEditor && data.tabs[1].name == "NewObject");
        CHECK(data.tabs[1].assetId.empty());  // not recorded then; the engine finds the asset by name on load
    }
    CHECK(data.sceneObjects.size() == 1 && data.sceneObjects[0].name == "NewObject_01");
}

static void testTheEnvironmentIsSaved() {
    ProjectData in;
    in.tabs.push_back(Tab("Untitled Scene", TabType::Scene));
    in.environment.hdriPath = "Assets/HDRI/sunset_4k.hdr";
    in.environment.intensity = 2.5f;
    in.environment.rotationDegrees = 135.0f;
    in.environment.showBackground = false;
    in.environment.shadows = false;
    in.environment.shadowDistance = 42.5f;
    in.environment.ambientOcclusion = false;
    in.environment.aoRadius = 1.25f;
    in.environment.aoStrength = 0.6f;
    in.environment.renderMode = RenderMode::PathTracer;
    in.environment.rtShadowRays = 40;
    in.environment.rtAoRays = 3;

    const fs::path dir = scratchDir();
    const std::string file = (dir / "Proj" / "Proj.pproj").string();
    CHECK(ProjectSerializer::saveProject(file, in));

    ProjectData out;
    CHECK(ProjectSerializer::loadProject(file, out));
    CHECK(out.environment == in.environment);
    CHECK(out.environment.hdriPath == "Assets/HDRI/sunset_4k.hdr" && !out.environment.showBackground);
    CHECK(out.environment.renderMode == RenderMode::PathTracer && out.environment.rtShadowRays == 40 && out.environment.rtAoRays == 3);
    // The rest of the file is where it was.
    CHECK(out.tabs.size() == 1 && out.showObjectsPanel == in.showObjectsPanel);

    // A project with the default sky saves and loads as the default sky.
    ProjectData plain;
    plain.tabs.push_back(Tab("Untitled Scene", TabType::Scene));
    CHECK(ProjectSerializer::saveProject(file, plain));
    ProjectData plainOut;
    plainOut.environment.intensity = 9.0f;  // whatever was there must be replaced
    CHECK(ProjectSerializer::loadProject(file, plainOut));
    CHECK(plainOut.environment == EnvironmentSettings());

    // Older files are newer ones without something on the end. Version 7 added the render mode and its ray counts after the
    // shadow and occlusion settings of version 6, which came after the sky's (version 5); before that there was none of them.
    // Each opens with what it didn't have at its default.
    const size_t rayTracingBytes = sizeof(int) * 3;
    const size_t renderSettingsBytes = sizeof(bool) + sizeof(float) + sizeof(bool) + sizeof(float) * 2;
    const size_t skyBytes = sizeof(size_t) + plain.environment.hdriPath.size() + sizeof(float) * 2 + sizeof(bool);
    auto downgrade = [&](ProjectData source, size_t dropBytes, int version) {
        CHECK(ProjectSerializer::saveProject(file, source));
        std::ifstream f(file, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        bytes.resize(bytes.size() - dropBytes);
        std::memcpy(&bytes[5], &version, sizeof(int));
        std::ofstream o(file, std::ios::binary | std::ios::trunc);
        o.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };

    ProjectData six = in;
    downgrade(six, rayTracingBytes, 6);
    ProjectData sixOut;
    sixOut.environment.renderMode = RenderMode::HybridRT;  // whatever was there must be replaced
    sixOut.environment.rtShadowRays = 7;
    CHECK(ProjectSerializer::loadProject(file, sixOut));
    CHECK(sixOut.fileVersion == 6);
    CHECK(sixOut.environment.shadowDistance == 42.5f && sixOut.environment.aoStrength == 0.6f);  // what version 6 had is kept
    CHECK(sixOut.environment.renderMode == RenderMode::Raster && sixOut.environment.rtShadowRays == EnvironmentSettings().rtShadowRays);

    ProjectData five = in;
    downgrade(five, rayTracingBytes + renderSettingsBytes, 5);
    ProjectData fiveOut;
    fiveOut.environment.shadowDistance = 9.0f;
    CHECK(ProjectSerializer::loadProject(file, fiveOut));
    CHECK(fiveOut.fileVersion == 5);
    CHECK(fiveOut.environment.hdriPath == in.environment.hdriPath && fiveOut.environment.intensity == 2.5f && !fiveOut.environment.showBackground);
    CHECK(fiveOut.environment.shadows && fiveOut.environment.shadowDistance == EnvironmentSettings().shadowDistance);  // not in the file: defaults
    CHECK(fiveOut.environment.ambientOcclusion && fiveOut.environment.aoRadius == EnvironmentSettings().aoRadius);

    downgrade(plain, skyBytes + renderSettingsBytes + rayTracingBytes, 4);
    ProjectData old;
    old.environment.intensity = 9.0f;
    CHECK(ProjectSerializer::loadProject(file, old));
    CHECK(old.fileVersion == 4);
    CHECK(old.environment == EnvironmentSettings());
    CHECK(old.tabs.size() == 1);

    // Damaged settings are pulled back into range: a negative or NaN intensity must not black out the scene.
    ProjectData bad;
    bad.tabs.push_back(Tab("Untitled Scene", TabType::Scene));
    bad.environment.intensity = -5.0f;
    bad.environment.rotationDegrees = 720.0f;
    CHECK(ProjectSerializer::saveProject(file, bad));
    ProjectData badOut;
    CHECK(ProjectSerializer::loadProject(file, badOut));
    CHECK(badOut.environment.intensity == 0.0f && badOut.environment.rotationDegrees == 720.0f);

    // The shadow and occlusion settings are pulled back into range too.
    bad.environment = EnvironmentSettings();
    bad.environment.shadowDistance = -3.0f;
    bad.environment.aoRadius = 1.0e9f;
    bad.environment.aoStrength = 7.0f;
    bad.environment.renderMode = static_cast<RenderMode>(42);
    bad.environment.rtShadowRays = 100000;
    bad.environment.rtAoRays = -4;
    CHECK(ProjectSerializer::saveProject(file, bad));
    CHECK(ProjectSerializer::loadProject(file, badOut));
    CHECK(badOut.environment.shadowDistance == EnvironmentSettings::kMinShadowDistance);
    CHECK(badOut.environment.aoRadius == EnvironmentSettings::kMaxAoRadius && badOut.environment.aoStrength == 1.0f);
    CHECK(badOut.environment.renderMode == RenderMode::Raster);
    CHECK(badOut.environment.rtShadowRays == EnvironmentSettings::kMaxRtRays && badOut.environment.rtAoRays == EnvironmentSettings::kMinRtRays);
    fs::remove_all(dir);
}

static void testANewerFileIsRefused() {
    ProjectData in;
    in.tabs.push_back(Tab("Untitled Scene", TabType::Scene));
    const fs::path dir = scratchDir();
    const std::string file = (dir / "Proj" / "Proj.pproj").string();
    CHECK(ProjectSerializer::saveProject(file, in));

    // Change the version number (the 4 bytes after "PROPU") to one this engine doesn't know.
    {
        std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(5);
        const int future = 99;
        f.write(reinterpret_cast<const char*>(&future), sizeof(int));
    }
    ProjectData out;
    std::streambuf* old = std::cerr.rdbuf(nullptr);  // the loader complains on cerr; keep the test output clean
    const bool loaded = ProjectSerializer::loadProject(file, out);
    std::cerr.rdbuf(old);
    CHECK(!loaded);
    fs::remove_all(dir);
}

int main(int argc, char** argv) {
    const fs::path fixtures = argc > 1 ? fs::path(argv[1]) : fs::path("tests") / "fixtures";
    testTabsRememberTheirAssets();
    testAnOlderProjectStillOpens(fixtures);
    testTheEnvironmentIsSaved();
    testANewerFileIsRefused();
    if (failures == 0) std::printf("ProjectFileTest: all passed\n");
    else std::printf("ProjectFileTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
