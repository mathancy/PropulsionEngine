#pragma once

/**
 * TransformComponent.h
 * Handles position, rotation, and scale for Objects.
 * 
 * Every Object in Forge Engine has a Transform component by default.
 * This component cannot be removed from an Object.
 */

#include "core/Component.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Forge {

/**
 * Transform component - controls position, rotation, and scale of an Object.
 * 
 * This is a required component - every Object must have exactly one Transform.
 */
class TransformComponent : public Component {
public:
    TransformComponent() = default;
    
    TransformComponent(const glm::vec3& pos, const glm::vec3& rot = glm::vec3(0.0f), 
                       const glm::vec3& scl = glm::vec3(1.0f))
        : position(pos), rotation(rot), scale(scl) {
        updateQuaternion();
    }
    
    // Component interface
    ComponentType getType() const override { return ComponentType::Transform; }
    bool canRemove() const override { return false; } // Transform is required
    
    std::unique_ptr<Component> clone() const override {
        auto copy = std::make_unique<TransformComponent>();
        copy->position = position;
        copy->rotation = rotation;
        copy->scale = scale;
        copy->quaternion = quaternion;
        return copy;
    }
    
    // Transform data
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);  // Euler angles in degrees
    glm::vec3 scale = glm::vec3(1.0f);
    glm::quat quaternion = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion
    
    /**
     * Update quaternion from euler rotation angles.
     * Call this after modifying rotation.
     */
    void updateQuaternion() {
        glm::vec3 radians = glm::radians(rotation);
        quaternion = glm::quat(radians);
    }
    
    /**
     * Update euler rotation from quaternion.
     * Call this after modifying quaternion directly.
     */
    void updateEulerFromQuaternion() {
        rotation = glm::degrees(glm::eulerAngles(quaternion));
    }
    
    /**
     * Get the model matrix for this transform.
     */
    glm::mat4 getModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model *= glm::mat4_cast(quaternion);
        model = glm::scale(model, scale);
        return model;
    }
    
    /**
     * Get the forward direction vector.
     */
    glm::vec3 getForward() const {
        return quaternion * glm::vec3(0.0f, 0.0f, -1.0f);
    }
    
    /**
     * Get the right direction vector.
     */
    glm::vec3 getRight() const {
        return quaternion * glm::vec3(1.0f, 0.0f, 0.0f);
    }
    
    /**
     * Get the up direction vector.
     */
    glm::vec3 getUp() const {
        return quaternion * glm::vec3(0.0f, 1.0f, 0.0f);
    }
    
    /**
     * Translate in local space.
     */
    void translateLocal(const glm::vec3& offset) {
        position += quaternion * offset;
    }
    
    /**
     * Translate in world space.
     */
    void translateWorld(const glm::vec3& offset) {
        position += offset;
    }
    
    /**
     * Look at a target position.
     */
    void lookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f)) {
        glm::vec3 direction = glm::normalize(target - position);
        if (glm::length(direction) > 0.0001f) {
            quaternion = glm::quatLookAt(direction, up);
            updateEulerFromQuaternion();
        }
    }
};

} // namespace Forge
