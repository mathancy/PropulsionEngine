// Tests for the .pobj Object asset file format (include/project/ObjectFile.h): the camera component
// added in v4 survives a save and load, and files from older versions still open.
//
//   cmake --build . --target ObjectFileTest --config Debug
//   build/tests/ObjectFileTest.exe

#include "project/ObjectFile.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

using namespace Propulsion;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static ObjectFile::Data roundTrip(const ObjectFile::Data& in, bool* ok = nullptr) {
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    ObjectFile::write(stream, in);
    ObjectFile::Data out;
    bool read = ObjectFile::read(stream, out);
    if (ok) *ok = read;
    return out;
}

static void testCameraRoundTrip() {
    ObjectFile::Data in;
    in.id = "abc";
    in.meshGUID = "mesh-1";
    in.materialGUID = "mat-1";
    in.scriptFile = "Scripts/Player.prop";
    in.camera.enabled = true;
    in.camera.fov = 75.0f;
    in.camera.nearClip = 0.25f;
    in.camera.farClip = 500.0f;
    in.camera.autoActivate = false;
    in.camera.offset = glm::vec3(0.0f, 2.0f, 6.0f);
    in.camera.rotation = glm::vec3(-15.0f, 0.0f, 3.0f);
    in.camera.arm.enabled = true;
    in.camera.arm.targetArmLength = 3.5f;
    in.camera.arm.targetOffset = glm::vec3(0.0f, 0.0f, 1.5f);
    in.camera.arm.socketOffset = glm::vec3(0.75f, 0.0f, 0.25f);
    in.camera.arm.usePawnControlRotation = false;
    in.camera.arm.doCollisionTest = false;
    in.camera.arm.probeSize = 0.35f;
    in.camera.arm.enableCameraLag = true;
    in.camera.arm.cameraLagSpeed = 6.5f;
    in.camera.arm.cameraLagMaxDistance = 1.25f;
    in.camera.arm.enableCameraRotationLag = true;
    in.camera.arm.cameraRotationLagSpeed = 4.5f;

    bool ok = false;
    ObjectFile::Data out = roundTrip(in, &ok);
    CHECK(ok);
    CHECK(out.version == 11);
    CHECK(out.id == "abc" && out.meshGUID == "mesh-1" && out.materialGUID == "mat-1");
    CHECK(out.scriptFile == "Scripts/Player.prop");
    CHECK(out.camera == in.camera);
}

// A new arm tests for collision and has no lag (Unreal's defaults), and so does one from a file that came before those settings.
static void testSpringArmBehaviourDefaultsAndOlderFiles() {
    const SpringArmSettings fresh;
    CHECK(fresh.doCollisionTest && fresh.probeSize == 0.2f && !fresh.enableCameraLag && !fresh.enableCameraRotationLag);
    CHECK(fresh.cameraLagSpeed == 10.0f && fresh.cameraRotationLagSpeed == 10.0f && fresh.cameraLagMaxDistance == 0.0f);

    // A v9 file is the same as a v10 one without the arm's behaviour on the end.
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    ObjectFile::Data in;
    in.id = "v9-id";
    in.camera.enabled = true;
    in.camera.arm.enabled = true;
    in.camera.arm.targetArmLength = 4.0f;
    in.inputMapping = "map";
    s.write("OBJ", 3);
    const uint8_t version = 9;
    s.write(reinterpret_cast<const char*>(&version), 1);
    ObjectFile::writeString(s, in.id);
    ObjectFile::writeString(s, "m");
    ObjectFile::writeString(s, "mat");
    ObjectFile::writeString(s, "");
    ObjectFile::writeCamera(s, in.camera);
    ObjectFile::writeCollision(s, in.collision);
    ObjectFile::writeString(s, in.inputMapping);
    ObjectFile::Data d;
    CHECK(ObjectFile::read(s, d));
    CHECK(d.version == 9 && d.camera.arm.targetArmLength == 4.0f && d.inputMapping == "map");
    CHECK(d.camera.arm == in.camera.arm);  // every behaviour setting at its default

    // Damaged values are repaired.
    in.camera.arm.probeSize = std::numeric_limits<float>::quiet_NaN();
    in.camera.arm.cameraLagSpeed = -3.0f;
    in.camera.arm.cameraRotationLagSpeed = -1.0f;
    in.camera.arm.cameraLagMaxDistance = -2.0f;
    bool ok = false;
    const ObjectFile::Data out = roundTrip(in, &ok);
    CHECK(ok);
    CHECK(out.camera.arm.probeSize == 0.0f && out.camera.arm.cameraLagSpeed == 0.0f);
    CHECK(out.camera.arm.cameraRotationLagSpeed == 0.0f && out.camera.arm.cameraLagMaxDistance == 0.0f);
}

