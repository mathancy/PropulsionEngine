// Tests for asset IDs: a Mesh asset's ID must be the same in every session (it is stored in Object files, and the
// mesh file itself has nowhere to keep one).
//
//   cmake --build . --target AssetIdTest --config Debug
//   build/tests/AssetIdTest.exe

#include "project/ObjectFile.h"
#include "ui/ContentItem.h"
#include "utils/Utils.h"

#include <cstdio>
#include <set>
#include <sstream>
#include <string>

using namespace PropulsionMKI::Utils;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static bool looksLikeAGuid(const std::string& g) {
    if (g.size() != 36) return false;
    for (size_t i = 0; i < g.size(); i++) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        const char ch = g[i];
        const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        if (dash ? ch != '-' : !hex) return false;
    }
    return true;
}

static void testStableGuid() {
    const std::string a = stableGUID("mesh:assets/meshes/cube.fbx");
    CHECK(looksLikeAGuid(a));
    CHECK(a[14] == '5');                                           // version 5: name-based
    CHECK(a[19] == '8' || a[19] == '9' || a[19] == 'a' || a[19] == 'b');  // the standard variant
    CHECK(a == stableGUID("mesh:assets/meshes/cube.fbx"));         // the same key, the same GUID, every time
    CHECK(a != stableGUID("mesh:assets/meshes/sphere.fbx"));
    CHECK(looksLikeAGuid(stableGUID("")));                          // even nothing gives a well-formed one

    // Many different keys don't collide.
    std::set<std::string> seen;
    for (int i = 0; i < 5000; i++) seen.insert(stableGUID("mesh:assets/meshes/thing" + std::to_string(i) + ".fbx"));
    CHECK(seen.size() == 5000);
}

static void testMeshIdsIgnoreHowThePathIsWritten() {
    const std::string cube = meshAssetGUID("Assets/Meshes/Cube.fbx", "Cube");
    CHECK(!cube.empty() && looksLikeAGuid(cube));
    CHECK(cube == meshAssetGUID("Assets\\Meshes\\Cube.fbx", "Cube"));    // the scan writes backslashes
    CHECK(cube == meshAssetGUID("assets/meshes/cube.fbx", "Cube"));      // case doesn't matter on Windows
    CHECK(cube == meshAssetGUID("./Assets/Meshes/Cube.fbx", "Cube"));
    CHECK(cube == meshAssetGUID("", "Cube"));                             // the built-in mesh, before any project is open
    CHECK(cube != meshAssetGUID("Assets/Meshes/Sphere.fbx", "Sphere"));
    CHECK(cube != meshAssetGUID("Assets/Meshes/Cube2.fbx", "Cube"));     // a different file is a different mesh
    CHECK(cube != meshAssetGUID("Assets/Extra/Cube.fbx", "Cube"));       // and so is a different folder
}

// These IDs are written into users' Object files, so they must never change. If this fails, the hash was
// changed and every saved mesh reference would come back as "None".
static void testTheIdsAreFrozen() {
    CHECK(meshAssetGUID("Assets/Meshes/Cube.fbx", "Cube") == "0161e207-50a3-5a6d-a37d-150bb689a27d");
}

static void testContentItemsGetTheirIds() {
    using Propulsion::ContentItem;
    // The three ways a mesh item comes to exist all agree: the engine's built-in, a new project's, and the scan's.
    ContentItem builtIn("Cube", "Mesh");
    ContentItem fromNewProject("Cube", "Mesh", "Meshes", "Assets/Meshes/Cube.fbx");
    ContentItem fromScan("Cube", "Mesh", "Meshes", "Assets\\Meshes\\Cube.fbx");
    CHECK(!builtIn.id.empty());
    CHECK(builtIn.id == fromNewProject.id && builtIn.id == fromScan.id);
    CHECK(builtIn.id != ContentItem("Sphere", "Mesh").id);

    // Other asset types are not given an ID by the constructor: their own files hold it.
    CHECK(ContentItem("Brick", "Material", "Materials", "Assets/Materials/Brick.pmat").id.empty());
    CHECK(ContentItem("Door", "Object", "Objects", "Assets/Objects/Door.pobj").id.empty());
}

// The bug this guards against: pick a mesh for an Object, save, reopen the project, and the Object's mesh is None.
// A reopened project rebuilds every mesh item from the files on disk, so the Object only finds its mesh again if the
// mesh gets the same ID both times.
static void testAnObjectsMeshSurvivesSavingAndReopening() {
    using Propulsion::ContentItem;
    using namespace Propulsion::ObjectFile;

    // Session 1: the project is open, the user picks the Cube mesh for an Object and saves it.
    std::vector<ContentItem> session1 = {ContentItem("Cube", "Mesh", "Meshes", "Assets/Meshes/Cube.fbx"),
                                         ContentItem("Sphere", "Mesh", "Meshes", "Assets/Meshes/Sphere.fbx")};
    ContentItem object("Door", "Object", "Objects", "Assets/Objects/Door.pobj");
    object.id = "door-guid";
    object.meshReferenceGUID = session1[1].id;  // the Sphere
    std::stringstream file(std::ios::in | std::ios::out | std::ios::binary);
    write(file, dataOf(object));

    // Session 2: the project is reopened. The scan rebuilds the mesh items, with Windows-style paths.
    std::vector<ContentItem> session2 = {ContentItem("Cube", "Mesh", "Meshes", "Assets\\Meshes\\Cube.fbx"),
                                         ContentItem("Cylinder", "Mesh", "Meshes", "Assets\\Meshes\\Cylinder.fbx"),
                                         ContentItem("Sphere", "Mesh", "Meshes", "Assets\\Meshes\\Sphere.fbx")};
    Data loaded;
    CHECK(read(file, loaded));
    CHECK(!loaded.meshGUID.empty());
    std::string found = "None";
    for (const ContentItem& mesh : session2) {
        if (mesh.type == "Mesh" && mesh.id == loaded.meshGUID) found = mesh.name;
    }
    CHECK(found == "Sphere");  // the same mesh, even though it is now the third item rather than the second
}

int main() {
    testStableGuid();
    testMeshIdsIgnoreHowThePathIsWritten();
    testTheIdsAreFrozen();
    testContentItemsGetTheirIds();
    testAnObjectsMeshSurvivesSavingAndReopening();
    if (failures == 0) std::printf("AssetIdTest: all passed\n");
    else std::printf("AssetIdTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
