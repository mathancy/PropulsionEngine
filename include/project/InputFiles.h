#pragma once

/**
 * InputFiles.h
 * The Input Action (.pinputaction) and Input Mapping (.pinputmap) file formats, in one place. Each asset's *name* is
 * its file name; the file holds the rest.
 *
 *   .pinputaction  "IAC" version(u8 = 1) id(string) valueType(u8: 0 button, 1 axis) description(string)
 *   .pinputmap     "IMP" version(u8 = 1) id(string) rowCount(u32) then per row:
 *                  actionId(string) key(string) negate(u8) scale(float)
 *
 * Strings are a size_t length followed by the bytes (the same as the Object file).
 */

#include "input/InputTypes.h"
#include "project/ObjectFile.h"

#include <cstdint>
#include <istream>
#include <ostream>

namespace Propulsion {
namespace InputFiles {

constexpr uint8_t kActionVersion = 1;
constexpr uint8_t kMappingVersion = 1;
constexpr uint32_t kMaxRows = 100000;  // a corrupt count shouldn't allocate gigabytes

inline void writeAction(std::ostream& out, const InputActionDef& action) {
    out.write("IAC", 3);
    ObjectFile::writeValue<uint8_t>(out, kActionVersion);
    ObjectFile::writeString(out, action.id);
    ObjectFile::writeValue<uint8_t>(out, static_cast<uint8_t>(action.type));
    ObjectFile::writeString(out, action.description);
}

/// Fills in `id`, `type` and `description` (the caller knows the name: it is the file's). False if this isn't an Input
/// Action file, or it is cut short.
inline bool readAction(std::istream& in, InputActionDef& action) {
    char magic[3];
    in.read(magic, 3);
    uint8_t version = 0, type = 0;
    if (!in || std::string(magic, 3) != "IAC" || !ObjectFile::readValue(in, version) || version == 0 || version > kActionVersion) return false;
    if (!ObjectFile::readString(in, action.id) || !ObjectFile::readValue(in, type) || type > 1 ||
        !ObjectFile::readString(in, action.description)) {
        return false;
    }
    action.type = static_cast<InputValueType>(type);
    return true;
}

inline void writeMapping(std::ostream& out, const InputMappingDef& mapping) {
    out.write("IMP", 3);
    ObjectFile::writeValue<uint8_t>(out, kMappingVersion);
    ObjectFile::writeString(out, mapping.id);
    ObjectFile::writeValue<uint32_t>(out, static_cast<uint32_t>(mapping.rows.size()));
    for (const InputRow& row : mapping.rows) {
        ObjectFile::writeString(out, row.actionId);
        ObjectFile::writeString(out, row.key);
        ObjectFile::writeValue<uint8_t>(out, row.negate ? 1 : 0);
        ObjectFile::writeValue(out, row.scale);
    }
}

/// Fills in `id` and `rows` (the caller knows the name). False if this isn't an Input Mapping file, or it is cut short.
inline bool readMapping(std::istream& in, InputMappingDef& mapping) {
    char magic[3];
    in.read(magic, 3);
    uint8_t version = 0;
    uint32_t count = 0;
    if (!in || std::string(magic, 3) != "IMP" || !ObjectFile::readValue(in, version) || version == 0 || version > kMappingVersion) return false;
    if (!ObjectFile::readString(in, mapping.id) || !ObjectFile::readValue(in, count) || count > kMaxRows) return false;
    mapping.rows.clear();
    for (uint32_t i = 0; i < count; ++i) {
        InputRow row;
        uint8_t negate = 0;
        if (!ObjectFile::readString(in, row.actionId) || !ObjectFile::readString(in, row.key) || !ObjectFile::readValue(in, negate) ||
            !ObjectFile::readValue(in, row.scale)) {
            return false;
        }
        if (!std::isfinite(row.scale)) row.scale = 1.0f;  // a damaged file shouldn't make the player walk at NaN
        row.negate = negate != 0;
        mapping.rows.push_back(row);
    }
    return true;
}

} // namespace InputFiles
} // namespace Propulsion