static void testCollisionRoundTrip() {
    ObjectFile::Data in;
    in.id = "hero";
    in.collision.solid = false;
    in.collision.character.enabled = true;
    in.collision.character.radius = 0.75f;
    in.collision.character.halfHeight = 1.25f;
    in.collision.character.gravity = 30.0f;
    in.collision.character.jumpSpeed = 11.0f;
    in.collision.character.maxSlopeDegrees = 50.0f;
    in.collision.character.jumpAction = "dash";
    in.inputMapping = "mapping-guid";
    bool ok = false;
    ObjectFile::Data out = roundTrip(in, &ok);
    CHECK(ok);
    CHECK(out.collision == in.collision);
    CHECK(!out.collision.solid && out.collision.character.enabled && out.collision.character.jumpAction == "dash");
    CHECK(out.inputMapping == "mapping-guid");

    // A plain Object is solid and not a character.
    ObjectFile::Data plain;
    plain.id = "wall";
    out = roundTrip(plain, &ok);
    CHECK(ok && out.collision.solid && !out.collision.character.enabled && out.collision.character.jumpAction == "jump");
    CHECK(out.inputMapping.empty());  // no mapping: no input
}

// A v7 file ends after the camera: it loads as a solid Object with no character movement.
static void testVersion7FilesStillLoad() {
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    ObjectFile::Data in;
    in.id = "v7-id";
    in.scriptFile = "Scripts/Player.prop";
    in.camera.enabled = true;
    in.camera.arm.enabled = true;
    in.camera.arm.targetArmLength = 6.0f;
    s.write("OBJ", 3);
    const uint8_t version = 7;
    s.write(reinterpret_cast<const char*>(&version), 1);
    ObjectFile::writeString(s, in.id);
    ObjectFile::writeString(s, "m");
    ObjectFile::writeString(s, "mat");
    ObjectFile::writeString(s, in.scriptFile);
    ObjectFile::writeCamera(s, in.camera);
    ObjectFile::Data d;
    CHECK(ObjectFile::read(s, d));
    CHECK(d.version == 7 && d.id == "v7-id" && d.camera.arm.targetArmLength == 6.0f);
    CHECK(d.collision.solid && !d.collision.character.enabled);
    CHECK(d.inputMapping.empty());
}

// A v8 file has the collision block with a yes/no for the jump, and nothing after it.
static std::stringstream version8File(bool jumpFromInput) {
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    s.write("OBJ", 3);
    const uint8_t version = 8;
    s.write(reinterpret_cast<const char*>(&version), 1);
    ObjectFile::writeString(s, "v8-id");
    ObjectFile::writeString(s, "m");
    ObjectFile::writeString(s, "mat");
    ObjectFile::writeString(s, "Scripts/Player.prop");
    ObjectFile::writeCamera(s, CameraSettings());
    ObjectFile::writeValue<uint8_t>(s, 1);  // solid
    ObjectFile::writeValue<uint8_t>(s, 1);  // character movement on
    for (float v : {0.5f, 1.0f, 20.0f, 8.0f, 45.0f}) ObjectFile::writeValue(s, v);
    ObjectFile::writeValue<uint8_t>(s, jumpFromInput ? 1 : 0);
    return s;
}

