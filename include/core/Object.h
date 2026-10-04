#pragma once

/**
 * Object.h
 * The fundamental entity class in Forge Engine.
 * 
 * Objects are the building blocks of scenes in Forge Engine.
 * They consist of a unique identity (GUID, name) and a collection
 * of Components that define their behavior and appearance.
 * 
 * Every Object automatically has a TransformComponent.
 * Additional components can be added/removed at runtime.
 */

#include "core/Component.h"
#include "core/TransformComponent.h"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>

namespace Forge {

/**
 * Object - The base entity class in Forge Engine.
 * 
 * Similar to Unity's GameObject or Unreal's Actor.
 * Contains components that define its properties and behavior.
 */
class Object {
public:
    /**
     * Create a new Object with default name.
     */
    Object() : Object("Object") {}
    
    /**
     * Create a new Object with the specified name.
     */
    explicit Object(const std::string& name) : m_name(name) {
        // Every Object has a Transform component by default
        auto transform = std::make_unique<TransformComponent>();
        transform->m_owner = this;
        m_transform = transform.get();
        m_components.push_back(std::move(transform));
        
        // Generate a GUID if not provided
        generateGUID();
    }
    
    ~Object() = default;
    
    // Disable copy, enable move
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = default;
    Object& operator=(Object&&) = default;
    
    // =========================================================================
    // Identity
    // =========================================================================
    
    /**
     * Get the unique identifier for this Object.
     */
    const std::string& getGUID() const { return m_guid; }
    
    /**
     * Set the GUID (typically only during deserialization).
     */
    void setGUID(const std::string& guid) { m_guid = guid; }
    
    /**
     * Get the name of this Object.
     */
    const std::string& getName() const { return m_name; }
    
    /**
     * Set the name of this Object.
     */
    void setName(const std::string& name) { m_name = name; }
    
    /**
     * Get whether this Object is visible/active in the scene.
     */
    bool isActive() const { return m_active; }
    
    /**
     * Set whether this Object is active in the scene.
     */
    void setActive(bool active) { m_active = active; }
    
    // =========================================================================
    // Transform (convenience accessors)
    // =========================================================================
    
    /**
     * Get the Transform component (always exists).
     */
    TransformComponent* getTransform() { return m_transform; }
    const TransformComponent* getTransform() const { return m_transform; }
    
    // =========================================================================
    // Component Management
    // =========================================================================
    
    /**
     * Add a component to this Object.
     * The Object takes ownership of the component.
     * 
     * @return Pointer to the added component
     */
    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {
        static_assert(std::is_base_of<Component, T>::value, 
                      "T must derive from Component");
        
        // Check if this component type already exists (for singleton components)
        if constexpr (std::is_same_v<T, TransformComponent>) {
            return m_transform; // Already have one
        }
        
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        component->m_owner = this;
        T* ptr = component.get();
        m_components.push_back(std::move(component));
        return ptr;
    }
    
    /**
     * Get a component of the specified type.
     * Returns nullptr if not found.
     */
    template<typename T>
    T* getComponent() {
        static_assert(std::is_base_of<Component, T>::value, 
                      "T must derive from Component");
        
        for (auto& comp : m_components) {
            T* casted = dynamic_cast<T*>(comp.get());
            if (casted) return casted;
        }
        return nullptr;
    }
    
    template<typename T>
    const T* getComponent() const {
        static_assert(std::is_base_of<Component, T>::value, 
                      "T must derive from Component");
        
        for (const auto& comp : m_components) {
            const T* casted = dynamic_cast<const T*>(comp.get());
            if (casted) return casted;
        }
        return nullptr;
    }
    
    /**
     * Get all components of the specified type.
     */
    template<typename T>
    std::vector<T*> getComponents() {
        static_assert(std::is_base_of<Component, T>::value, 
                      "T must derive from Component");
        
        std::vector<T*> result;
        for (auto& comp : m_components) {
            T* casted = dynamic_cast<T*>(comp.get());
            if (casted) result.push_back(casted);
        }
        return result;
    }
    
    /**
     * Check if this Object has a component of the specified type.
     */
    template<typename T>
    bool hasComponent() const {
        return getComponent<T>() != nullptr;
    }
    
    /**
     * Remove a component from this Object.
     * Cannot remove the Transform component.
     * 
     * @return true if component was removed
     */
    template<typename T>
    bool removeComponent() {
        static_assert(std::is_base_of<Component, T>::value, 
                      "T must derive from Component");
        
        auto it = std::find_if(m_components.begin(), m_components.end(),
            [](const std::unique_ptr<Component>& comp) {
                return dynamic_cast<T*>(comp.get()) != nullptr && comp->canRemove();
            });
        
        if (it != m_components.end()) {
            m_components.erase(it);
            return true;
        }
        return false;
    }
    
    /**
     * Remove a specific component instance.
     */
    bool removeComponent(Component* component) {
        if (!component || !component->canRemove()) return false;
        
        auto it = std::find_if(m_components.begin(), m_components.end(),
            [component](const std::unique_ptr<Component>& comp) {
                return comp.get() == component;
            });
        
        if (it != m_components.end()) {
            m_components.erase(it);
            return true;
        }
        return false;
    }
    
    /**
     * Get all components on this Object.
     */
    const std::vector<std::unique_ptr<Component>>& getAllComponents() const {
        return m_components;
    }
    
    /**
     * Get the number of components on this Object.
     */
    size_t getComponentCount() const { return m_components.size(); }
    
    // =========================================================================
    // Cloning
    // =========================================================================
    
    /**
     * Create a deep copy of this Object.
     * The clone will have a new GUID.
     */
    std::unique_ptr<Object> clone() const {
        auto copy = std::make_unique<Object>(m_name + " (Clone)");
        
        // Copy transform properties
        copy->m_transform->position = m_transform->position;
        copy->m_transform->rotation = m_transform->rotation;
        copy->m_transform->scale = m_transform->scale;
        copy->m_transform->quaternion = m_transform->quaternion;
        
        // Clone other components (skip transform, index 0)
        for (size_t i = 1; i < m_components.size(); ++i) {
            auto clonedComp = m_components[i]->clone();
            clonedComp->m_owner = copy.get();
            copy->m_components.push_back(std::move(clonedComp));
        }
        
        copy->m_active = m_active;
        
        return copy;
    }
    
    // =========================================================================
    // Asset References (for Content Browser integration)
    // =========================================================================
    
    /**
     * The source ContentItem ID this Object was created from (if any).
     * Empty if this is an instance not derived from a ContentItem.
     */
    std::string sourceContentItemId;
    
private:
    void generateGUID() {
        // Simple UUID v4 generation
        // In production, use a proper UUID library
        static const char* chars = "0123456789abcdef";
        m_guid.reserve(36);
        
        for (int i = 0; i < 36; ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                m_guid += '-';
            } else if (i == 14) {
                m_guid += '4'; // UUID version 4
            } else if (i == 19) {
                m_guid += chars[8 + (rand() % 4)]; // UUID variant
            } else {
                m_guid += chars[rand() % 16];
            }
        }
    }
    
    std::string m_guid;
    std::string m_name;
    bool m_active = true;
    
    std::vector<std::unique_ptr<Component>> m_components;
    TransformComponent* m_transform = nullptr; // Cached pointer for quick access
};

} // namespace Forge
