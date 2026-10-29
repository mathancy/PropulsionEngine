#pragma once

/**
 * ObjectFile.h
 * The .pobj (Object asset) file format, in one place.
 *
 * An Object asset is the engine's equivalent of a Blueprint class: which mesh and material it uses,
 * which PropulsionScript (.prop) script gives it behavior, and which components it carries (a
 * Camera, a Light, and so on). A blank script means the object just exists and is rendered. Scene instances placed from
 * the asset take their behavior from it.
 *
 *   v1: "OBJ" guid(len+bytes) meshIndex(int) materialIndex(int)           (legacy)
 *   v2: "OBJ" 2 guid meshGUID materialGUID
 *   v3: "OBJ" 3 guid meshGUID materialGUID scriptFile
 *   v4: "OBJ" 4 guid meshGUID materialGUID scriptFile camera
 *       camera = enabled(u8) fov nearClip farClip (float) autoActivate(u8) offset(3 float) rotation(3 float)
 *   v5, v6: the camera also ends with "pitch tilts camera only"(u8) and, in v6, a "look pivot"(3 float). These were
 *       replaced by the spring arm; they are read, and turned into one (see readCamera)
 *   v7: the camera ends with its spring arm = enabled(u8) targetArmLength(float) targetOffset(3 float)
 *       socketOffset(3 float) usePawnControlRotation(u8)
 *   v8: after the camera, the collision settings = solid(u8) then the character movement component =
 *       enabled(u8) radius halfHeight gravity jumpSpeed maxSlopeDegrees (float) jumpFromInput(u8)
 *   v9: the jump becomes an Input Action name: jumpFromInput(u8) is replaced by jumpAction(string, "" for none; a v8
 *       file's "on" is "jump"). After the collision settings comes the Object's input mapping = mappingId(string,
 *       the Input Mapping asset's GUID, "" for none)
 *   v10: after that, how the spring arm behaves in Play = doCollisionTest(u8) probeSize(float) enableCameraLag(u8)
 *       cameraLagSpeed cameraLagMaxDistance (float) enableCameraRotationLag(u8) cameraRotationLagSpeed(float)
 *   v11: after that, the light component = enabled(u8) type(u8, 0 directional / 1 point) color(3 float) intensity range
 *       radius angularDiameterDeg (float) castShadows(u8)
 *
 * Strings are a size_t length followed by the bytes. Older versions still load (no script, no camera, solid, no
 * character movement).
 */

#include "core/CameraSettings.h"
#include "core/CollisionSettings.h"
#include "core/LightSettings.h"

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