static void testVersion8FilesStillLoad() {
    {
        std::stringstream s = version8File(true);
        ObjectFile::Data d;
        CHECK(ObjectFile::read(s, d));
        CHECK(d.version == 8 && d.id == "v8-id" && d.collision.character.enabled && d.collision.character.gravity == 20.0f);
        CHECK(d.collision.character.jumpAction == "jump");  // the jump input was the input called jump
        CHECK(d.inputMapping.empty());
    }
    {
        std::stringstream s = version8File(false);
        ObjectFile::Data d;
        CHECK(ObjectFile::read(s, d));
        CHECK(d.collision.character.jumpAction.empty());   // and "off" is no jump action
    }
}

static void testDefaultsAreNoCamera() {
    ObjectFile::Data in;
    in.id = "plain";
    bool ok = false;
    ObjectFile::Data out = roundTrip(in, &ok);
    CHECK(ok);
    CHECK(!out.camera.enabled);
    CHECK(out.camera.fov == 90.0f);  // Unreal's default
}

// Build a file the way versions 2 and 3 wrote it, to be sure they still load.
static std::stringstream oldFile(uint8_t version, bool withScript) {
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    stream.write("OBJ", 3);
    stream.write(reinterpret_cast<const char*>(&version), 1);
    ObjectFile::writeString(stream, "old-id");
    ObjectFile::writeString(stream, "old-mesh");
    ObjectFile::writeString(stream, "old-mat");
    if (withScript) ObjectFile::writeString(stream, "Scripts/Door.prop");
    return stream;
}

static void testOlderVersionsLoad() {
    {
        std::stringstream s = oldFile(3, true);
        ObjectFile::Data d;
        CHECK(ObjectFile::read(s, d));
        CHECK(d.version == 3 && d.id == "old-id" && d.scriptFile == "Scripts/Door.prop");
        CHECK(!d.camera.enabled);
    }
    {
        std::stringstream s = oldFile(2, false);
        ObjectFile::Data d;
        CHECK(ObjectFile::read(s, d));
        CHECK(d.version == 2 && d.meshGUID == "old-mesh" && d.scriptFile.empty());
        CHECK(!d.camera.enabled);
    }
}

// Versions 5 and 6 had a "pitch tilts camera only" switch (and, in v6, a pivot) in place of the spring arm. They load as
// an arm that puts the camera in the same place and swings it the same way.
static std::stringstream camera56(uint8_t version, bool pitchOnly, glm::vec3 offset, const glm::vec3* pivot) {
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    s.write("OBJ", 3);
    s.write(reinterpret_cast<const char*>(&version), 1);
    ObjectFile::writeString(s, "old-id");
    ObjectFile::writeString(s, "old-mesh");
    ObjectFile::writeString(s, "old-mat");
    ObjectFile::writeString(s, "Scripts/Player.prop");
    ObjectFile::writeValue<uint8_t>(s, 1);                    // enabled
    ObjectFile::writeValue(s, 80.0f);                         // fov
    ObjectFile::writeValue(s, 0.2f);                          // near
    ObjectFile::writeValue(s, 400.0f);                        // far
    ObjectFile::writeValue<uint8_t>(s, 1);                    // auto activate
    for (float v : {offset.x, offset.y, offset.z}) ObjectFile::writeValue(s, v);
    for (float v : {0.0f, 0.0f, 0.0f}) ObjectFile::writeValue(s, v);   // rotation
    ObjectFile::writeValue<uint8_t>(s, pitchOnly ? 1 : 0);
    if (pivot) for (float v : {pivot->x, pivot->y, pivot->z}) ObjectFile::writeValue(s, v);
    return s;
}

