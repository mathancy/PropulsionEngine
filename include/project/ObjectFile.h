#pragma once

/**
 * ObjectFile.h
 * The .fobj (Object asset) file format, in one place.
 *
 * An Object asset is the engine's equivalent of a Blueprint class: which mesh and material it uses,
 * and which ForgeScript (.forge) script gives it behavior. A blank script means the object just
 * exists and is rendered. Scene instances placed from the asset take their behavior from it.
 *
 *   v1: "OBJ" guid(len+bytes) meshIndex(int) materialIndex(int)           (legacy)
 *   v2: "OBJ" 2 guid meshGUID materialGUID
 *   v3: "OBJ" 3 guid meshGUID materialGUID scriptFile
 *
 * Strings are a size_t length followed by the bytes. Older versions still load (no script).
 */

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

namespace Forge {
namespace ObjectFile {

constexpr uint8_t kVersion = 3;

struct Data {
    std::string id;
    std::string meshGUID;
    std::string materialGUID;
    std::string scriptFile;  // path as listed by the script registry, e.g. "rotator.forge"; empty for none
    int legacyMeshIndex = -1;      // v1 only
    int legacyMaterialIndex = -1;  // v1 only
    uint8_t version = kVersion;
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

inline void write(std::ostream& out, const Data& data) {
    out.write("OBJ", 3);
    out.write(reinterpret_cast<const char*>(&kVersion), sizeof(uint8_t));
    writeString(out, data.id);
    writeString(out, data.meshGUID);
    writeString(out, data.materialGUID);
    writeString(out, data.scriptFile);
}

/// Read any version. Returns false if this isn't an object file or it is cut short.
inline bool read(std::istream& in, Data& data) {
    char header[3];
    in.read(header, 3);
    if (!in || std::string(header, 3) != "OBJ") return false;

    char first = 0;
    in.read(&first, 1);
    if (!in) return false;

    if (first == 2 || first == 3) {
        data.version = static_cast<uint8_t>(first);
        if (!readString(in, data.id) || !readString(in, data.meshGUID) || !readString(in, data.materialGUID)) return false;
        if (data.version >= 3 && !readString(in, data.scriptFile)) return false;
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
} // namespace Forge
