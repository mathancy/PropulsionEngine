// PropulsionMKI Utility Functions Implementation

#include "utils/Utils.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <random>
#include <sstream>
#include <iomanip>

namespace PropulsionMKI {
namespace Utils {

std::string generateGUID() {
    // Simple GUID generator using random numbers
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4"; // Version 4
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen); // Variant
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    
    return ss.str();
}

std::string stableGUID(const std::string& key) {
    // Two independent 64-bit FNV-1a hashes of the key (different offset bases) fill the 128 bits of a UUID.
    auto fnv = [&](uint64_t hash) {
        for (unsigned char ch : key) {
            hash ^= ch;
            hash *= 1099511628211ull;
        }
        return hash;
    };
    uint64_t hi = fnv(14695981039346656037ull);
    uint64_t lo = fnv(0x9E3779B97F4A7C15ull);
    hi = (hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000005000ull;  // version 5 (name-based)
    lo = (lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;  // variant 10xx

    char text[40];
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%04x-%012llx",
                  static_cast<unsigned>(hi >> 32), static_cast<unsigned>((hi >> 16) & 0xFFFF), static_cast<unsigned>(hi & 0xFFFF),
                  static_cast<unsigned>(lo >> 48), static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFull));
    return text;
}

std::string meshAssetGUID(const std::string& relativePath, const std::string& name) {
    std::string key = relativePath.empty() ? "Assets/Meshes/" + name + ".fbx" : relativePath;
    for (char& ch : key) {
        if (ch == '\\') ch = '/';
        else ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (key.compare(0, 2, "./") == 0) key.erase(0, 2);
    return stableGUID("mesh:" + key);
}

} // namespace Utils
} // namespace PropulsionMKI
