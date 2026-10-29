#pragma once

/**
 * MeshRendererComponent.h
 * Handles rendering configuration for Objects with meshes.
 * 
 * This component stores references to mesh and material assets,
 * along with rendering-specific properties like visibility and color.
 */

#include "core/Component.h"
#include <string>
#include <glm/glm.hpp>

namespace Propulsion {

/**
 * Mesh Renderer component - controls how an Object's mesh is rendered.
 * 
 * Stores references to mesh and material assets via their GUIDs.
 * The actual mesh/material data lives in the Content Browser's asset system.
 */
class MeshRendererComponent : public Component {
public:
    MeshRendererComponent() = default;
    
    // Component interface
    ComponentType getType() const override { return ComponentType::MeshRenderer; }
    
    std::unique_ptr<Component> clone() const override {
        auto copy = std::make_unique<MeshRendererComponent>();
        copy->meshGUID = meshGUID;
        copy->materialGUID = materialGUID;
        copy->color = color;
        copy->visible = visible;
        copy->castShadows = castShadows;
        copy->receiveShadows = receiveShadows;
        return copy;
    }
    
    // Asset references (GUIDs from Content Browser)
    std::string meshGUID;      // Reference to mesh asset
    std::string materialGUID;  // Reference to material asset
    
    // Legacy support: indices for backward compatibility during migration
    int meshIndex = -1;        // Index in mesh list (deprecated, use meshGUID)
    int materialIndex = -1;    // Index in material list (deprecated, use materialGUID)
    
    // Rendering properties
    glm::vec3 color = glm::vec3(1.0f);  // Tint color
    bool visible = true;                 // Whether to render this mesh
    bool castShadows = true;             // Whether this mesh casts shadows
    bool receiveShadows = true;          // Whether this mesh receives shadows
    
    /**
     * Check if this renderer has a valid mesh reference.
     */
    bool hasMesh() const {
        return !meshGUID.empty() || meshIndex >= 0;
    }
    
    /**
     * Check if this renderer has a valid material reference.
     */
    bool hasMaterial() const {
        return !materialGUID.empty() || materialIndex >= 0;
    }
};

} // namespace Propulsion
