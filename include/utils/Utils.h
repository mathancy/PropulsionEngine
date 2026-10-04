// ForgeEngine Utility Functions
// General-purpose utilities used throughout the engine

#pragma once

#include <string>

namespace ForgeEngine {
namespace Utils {

/**
 * @brief Generates a UUID/GUID string
 * 
 * Generates a version 4 UUID using random number generation.
 * Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 * where x is a random hex digit and y is one of 8, 9, a, or b.
 * 
 * @return A UUID string in standard format
 */
std::string generateGUID();

} // namespace Utils
} // namespace ForgeEngine
