#pragma once

#include "rendering/Mesh.h"
#include "rendering/VulkanContext.h"
#include <string>
#include <memory>

namespace Propulsion {

class MeshLoader {
public:
    // Load a mesh from an OBJ/FBX file using ufbx
    static std::unique_ptr<Mesh> loadFromFile(VulkanContext* context, const std::string& filepath);
    
private:
    MeshLoader() = default;
};

} // namespace Propulsion
