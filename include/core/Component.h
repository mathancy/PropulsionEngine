#pragma once

/**
 * Component.h
 * Base class for all components in Propulsion MK I's Object system.
 * 
 * Components are the building blocks that give Objects their behavior
 * and representation. Each Object can have multiple components attached.
 * 
 * Part of the Propulsion MK I Object/Actor system.
 */

#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Propulsion {

// Forward declarations
class Object;

/**
 * Component type enumeration for runtime type identification.
 * Used for serialization and component queries.
 */
enum class ComponentType {
    Transform,
    MeshRenderer,
    Collider,
    Rigidbody,
    AudioSource,
    Camera,
    Light,
    Script,         // Logic Graph component
    Custom          // For future plugin components
};

/**
 * Get a human-readable name for a component type.
 */
inline const char* getComponentTypeName(ComponentType type) {
    switch (type) {
        case ComponentType::Transform:     return "Transform";
        case ComponentType::MeshRenderer:  return "Mesh Renderer";
        case ComponentType::Collider:      return "Collider";
        case ComponentType::Rigidbody:     return "Rigidbody";
        case ComponentType::AudioSource:   return "Audio Source";
        case ComponentType::Camera:        return "Camera";
        case ComponentType::Light:         return "Light";
        case ComponentType::Script:        return "Script";
        case ComponentType::Custom:        return "Custom";
        default:                           return "Unknown";
    }
}

/**
 * Base class for all components.
 * 
 * Components should:
 * - Be lightweight data containers
 * - Not contain heavy logic (systems process components)
 * - Be serializable
 * - Have sensible defaults
 */
class Component {
public:
    virtual ~Component() = default;
    
    /**
     * Get the type of this component.
     */
    virtual ComponentType getType() const = 0;
    
    /**
     * Get the type name as a string (for UI/serialization).
     */
    virtual const char* getTypeName() const {
        return getComponentTypeName(getType());
    }
    
    /**
     * Clone this component (for Object duplication/instancing).
     */
    virtual std::unique_ptr<Component> clone() const = 0;
    
    /**
     * Whether this component can be removed from an Object.
     * Transform components typically return false.
     */
    virtual bool canRemove() const { return true; }
    
    /**
     * Get the owner Object of this component.
     */
    Object* getOwner() const { return m_owner; }
    
protected:
    friend class Object;
    Object* m_owner = nullptr;
};

} // namespace Propulsion