namespace Propulsion {
namespace ObjectFile {

constexpr uint8_t kVersion = 11;

struct Data {
    std::string id;
    std::string meshGUID;
    std::string materialGUID;
    std::string scriptFile;  // path as listed by the script registry, e.g. "rotator.prop"; empty for none
    int legacyMeshIndex = -1;      // v1 only
    int legacyMaterialIndex = -1;  // v1 only
    uint8_t version = kVersion;
    CameraSettings camera;   // v4 and up; a disabled camera for older files
    CollisionSettings collision;  // v8 and up; solid, with no character movement, for older files
    std::string inputMapping;     // v9 and up: the GUID of the Input Mapping asset this Object is played with; empty for none
    LightSettings light;          // v11 and up; a disabled light (the Object doesn't shine) for older files
};

inline void writeString(std::ostream& out, const std::string& s) {
    size_t length = s.length();
    out.write(reinterpret_cast<const char*>(&length), sizeof(size_t));
    out.write(s.data(), static_cast<std::streamsize>(length));
}

inline bool readString(std::istream& in, std::string& s) {
    size_t length = 0;
    in.read(reinterpret_cast<char*>(&length), sizeof(size_t));
    if (!in || length > (1u << 20)) return false;  // a corrupt length shouldn't allocate gigabytes
    s.assign(length, '\0');
    if (length > 0) in.read(&s[0], static_cast<std::streamsize>(length));
    return static_cast<bool>(in);
}

template <typename T>
inline void writeValue(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
inline bool readValue(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

inline void writeCamera(std::ostream& out, const CameraSettings& c) {
    writeValue<uint8_t>(out, c.enabled ? 1 : 0);
    writeValue(out, c.fov);
    writeValue(out, c.nearClip);
    writeValue(out, c.farClip);
    writeValue<uint8_t>(out, c.autoActivate ? 1 : 0);
    writeValue(out, c.offset.x); writeValue(out, c.offset.y); writeValue(out, c.offset.z);
    writeValue(out, c.rotation.x); writeValue(out, c.rotation.y); writeValue(out, c.rotation.z);
    // v7: the spring arm
    writeValue<uint8_t>(out, c.arm.enabled ? 1 : 0);
    writeValue(out, c.arm.targetArmLength);
    writeValue(out, c.arm.targetOffset.x); writeValue(out, c.arm.targetOffset.y); writeValue(out, c.arm.targetOffset.z);
    writeValue(out, c.arm.socketOffset.x); writeValue(out, c.arm.socketOffset.y); writeValue(out, c.arm.socketOffset.z);
    writeValue<uint8_t>(out, c.arm.usePawnControlRotation ? 1 : 0);
}

inline bool readVec3(std::istream& in, glm::vec3& v) {
    return readValue(in, v.x) && readValue(in, v.y) && readValue(in, v.z);
}

inline bool readCamera(std::istream& in, CameraSettings& c, int fileVersion = kVersion) {
    uint8_t enabled = 0, autoActivate = 0;
    if (!readValue(in, enabled) || !readValue(in, c.fov) || !readValue(in, c.nearClip) || !readValue(in, c.farClip) ||
        !readValue(in, autoActivate) || !readVec3(in, c.offset) || !readVec3(in, c.rotation)) {
        return false;
    }
    c.enabled = enabled != 0;
    c.autoActivate = autoActivate != 0;

    if (fileVersion >= 7) {
        uint8_t armEnabled = 0, usePawn = 0;
        if (!readValue(in, armEnabled) || !readValue(in, c.arm.targetArmLength) || !readVec3(in, c.arm.targetOffset) ||
            !readVec3(in, c.arm.socketOffset) || !readValue(in, usePawn)) {
            return false;
        }
        c.arm.enabled = armEnabled != 0;
        c.arm.usePawnControlRotation = usePawn != 0;
    } else if (fileVersion >= 5) {
        // Versions 5 and 6 had a "pitch tilts camera only" switch and (v6) a pivot the camera swung around. The
        // spring arm does that now: an arm fixed at the pivot, as long as the camera was behind it, with the rest of
        // the camera's offset as the arm's socket offset. The camera sits in the same place and swings the same way.
        uint8_t pitchOnly = 0;
        glm::vec3 pivot(0.0f);
        if (!readValue(in, pitchOnly)) return false;
        if (fileVersion >= 6 && !readVec3(in, pivot)) return false;
        if (pitchOnly != 0) {
            const glm::vec3 fromPivot = c.offset - pivot;
            c.arm.enabled = true;
            c.arm.usePawnControlRotation = true;
            c.arm.targetOffset = pivot;
            c.arm.targetArmLength = fromPivot.y < 0.0f ? -fromPivot.y : 0.0f;
            c.arm.socketOffset = glm::vec3(fromPivot.x, 0.0f, fromPivot.z);
            c.offset = glm::vec3(0.0f);
        }
    }
    c.sanitize();  // a damaged file shouldn't produce an unusable projection
    return true;
}

inline void writeCollision(std::ostream& out, const CollisionSettings& c) {
    writeValue<uint8_t>(out, c.solid ? 1 : 0);
    writeValue<uint8_t>(out, c.character.enabled ? 1 : 0);
    writeValue(out, c.character.radius);
    writeValue(out, c.character.halfHeight);
    writeValue(out, c.character.gravity);
    writeValue(out, c.character.jumpSpeed);
    writeValue(out, c.character.maxSlopeDegrees);
    writeString(out, c.character.jumpAction);  // v9 (v8 had a yes/no here)
}

inline bool readCollision(std::istream& in, CollisionSettings& c, int fileVersion = kVersion) {
    uint8_t solid = 1, enabled = 0;
    CharacterMovementSettings& m = c.character;
    if (!readValue(in, solid) || !readValue(in, enabled) || !readValue(in, m.radius) || !readValue(in, m.halfHeight) ||
        !readValue(in, m.gravity) || !readValue(in, m.jumpSpeed) || !readValue(in, m.maxSlopeDegrees)) {
        return false;
    }
    if (fileVersion >= 9) {
        if (!readString(in, m.jumpAction)) return false;
    } else {
        uint8_t jumpFromInput = 1;  // v8: the jump input made it jump, and the input was called `jump`
        if (!readValue(in, jumpFromInput)) return false;
        m.jumpAction = jumpFromInput != 0 ? "jump" : "";
    }
    c.solid = solid != 0;
    m.enabled = enabled != 0;
    c.sanitize();  // a damaged file shouldn't produce a character that can't stand up
    return true;
}

/// v10: what the spring arm does in Play. (After everything else, so the older layouts are untouched; a file from before
/// it has an arm that tests for collision and has no lag, which is what a new arm does.)
inline void writeArmBehaviour(std::ostream& out, const SpringArmSettings& a) {
    writeValue<uint8_t>(out, a.doCollisionTest ? 1 : 0);
    writeValue(out, a.probeSize);
    writeValue<uint8_t>(out, a.enableCameraLag ? 1 : 0);
    writeValue(out, a.cameraLagSpeed);
    writeValue(out, a.cameraLagMaxDistance);
    writeValue<uint8_t>(out, a.enableCameraRotationLag ? 1 : 0);
    writeValue(out, a.cameraRotationLagSpeed);
}

inline bool readArmBehaviour(std::istream& in, SpringArmSettings& a) {
    uint8_t collision = 1, lag = 0, rotationLag = 0;
    if (!readValue(in, collision) || !readValue(in, a.probeSize) || !readValue(in, lag) || !readValue(in, a.cameraLagSpeed) ||
        !readValue(in, a.cameraLagMaxDistance) || !readValue(in, rotationLag) || !readValue(in, a.cameraRotationLagSpeed)) {
        return false;
    }
    a.doCollisionTest = collision != 0;
    a.enableCameraLag = lag != 0;
    a.enableCameraRotationLag = rotationLag != 0;
    a.sanitize();
    return true;
}

/// v11: the Light component. (After everything else, so the older layouts are untouched.)
inline void writeLight(std::ostream& out, const LightSettings& l) {
    writeValue<uint8_t>(out, l.enabled ? 1 : 0);
    writeValue<uint8_t>(out, static_cast<uint8_t>(l.type));
    writeValue(out, l.color.x); writeValue(out, l.color.y); writeValue(out, l.color.z);
    writeValue(out, l.intensity);
    writeValue(out, l.range);
    writeValue(out, l.radius);
    writeValue(out, l.angularDiameterDeg);
    writeValue<uint8_t>(out, l.castShadows ? 1 : 0);
}

inline bool readLight(std::istream& in, LightSettings& l) {
    uint8_t enabled = 0, type = 0, castShadows = 1;
    if (!readValue(in, enabled) || !readValue(in, type) || !readVec3(in, l.color) || !readValue(in, l.intensity) ||
        !readValue(in, l.range) || !readValue(in, l.radius) || !readValue(in, l.angularDiameterDeg) ||
        !readValue(in, castShadows)) {
        return false;
    }
    l.enabled = enabled != 0;
    l.type = type == static_cast<uint8_t>(LightSettings::Type::Directional) ? LightSettings::Type::Directional
                                                                            : LightSettings::Type::Point;
    l.castShadows = castShadows != 0;
    l.sanitize();  // a damaged file shouldn't produce a light that blows up the frame
    return true;
}

inline void write(std::ostream& out, const Data& data) {
    out.write("OBJ", 3);
    out.write(reinterpret_cast<const char*>(&kVersion), sizeof(uint8_t));
    writeString(out, data.id);
    writeString(out, data.meshGUID);
    writeString(out, data.materialGUID);
    writeString(out, data.scriptFile);
    writeCamera(out, data.camera);
    writeCollision(out, data.collision);
    writeString(out, data.inputMapping);
    writeArmBehaviour(out, data.camera.arm);
    writeLight(out, data.light);
}

/// The file contents for an Object asset (a ContentItem, or anything with the same fields).
template <typename Item>
inline Data dataOf(const Item& item) {
    Data data;
    data.id = item.id;
    data.meshGUID = item.meshReferenceGUID;
    data.materialGUID = item.materialReferenceGUID;
    data.scriptFile = item.scriptFile;
    data.camera = item.camera;
    data.collision = item.collision;
    data.inputMapping = item.inputMappingGUID;
    data.light = item.light;
    return data;
}

/// Read any version. Returns false if this isn't an object file or it is cut short.
inline bool read(std::istream& in, Data& data) {
    char header[3];
    in.read(header, 3);
    if (!in || std::string(header, 3) != "OBJ") return false;

    char first = 0;
    in.read(&first, 1);
    if (!in) return false;

    if (first >= 2 && first <= 11) {
        data.version = static_cast<uint8_t>(first);
        if (!readString(in, data.id) || !readString(in, data.meshGUID) || !readString(in, data.materialGUID)) return false;
        if (data.version >= 3 && !readString(in, data.scriptFile)) return false;
        if (data.version >= 4 && !readCamera(in, data.camera, data.version)) return false;
        if (data.version >= 8 && !readCollision(in, data.collision, data.version)) return false;
        if (data.version >= 9 && !readString(in, data.inputMapping)) return false;
        if (data.version >= 10 && !readArmBehaviour(in, data.camera.arm)) return false;
        if (data.version >= 11 && !readLight(in, data.light)) return false;
        return true;
    }

    // Version 1: the byte we just read was the start of the guid's length.
    data.version = 1;
    in.seekg(3);
    if (!readString(in, data.id)) return false;
    in.read(reinterpret_cast<char*>(&data.legacyMeshIndex), sizeof(int));
    in.read(reinterpret_cast<char*>(&data.legacyMaterialIndex), sizeof(int));
    return static_cast<bool>(in);
}

} // namespace ObjectFile
} // namespace Propulsion