static void testVersion5FilesBecomeASpringArm() {
    {   // third person, v5 (no pivot: it was the Object's origin): 5 behind and 2 up
        std::stringstream s = camera56(5, true, glm::vec3(0, -5, 2), nullptr);
        ObjectFile::Data d;
        CHECK(ObjectFile::read(s, d));
        CHECK(d.version == 5 && d.scriptFile == "Scripts/Player.prop" && d.camera.enabled && d.camera.fov == 80.0f);
        CHECK(d.camera.arm.enabled && d.camera.arm.usePawnControlRotation);
        CHECK(d.camera.arm.targetArmLength == 5.0f && d.camera.arm.targetOffset == glm::vec3(0.0f));
        CHECK(d.camera.arm.socketOffset == glm::vec3(0, 0, 2) && d.camera.offset == glm::vec3(0.0f));
        // Same place, same swing as the old camera: pivot at the origin, offset (0,-5,2), pitched 90 degrees.
        const glm::mat4 world = cameraWorldMatrix(glm::vec3(0), glm::quat(glm::vec3(0)), d.camera, 90.0f);
        CHECK(std::fabs(world[3].x) < 1e-4f && std::fabs(world[3].y + 2.0f) < 1e-4f && std::fabs(world[3].z + 5.0f) < 1e-4f);
    }
    {   // the switch was off: nothing to convert, the camera stays as it was
        std::stringstream s = camera56(5, false, glm::vec3(0, -5, 2), nullptr);
        ObjectFile::Data d;
        CHECK(ObjectFile::read(s, d));
        CHECK(!d.camera.arm.enabled && d.camera.offset == glm::vec3(0, -5, 2));
    }
}

static void testVersion6FilesBecomeASpringArm() {
    const glm::vec3 pivot(0, 0, 1.5f);
    std::stringstream s = camera56(6, true, glm::vec3(0, -4, 1.5f), &pivot);
    ObjectFile::Data d;
    CHECK(ObjectFile::read(s, d));
    CHECK(d.version == 6 && d.camera.arm.enabled);
    CHECK(d.camera.arm.targetOffset == pivot && d.camera.arm.targetArmLength == 4.0f);
    CHECK(d.camera.arm.socketOffset == glm::vec3(0.0f) && d.camera.offset == glm::vec3(0.0f));
    // The old camera, pitched 90 degrees about its pivot, was 4 below the pivot: z = 1.5 - 4.
    const glm::mat4 world = cameraWorldMatrix(glm::vec3(0), glm::quat(glm::vec3(0)), d.camera, 90.0f);
    CHECK(std::fabs(world[3].x) < 1e-4f && std::fabs(world[3].y) < 1e-4f && std::fabs(world[3].z + 2.5f) < 1e-4f);

    // A first-person v6 camera (pivot where the camera was) becomes an arm of no length.
    const glm::vec3 eye(0, 0, 1.7f);
    std::stringstream f = camera56(6, true, eye, &eye);
    ObjectFile::Data e;
    CHECK(ObjectFile::read(f, e));
    CHECK(e.camera.arm.enabled && e.camera.arm.targetArmLength == 0.0f && e.camera.arm.targetOffset == eye);
}

// A v4 file ends its camera block before the pitch setting existed: it loads, with the setting off.
static void testVersion4FilesStillLoad() {
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    s.write("OBJ", 3);
    const uint8_t version = 4;
    s.write(reinterpret_cast<const char*>(&version), 1);
    ObjectFile::writeString(s, "v4-id");
    ObjectFile::writeString(s, "v4-mesh");
    ObjectFile::writeString(s, "v4-mat");
    ObjectFile::writeString(s, "Scripts/Player.prop");
    ObjectFile::writeValue<uint8_t>(s, 1);                    // enabled
    ObjectFile::writeValue(s, 80.0f);                         // fov
    ObjectFile::writeValue(s, 0.2f);                          // near
    ObjectFile::writeValue(s, 400.0f);                        // far
    ObjectFile::writeValue<uint8_t>(s, 1);                    // auto activate
    for (float v : {0.0f, 0.0f, 1.7f}) ObjectFile::writeValue(s, v);   // offset
    for (float v : {0.0f, 0.0f, 0.0f}) ObjectFile::writeValue(s, v);   // rotation
    ObjectFile::Data d;
    CHECK(ObjectFile::read(s, d));
    CHECK(d.version == 4 && d.id == "v4-id" && d.scriptFile == "Scripts/Player.prop");
    CHECK(d.camera.enabled && d.camera.fov == 80.0f && d.camera.offset.z == 1.7f);
    CHECK(!d.camera.arm.enabled);
}

