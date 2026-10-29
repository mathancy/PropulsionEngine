// PropulsionMKI Utility Functions
// General-purpose utilities used throughout the engine

#pragma once

#include <string>

namespace PropulsionMKI {
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

/**
 * @brief A GUID worked out from `key`, so the same key always gives the same GUID (in every session and on every machine).
 *
 * For assets that have no file of their own to keep an ID in, such as meshes (an .fbx has nowhere to put one). It is not a
 * secure hash: two different keys could in principle give the same GUID, but not by accident. The result has the
 * standard UUID shape with the version nibble set to 5 (name-based).
 */
std::string stableGUID(const std::string& key);

/**
 * @brief The ID of a Mesh asset: a stable GUID of where it lives in the project, so it survives saving and reopening.
 *
 * `relativePath` is the asset's path inside the project (`Assets/Meshes/Cube.fbx`, either slash, any case). If it is empty
 * the mesh is taken to be in the default place for its name. Renaming or moving the file changes the ID.
 */
std::string meshAssetGUID(const std::string& relativePath, const std::string& name);

} // namespace Utils
} // namespace PropulsionMKI
