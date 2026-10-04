#include "core/Camera.h"

namespace Forge {

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : m_position(position), m_worldUp(up), m_yaw(yaw), m_pitch(pitch),
      m_movementSpeed(5.0f), m_mouseSensitivity(0.1f), m_zoom(45.0f),
      m_nearPlane(0.1f), m_farPlane(1000.0f) {
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    // Vulkan uses Y-axis down and Z-axis 0 to 1, so we need to flip Y
    glm::mat4 proj = glm::perspective(glm::radians(m_zoom), aspectRatio, m_nearPlane, m_farPlane);
    proj[1][1] *= -1; // Flip Y-axis for Vulkan
    return proj;
}

glm::mat4 Camera::getProjectionMatrixForRayCast(float aspectRatio) const {
    // Standard OpenGL-style projection for ray casting (no Y flip)
    return glm::perspective(glm::radians(m_zoom), aspectRatio, m_nearPlane, m_farPlane);
}

void Camera::processKeyboard(CameraMovement direction, float deltaTime) {
    float velocity = m_movementSpeed * deltaTime;
    
    switch (direction) {
        case CameraMovement::FORWARD:
            m_position += m_front * velocity;
            break;
        case CameraMovement::BACKWARD:
            m_position -= m_front * velocity;
            break;
        case CameraMovement::LEFT:
            m_position -= m_right * velocity;
            break;
        case CameraMovement::RIGHT:
            m_position += m_right * velocity;
            break;
        case CameraMovement::UP:
            m_position += m_worldUp * velocity;
            break;
        case CameraMovement::DOWN:
            m_position -= m_worldUp * velocity;
            break;
    }
}

void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= m_mouseSensitivity;
    yoffset *= m_mouseSensitivity;

    m_yaw -= xoffset;  // Inverted for natural camera control
    m_pitch += yoffset; // Not inverted - natural up/down

    if (constrainPitch) {
        if (m_pitch > 89.0f)
            m_pitch = 89.0f;
        if (m_pitch < -89.0f)
            m_pitch = -89.0f;
    }

    updateCameraVectors();
}

void Camera::processMouseScroll(float yoffset) {
    m_zoom -= yoffset;
    if (m_zoom < 1.0f)
        m_zoom = 1.0f;
    if (m_zoom > 45.0f)
        m_zoom = 45.0f;
}

void Camera::updateCameraVectors() {
    // For Z-up coordinate system (Blender-style)
    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_pitch));
    m_front = glm::normalize(front);
    
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}

} // namespace Forge