static void testDamagedFiles() {
    // A v4 file cut short in the middle of the camera block must be refused, not half-read.
    std::stringstream full(std::ios::in | std::ios::out | std::ios::binary);
    ObjectFile::Data in;
    in.id = "x";
    in.camera.enabled = true;
    ObjectFile::write(full, in);
    std::string bytes = full.str();
    std::stringstream cut(bytes.substr(0, bytes.size() - 5), std::ios::in | std::ios::binary);
    ObjectFile::Data d;
    CHECK(!ObjectFile::read(cut, d));

    std::stringstream notAnObject("NOPE", std::ios::in | std::ios::binary);
    CHECK(!ObjectFile::read(notAnObject, d));
}

static void testBadValuesAreRepaired() {
    ObjectFile::Data in;
    in.id = "bad";
    in.camera.enabled = true;
    in.camera.fov = std::numeric_limits<float>::quiet_NaN();
    in.camera.nearClip = -3.0f;
    in.camera.farClip = -1.0f;
    bool ok = false;
    ObjectFile::Data out = roundTrip(in, &ok);
    CHECK(ok);
    CHECK(std::isfinite(out.camera.fov) && out.camera.fov >= CameraSettings::kMinFov && out.camera.fov <= CameraSettings::kMaxFov);
    CHECK(out.camera.nearClip > 0.0f);
    CHECK(out.camera.farClip > out.camera.nearClip);

    in.camera = CameraSettings();
    in.camera.fov = 400.0f;
    out = roundTrip(in, &ok);
    CHECK(out.camera.fov == CameraSettings::kMaxFov);

    // A character that couldn't stand up is repaired: a size of at least something, and taller than it is wide.
    in = ObjectFile::Data();
    in.id = "bad";
    in.collision.character.enabled = true;
    in.collision.character.radius = std::numeric_limits<float>::quiet_NaN();
    in.collision.character.halfHeight = -2.0f;
    in.collision.character.gravity = -5.0f;
    in.collision.character.maxSlopeDegrees = 400.0f;
    out = roundTrip(in, &ok);
    CHECK(ok);
    const CharacterMovementSettings& c = out.collision.character;
    CHECK(c.radius >= CharacterMovementSettings::kMinRadius && std::isfinite(c.radius));
    CHECK(c.halfHeight >= c.radius);
    CHECK(c.gravity == 0.0f && c.maxSlopeDegrees <= 89.0f);
}

static void testLightRoundTrip() {
    ObjectFile::Data in;
    in.id = "sun";
    in.camera.enabled = true;  // a light sits alongside the other components without disturbing them
    in.camera.fov = 70.0f;
    in.collision.solid = false;
    in.inputMapping = "map";
    in.light = LightSettings::directional();
    in.light.color = glm::vec3(1.0f, 0.5f, 0.25f);
    in.light.intensity = 6.5f;
    in.light.range = 12.0f;
    in.light.radius = 0.4f;
    in.light.angularDiameterDeg = 1.5f;
    in.light.castShadows = false;
    bool ok = false;
    ObjectFile::Data out = roundTrip(in, &ok);
    CHECK(ok);
    CHECK(out.light == in.light);
    CHECK(out.light.type == LightSettings::Type::Directional && !out.light.castShadows);
    CHECK(out.camera.enabled && out.camera.fov == 70.0f && !out.collision.solid && out.inputMapping == "map");

    in.light = LightSettings::point();
    in.light.intensity = 40.0f;
    out = roundTrip(in, &ok);
    CHECK(ok && out.light == in.light && out.light.type == LightSettings::Type::Point);

    // No light component: it reads back off.
    in.light = LightSettings();
    out = roundTrip(in, &ok);
    CHECK(ok && !out.light.enabled);
}

// A v10 file ends after the spring arm's behaviour. It loads, with no light.
static void testVersion10FilesStillLoad() {
    ObjectFile::Data in;
    in.id = "v10-id";
    in.meshGUID = "m";
    in.camera.enabled = true;
    in.camera.arm.enabled = true;
    in.camera.arm.targetArmLength = 3.0f;
    in.inputMapping = "map";
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    s.write("OBJ", 3);
    const uint8_t version = 10;
    s.write(reinterpret_cast<const char*>(&version), 1);
    ObjectFile::writeString(s, in.id);
    ObjectFile::writeString(s, in.meshGUID);
    ObjectFile::writeString(s, "mat");
    ObjectFile::writeString(s, "");
    ObjectFile::writeCamera(s, in.camera);
    ObjectFile::writeCollision(s, in.collision);
    ObjectFile::writeString(s, in.inputMapping);
    ObjectFile::writeArmBehaviour(s, in.camera.arm);
    ObjectFile::Data d;
    CHECK(ObjectFile::read(s, d));
    CHECK(d.version == 10 && d.id == "v10-id" && d.inputMapping == "map" && d.camera.arm.targetArmLength == 3.0f);
    CHECK(!d.light.enabled);
}

static void testDamagedLightIsRefusedOrRepaired() {
    // A file cut short inside the light block is refused, not half-read.
    ObjectFile::Data in;
    in.id = "x";
    in.light = LightSettings::point();
    std::stringstream full(std::ios::in | std::ios::out | std::ios::binary);
    ObjectFile::write(full, in);
    const std::string bytes = full.str();
    std::stringstream cut(bytes.substr(0, bytes.size() - 3), std::ios::in | std::ios::binary);
    ObjectFile::Data d;
    CHECK(!ObjectFile::read(cut, d));

    // Values that would blow up the frame are pulled back into range.
    in.light.intensity = std::numeric_limits<float>::quiet_NaN();
    in.light.range = -4.0f;
    in.light.radius = std::numeric_limits<float>::infinity();
    in.light.color = glm::vec3(-1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN());
    bool ok = false;
    const ObjectFile::Data out = roundTrip(in, &ok);
    CHECK(ok);
    CHECK(out.light.intensity == 0.0f);
    CHECK(out.light.range >= LightSettings::kMinRange);
    CHECK(std::isfinite(out.light.radius) && out.light.radius <= LightSettings::kMaxRadius);
    CHECK(out.light.color.x == 0.0f && out.light.color.y == 2.0f && out.light.color.z == 0.0f);

    // An unknown light type reads as a point light.
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    ObjectFile::writeLight(s, LightSettings::point());
    std::string raw = s.str();
    raw[1] = 7;  // the type byte
    std::stringstream odd(raw, std::ios::in | std::ios::binary);
    LightSettings l;
    CHECK(ObjectFile::readLight(odd, l) && l.type == LightSettings::Type::Point);
}

static void testCapsuleScalesWithTheObject() {
    CharacterMovementSettings m;
    m.radius = 0.5f;
    m.halfHeight = 1.0f;
    Physics::CharacterParams p = m.paramsAt(glm::vec3(1.0f));
    CHECK(p.radius == 0.5f && p.halfHeight == 1.0f);
    p = m.paramsAt(glm::vec3(2.0f, 3.0f, 4.0f));  // width follows the larger of X and Y, height follows Z
    CHECK(p.radius == 1.5f && p.halfHeight == 4.0f);
    p = m.paramsAt(glm::vec3(1.0f, 1.0f, 0.1f));  // squashed flat: never thinner than it is wide
    CHECK(p.radius == 0.5f && p.halfHeight == p.radius);
    p = m.paramsAt(glm::vec3(-2.0f, 1.0f, -1.0f));  // a negative scale flips the mesh, not the size
    CHECK(p.radius == 1.0f && p.halfHeight == 1.0f);
}

int main() {
    testCameraRoundTrip();
    testCollisionRoundTrip();
    testSpringArmBehaviourDefaultsAndOlderFiles();
    testVersion7FilesStillLoad();
    testVersion8FilesStillLoad();
    testLightRoundTrip();
    testVersion10FilesStillLoad();
    testDamagedLightIsRefusedOrRepaired();
    testCapsuleScalesWithTheObject();
    testDefaultsAreNoCamera();
    testOlderVersionsLoad();
    testVersion4FilesStillLoad();
    testVersion5FilesBecomeASpringArm();
    testVersion6FilesBecomeASpringArm();
    testDamagedFiles();
    testBadValuesAreRepaired();
    if (failures == 0) std::printf("ObjectFileTest: all passed\n");
    else std::printf("ObjectFileTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
