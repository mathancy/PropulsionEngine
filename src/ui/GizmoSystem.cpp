#include "Engine.h"
#include "rendering/VulkanContext.h"
#include "rendering/Shader.h"
#include "rendering/Mesh.h"
#include "core/Camera.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
#undef max  // Prevent Windows.h max macro from conflicting with std::max
#undef min  // Prevent Windows.h min macro from conflicting with std::min
#endif

namespace Forge {

void Engine::renderGizmo(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& projection) {
    // Only render for Translate, Rotate, or Scale mode
    if (m_gizmoMode != GizmoMode::Translate && m_gizmoMode != GizmoMode::Rotate && m_gizmoMode != GizmoMode::Scale) {
        return;
    }
    
    if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) {
        return;
    }
    
    if (!m_gizmoShader || !m_gizmoCube || !m_gizmoCone || !m_gizmoTorus || !m_gizmoDisc) {
        return;
    }
    
    SceneObject& obj = m_sceneObjects[m_selectedObjectIndex];
    glm::vec3 gizmoPos = obj.position;
    
    // Calculate gizmo size based on distance from camera (constant screen size)
    float distance = glm::length(m_camera->getPosition() - gizmoPos);
    float gizmoScale = distance * 0.12f;
    
    // Axis parameters
    float shaftLength = gizmoScale;
    float shaftThickness = gizmoScale * 0.025f;
    float coneHeight = gizmoScale * 0.12f;  // Half the previous size
    float coneRadius = gizmoScale * 0.06f;
    
    // Alpha values: 0.7 default, 1.0 highlighted (fully opaque when highlighted)
    const float defaultAlpha = 0.6f;
    const float highlightAlpha = 1.0f;
    
    // Axis colors with alpha
    bool xHighlighted = (m_hoveredGizmoAxis == GizmoAxis::X || m_activeGizmoAxis == GizmoAxis::X);
    bool yHighlighted = (m_hoveredGizmoAxis == GizmoAxis::Y || m_activeGizmoAxis == GizmoAxis::Y);
    bool zHighlighted = (m_hoveredGizmoAxis == GizmoAxis::Z || m_activeGizmoAxis == GizmoAxis::Z);
    
    glm::vec4 xColor = glm::vec4(1.0f, 0.4f, 0.4f, xHighlighted ? highlightAlpha : defaultAlpha);
    glm::vec4 yColor = glm::vec4(0.4f, 1.0f, 0.4f, yHighlighted ? highlightAlpha : defaultAlpha);
    glm::vec4 zColor = glm::vec4(0.4f, 0.4f, 1.0f, zHighlighted ? highlightAlpha : defaultAlpha);
    
    // Bind gizmo shader and set view/projection (constant for entire frame)
    m_gizmoShader->bind(commandBuffer);
    
    GizmoUniformBufferObject ubo{};
    ubo.view = view;
    ubo.projection = projection;
    m_gizmoShader->updateGizmoUniformBuffer(m_vulkanContext->getCurrentFrame(), ubo);
    
    GizmoPushConstants push{};
    
    if (m_gizmoMode == GizmoMode::Translate) {
        // ============ TRANSLATE GIZMO ============
        // ============ X AXIS (Red) ============
    // X shaft - thin cube stretched along X
    glm::mat4 model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(shaftLength * 0.5f, 0.0f, 0.0f));
    model = glm::scale(model, glm::vec3(shaftLength, shaftThickness, shaftThickness));
    push.model = model;
    push.color = xColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCube->draw(commandBuffer);
    
    // X cone - at end of shaft, pointing along +X
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(shaftLength + coneHeight * 0.5f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f)); // Rotate cone to point +X
    model = glm::scale(model, glm::vec3(coneRadius, coneHeight, coneRadius));
    push.model = model;
    push.color = xColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCone->draw(commandBuffer);
    
    // ============ Y AXIS (Green) ============
    // Y shaft - thin cube stretched along Y
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(0.0f, shaftLength * 0.5f, 0.0f));
    model = glm::scale(model, glm::vec3(shaftThickness, shaftLength, shaftThickness));
    push.model = model;
    push.color = yColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCube->draw(commandBuffer);
    
    // Y cone - at end of shaft, pointing along +Y (default orientation)
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(0.0f, shaftLength + coneHeight * 0.5f, 0.0f));
    model = glm::scale(model, glm::vec3(coneRadius, coneHeight, coneRadius));
    push.model = model;
    push.color = yColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCone->draw(commandBuffer);
    
    // ============ Z AXIS (Blue) ============
    // Z shaft - thin cube stretched along Z
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(0.0f, 0.0f, shaftLength * 0.5f));
    model = glm::scale(model, glm::vec3(shaftThickness, shaftThickness, shaftLength));
    push.model = model;
    push.color = zColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCube->draw(commandBuffer);
    
    // Z cone - at end of shaft, pointing along +Z
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(0.0f, 0.0f, shaftLength + coneHeight * 0.5f));
    model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate cone to point +Z
    model = glm::scale(model, glm::vec3(coneRadius, coneHeight, coneRadius));
    push.model = model;
    push.color = zColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCone->draw(commandBuffer);
    
    // ============ PLANE HANDLES ============
    float planeSize = shaftLength * 0.33f;  // 1/3 of axis length
    float planeOffset = planeSize * 1.2f;   // Offset from origin - pushed out to float between axis lines
    float planeThickness = shaftThickness * 0.5f;
    
    // Plane colors with alpha (highlighted when hovered/active)
    bool xzHighlighted = (m_hoveredGizmoAxis == GizmoAxis::XZ || m_activeGizmoAxis == GizmoAxis::XZ);
    bool yzHighlighted = (m_hoveredGizmoAxis == GizmoAxis::YZ || m_activeGizmoAxis == GizmoAxis::YZ);
    bool xyHighlighted = (m_hoveredGizmoAxis == GizmoAxis::XY || m_activeGizmoAxis == GizmoAxis::XY);
    
    // XZ plane (Red + Blue = Purple)
    glm::vec4 xzColor = glm::vec4(0.9f, 0.5f, 0.9f, xzHighlighted ? highlightAlpha : defaultAlpha);
    // YZ plane (Green + Blue = Teal/Cyan)
    glm::vec4 yzColor = glm::vec4(0.5f, 0.9f, 0.9f, yzHighlighted ? highlightAlpha : defaultAlpha);
    // XY plane (Red + Green = Yellow)
    glm::vec4 xyColor = glm::vec4(1.0f, 1.0f, 0.5f, xyHighlighted ? highlightAlpha : defaultAlpha);
    
    // XZ Plane (Purple) - flat on Y axis, extends in X and Z
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(planeOffset, 0.0f, planeOffset));
    model = glm::scale(model, glm::vec3(planeSize, planeThickness, planeSize));
    push.model = model;
    push.color = xzColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCube->draw(commandBuffer);
    
    // YZ Plane (Teal) - flat on X axis, extends in Y and Z
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(0.0f, planeOffset, planeOffset));
    model = glm::scale(model, glm::vec3(planeThickness, planeSize, planeSize));
    push.model = model;
    push.color = yzColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCube->draw(commandBuffer);
    
    // XY Plane (Yellow) - flat on Z axis, extends in X and Y
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::translate(model, glm::vec3(planeOffset, planeOffset, 0.0f));
    model = glm::scale(model, glm::vec3(planeSize, planeSize, planeThickness));
    push.model = model;
    push.color = xyColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCube->draw(commandBuffer);
    
    // ============ CENTER CUBE (Screen-space translation) ============
    // Render last so it appears on top of axis lines and plane handles
    float centerCubeSize = gizmoScale * 0.15f;
    
    // Center cube color with alpha (white/gray, highlighted when hovered/active)
    bool centerHighlighted = (m_hoveredGizmoAxis == GizmoAxis::Center || m_activeGizmoAxis == GizmoAxis::Center);
    glm::vec4 centerColor = glm::vec4(0.9f, 0.9f, 0.9f, centerHighlighted ? highlightAlpha : defaultAlpha);
    
    model = glm::translate(glm::mat4(1.0f), gizmoPos);
    model = glm::scale(model, glm::vec3(centerCubeSize, centerCubeSize, centerCubeSize));
    push.model = model;
    push.color = centerColor;
    m_gizmoShader->pushConstants(commandBuffer, push);
    m_gizmoCube->draw(commandBuffer);
    }
    else if (m_gizmoMode == GizmoMode::Rotate) {
        // ============ ROTATION GIZMO - THREE TORUS RINGS ============
        // Default torus lies in XZ plane (horizontal ring)
        
        // X ring (Red) - rotates around X axis, ring lies in YZ plane
        glm::mat4 model = glm::translate(glm::mat4(1.0f), gizmoPos);
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)); // Rotate around Z to put ring in YZ plane
        model = glm::scale(model, glm::vec3(gizmoScale));
        push.model = model;
        push.color = xColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoTorus->draw(commandBuffer);
        
        // Y ring (Green) - rotates around Y axis, ring lies in XZ plane (default orientation)
        model = glm::translate(glm::mat4(1.0f), gizmoPos);
        model = glm::scale(model, glm::vec3(gizmoScale));
        push.model = model;
        push.color = yColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoTorus->draw(commandBuffer);
        
        // Z ring (Blue) - rotates around Z axis, ring lies in XY plane
        model = glm::translate(glm::mat4(1.0f), gizmoPos);
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate around X to put ring in XY plane
        model = glm::scale(model, glm::vec3(gizmoScale));
        push.model = model;
        push.color = zColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoTorus->draw(commandBuffer);
        
        // ============ ROTATION FEEDBACK DISC ============
        // Only render when actively rotating (gizmo is being dragged)
        if (m_gizmoActive && m_activeGizmoAxis != GizmoAxis::None && m_gizmoDisc) {
            const float TWO_PI = 2.0f * 3.14159265359f;
            
            // Calculate the angular sweep from start to current
            float angleDiff = m_rotationAccumulatedAngle;  // This is relative to start
            
            // Skip if barely any rotation
            if (std::abs(angleDiff) < 0.02f) {
                // Don't render disc for tiny rotations
            } else {
                // Disc color matches the active axis with more transparency
                glm::vec4 discColor;
                glm::mat4 discModel = glm::translate(glm::mat4(1.0f), gizmoPos);
                
                // Determine if we need to flip the disc based on rotation direction
                // The disc mesh is wound counter-clockwise in local space
                // Different axes have different handedness after their orientation transforms
                bool needsFlip = false;
                
                // Use the EXACT same orientation as the corresponding ring
                // Then rotate around the ring's axis (Y in disc local space) to align the start angle
                if (m_activeGizmoAxis == GizmoAxis::X) {
                    discColor = glm::vec4(1.0f, 0.4f, 0.4f, 0.4f);
                    // Same as X ring: rotate 90 around Z to put in YZ plane
                    discModel = glm::rotate(discModel, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                    // Then rotate around the local Y axis (which is now world X) to align start
                    discModel = glm::rotate(discModel, m_rotationStartAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                    // X axis has opposite handedness - flip for clockwise (negative) rotation
                    needsFlip = angleDiff < 0.0f;
                }
                else if (m_activeGizmoAxis == GizmoAxis::Y) {
                    discColor = glm::vec4(0.4f, 1.0f, 0.4f, 0.4f);
                    // Same as Y ring: no rotation (disc in XZ plane, default orientation)
                    // Rotate around local Y to align start angle
                    discModel = glm::rotate(discModel, m_rotationStartAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                    // Y and Z axes - flip for counter-clockwise (positive) rotation
                    needsFlip = angleDiff > 0.0f;
                }
                else if (m_activeGizmoAxis == GizmoAxis::Z) {
                    discColor = glm::vec4(0.4f, 0.4f, 1.0f, 0.4f);
                    // Same as Z ring: rotate 90 around X to put in XY plane
                    discModel = glm::rotate(discModel, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                    // Then rotate around the local Y axis (which is now world Z) to align start
                    discModel = glm::rotate(discModel, m_rotationStartAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                    // Y and Z axes - flip for counter-clockwise (positive) rotation
                    needsFlip = angleDiff > 0.0f;
                }
                
                // Flip the disc by rotating 180° around local X axis
                // This keeps front faces visible (unlike negative scale which would flip normals)
                // while reversing the visual winding direction
                if (needsFlip) {
                    discModel = glm::rotate(discModel, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                }
                
                discModel = glm::scale(discModel, glm::vec3(gizmoScale));
                push.model = discModel;
                push.color = discColor;
                m_gizmoShader->pushConstants(commandBuffer, push);
                
                // Calculate how many triangles to draw based on the absolute rotation angle
                int totalSegments = m_gizmoDisc->getSegmentCount();
                float absAngle = std::abs(angleDiff);
                float angleFraction = absAngle / TWO_PI;
                angleFraction = std::min(angleFraction, 1.0f);
                int segmentsToDraw = std::max(1, static_cast<int>(angleFraction * totalSegments));
                
                // Draw top side triangles
                uint32_t topIndicesToDraw = segmentsToDraw * 3;
                m_gizmoDisc->drawPartial(commandBuffer, topIndicesToDraw);
                
                // Draw bottom side triangles (they start after all top triangles)
                // Bottom triangles are at indices [totalSegments*3 ... totalSegments*6)
                uint32_t bottomStartIndex = totalSegments * 3;
                m_gizmoDisc->drawPartialRange(commandBuffer, bottomStartIndex, topIndicesToDraw);
            }
        }
    }
    else if (m_gizmoMode == GizmoMode::Scale) {
        // ============ SCALE GIZMO (Local Space) ============
        // Scale gizmo follows the object's rotation
        float cubeSize = gizmoScale * 0.12f;
        
        // Build rotation matrix from object's quaternion
        glm::mat4 rotMat = glm::mat4_cast(obj.quaternion);
        
        // Get the local axes after rotation
        glm::vec3 localX = glm::vec3(rotMat * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        glm::vec3 localY = glm::vec3(rotMat * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        glm::vec3 localZ = glm::vec3(rotMat * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
        
        // Flip axes if they point in negative direction (keep handles pointing "positive")
        // X axis: flip if pointing left (negative world X)
        float xSign = (localX.x >= 0.0f) ? 1.0f : -1.0f;
        // Y axis: flip if pointing down (negative world Y)  
        float ySign = (localY.y >= 0.0f) ? 1.0f : -1.0f;
        // Z axis: flip if pointing backward (negative world Z)
        float zSign = (localZ.z >= 0.0f) ? 1.0f : -1.0f;
        
        // Apply flips to get corrected local axes
        glm::vec3 axisX = localX * xSign;
        glm::vec3 axisY = localY * ySign;
        glm::vec3 axisZ = localZ * zSign;
        
        // Helper lambda to create model matrix for axis-aligned elements in local space
        auto makeLocalModel = [&](glm::vec3 localOffset, glm::vec3 scaleVec) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), gizmoPos);
            // Apply the corrected rotation
            glm::mat4 correctedRot = glm::mat4(1.0f);
            correctedRot[0] = glm::vec4(axisX, 0.0f);
            correctedRot[1] = glm::vec4(axisY, 0.0f);
            correctedRot[2] = glm::vec4(axisZ, 0.0f);
            model = model * correctedRot;
            model = glm::translate(model, localOffset);
            model = glm::scale(model, scaleVec);
            return model;
        };
        
        // ============ X AXIS (Red) ============
        // X shaft
        glm::mat4 model = makeLocalModel(glm::vec3(shaftLength * 0.5f, 0.0f, 0.0f), 
                                          glm::vec3(shaftLength, shaftThickness, shaftThickness));
        push.model = model;
        push.color = xColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // X cube
        model = makeLocalModel(glm::vec3(shaftLength + cubeSize * 0.5f, 0.0f, 0.0f),
                               glm::vec3(cubeSize, cubeSize, cubeSize));
        push.model = model;
        push.color = xColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // ============ Y AXIS (Green) ============
        // Y shaft
        model = makeLocalModel(glm::vec3(0.0f, shaftLength * 0.5f, 0.0f),
                               glm::vec3(shaftThickness, shaftLength, shaftThickness));
        push.model = model;
        push.color = yColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // Y cube
        model = makeLocalModel(glm::vec3(0.0f, shaftLength + cubeSize * 0.5f, 0.0f),
                               glm::vec3(cubeSize, cubeSize, cubeSize));
        push.model = model;
        push.color = yColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // ============ Z AXIS (Blue) ============
        // Z shaft
        model = makeLocalModel(glm::vec3(0.0f, 0.0f, shaftLength * 0.5f),
                               glm::vec3(shaftThickness, shaftThickness, shaftLength));
        push.model = model;
        push.color = zColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // Z cube
        model = makeLocalModel(glm::vec3(0.0f, 0.0f, shaftLength + cubeSize * 0.5f),
                               glm::vec3(cubeSize, cubeSize, cubeSize));
        push.model = model;
        push.color = zColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // ============ PLANE HANDLES (Dual-axis scaling) ============
        float planeSize = shaftLength * 0.33f;
        float planeOffset = planeSize * 1.2f;
        float planeThickness = shaftThickness * 0.5f;
        
        bool xzHighlighted = (m_hoveredGizmoAxis == GizmoAxis::XZ || m_activeGizmoAxis == GizmoAxis::XZ);
        bool yzHighlighted = (m_hoveredGizmoAxis == GizmoAxis::YZ || m_activeGizmoAxis == GizmoAxis::YZ);
        bool xyHighlighted = (m_hoveredGizmoAxis == GizmoAxis::XY || m_activeGizmoAxis == GizmoAxis::XY);
        
        glm::vec4 xzColor = glm::vec4(0.9f, 0.5f, 0.9f, xzHighlighted ? highlightAlpha : defaultAlpha);
        glm::vec4 yzColor = glm::vec4(0.5f, 0.9f, 0.9f, yzHighlighted ? highlightAlpha : defaultAlpha);
        glm::vec4 xyColor = glm::vec4(1.0f, 1.0f, 0.5f, xyHighlighted ? highlightAlpha : defaultAlpha);
        
        // XZ Plane (Purple)
        model = makeLocalModel(glm::vec3(planeOffset, 0.0f, planeOffset),
                               glm::vec3(planeSize, planeThickness, planeSize));
        push.model = model;
        push.color = xzColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // YZ Plane (Teal)
        model = makeLocalModel(glm::vec3(0.0f, planeOffset, planeOffset),
                               glm::vec3(planeThickness, planeSize, planeSize));
        push.model = model;
        push.color = yzColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // XY Plane (Yellow)
        model = makeLocalModel(glm::vec3(planeOffset, planeOffset, 0.0f),
                               glm::vec3(planeSize, planeSize, planeThickness));
        push.model = model;
        push.color = xyColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoCube->draw(commandBuffer);
        
        // ============ CENTER RING (Uniform scale) - Camera-facing ============
        float ringRadius = gizmoScale * 1.15f;  // Ring around the whole gizmo
        
        bool centerHighlighted = (m_hoveredGizmoAxis == GizmoAxis::Center || m_activeGizmoAxis == GizmoAxis::Center);
        glm::vec4 centerColor = glm::vec4(0.9f, 0.9f, 0.9f, centerHighlighted ? highlightAlpha : defaultAlpha);
        
        // Make the ring face the camera (billboard)
        glm::vec3 toCamera = glm::normalize(m_camera->getPosition() - gizmoPos);
        glm::vec3 worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        
        // Handle case when looking straight down/up
        if (abs(glm::dot(toCamera, worldUp)) > 0.99f) {
            worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        
        glm::vec3 right = glm::normalize(glm::cross(worldUp, toCamera));
        glm::vec3 up = glm::normalize(glm::cross(toCamera, right));
        
        // Build billboard rotation matrix
        glm::mat4 billboardRot = glm::mat4(1.0f);
        billboardRot[0] = glm::vec4(right, 0.0f);
        billboardRot[1] = glm::vec4(up, 0.0f);
        billboardRot[2] = glm::vec4(toCamera, 0.0f);
        
        // The torus is in the XZ plane by default (Y is the "up" axis through the hole)
        // We need to rotate it so it faces the camera (its Y axis aligns with toCamera)
        // Rotate 90 degrees around X to make the torus face along Y instead of Z
        glm::mat4 torusOrient = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        
        model = glm::translate(glm::mat4(1.0f), gizmoPos);
        model = model * billboardRot;
        model = model * torusOrient;
        model = glm::scale(model, glm::vec3(ringRadius));  // Uniform scale - no distortion
        push.model = model;
        push.color = centerColor;
        m_gizmoShader->pushConstants(commandBuffer, push);
        m_gizmoTorus->draw(commandBuffer);
    }
}

bool Engine::rayCastGizmo(const glm::vec3& rayOrigin, const glm::vec3& rayDir, GizmoAxis& hitAxis) {
    if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) {
        return false;
    }
    
    SceneObject& obj = m_sceneObjects[m_selectedObjectIndex];
    glm::vec3 gizmoPos = obj.position;
    float distance = glm::length(m_camera->getPosition() - gizmoPos);
    float gizmoSize = distance * 0.12f;  // Match visual gizmo size (same as renderGizmo)
    float hitThreshold = gizmoSize * 0.35f; // Generous hit detection threshold
    
    float closestDist = FLT_MAX;
    GizmoAxis closest = GizmoAxis::None;
    
    if (m_gizmoMode == GizmoMode::Translate || m_gizmoMode == GizmoMode::Scale) {
        // Get axis directions - for Scale mode, use local (rotated) axes with flipping; for Translate, use world axes
        glm::vec3 xAxisDir = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 yAxisDir = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 zAxisDir = glm::vec3(0.0f, 0.0f, 1.0f);
        
        if (m_gizmoMode == GizmoMode::Scale) {
            // Use the same logic as rendering - get rotated axes and flip if pointing negative
            glm::mat4 rotMat = glm::mat4_cast(obj.quaternion);
            
            glm::vec3 localX = glm::vec3(rotMat * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            glm::vec3 localY = glm::vec3(rotMat * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            glm::vec3 localZ = glm::vec3(rotMat * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
            
            // Flip axes if they point in negative direction (same as rendering)
            float xSign = (localX.x >= 0.0f) ? 1.0f : -1.0f;
            float ySign = (localY.y >= 0.0f) ? 1.0f : -1.0f;
            float zSign = (localZ.z >= 0.0f) ? 1.0f : -1.0f;
            
            xAxisDir = localX * xSign;
            yAxisDir = localY * ySign;
            zAxisDir = localZ * zSign;
        }
        
        // Check X axis
        glm::vec3 axisStart = gizmoPos;
        glm::vec3 axisEnd = gizmoPos + xAxisDir * gizmoSize;
        glm::vec3 axisVec = axisEnd - axisStart;
        glm::vec3 rayToAxisStart = axisStart - rayOrigin;
        glm::vec3 cross = glm::cross(rayDir, axisVec);
        float crossLen = glm::length(cross);
        float perpDist = (crossLen > 0.0001f) ? abs(glm::dot(rayToAxisStart, cross) / crossLen) : FLT_MAX;
        
        // Calculate actual distance along ray to closest point on axis
        float rayDist = FLT_MAX;
        if (crossLen > 0.0001f && perpDist < hitThreshold) {
            // Find t parameter along axis where ray comes closest
            glm::vec3 w = rayToAxisStart;
            float a = glm::dot(axisVec, axisVec);
            float b = glm::dot(axisVec, rayDir);
            float c = glm::dot(rayDir, rayDir);
            float d = glm::dot(axisVec, w);
            float e = glm::dot(rayDir, w);
            float denom = a * c - b * b;
            float t = (denom != 0.0f) ? (b * e - c * d) / denom : 0.0f;
            
            // Only accept hits within the actual gizmo geometry bounds (0 to 1, with small buffer for cone)
            if (t >= -0.2f && t <= 1.2f) {
                glm::vec3 closestOnAxis = axisStart + axisVec * t;
                rayDist = glm::length(closestOnAxis - rayOrigin);
            }
        }
        
        if (perpDist < hitThreshold && rayDist < closestDist) {
            closestDist = rayDist;
            closest = GizmoAxis::X;
        }
        
        // Check Y axis (forward along Y)
        axisEnd = gizmoPos + yAxisDir * gizmoSize;
        axisVec = axisEnd - axisStart;
        rayToAxisStart = axisStart - rayOrigin;
        cross = glm::cross(rayDir, axisVec);
        crossLen = glm::length(cross);
        perpDist = (crossLen > 0.0001f) ? abs(glm::dot(rayToAxisStart, cross) / crossLen) : FLT_MAX;
        
        rayDist = FLT_MAX;
        if (crossLen > 0.0001f && perpDist < hitThreshold) {
            glm::vec3 w = rayToAxisStart;
            float a = glm::dot(axisVec, axisVec);
            float b = glm::dot(axisVec, rayDir);
            float c = glm::dot(rayDir, rayDir);
            float d = glm::dot(axisVec, w);
            float e = glm::dot(rayDir, w);
            float denom = a * c - b * b;
            float t = (denom != 0.0f) ? (b * e - c * d) / denom : 0.0f;
            
            if (t >= -0.2f && t <= 1.2f) {
                glm::vec3 closestOnAxis = axisStart + axisVec * t;
                rayDist = glm::length(closestOnAxis - rayOrigin);
            }
        }
        
        if (perpDist < hitThreshold && rayDist < closestDist) {
            closestDist = rayDist;
            closest = GizmoAxis::Y;
        }
        
        // Check Z axis (up along Z)
        axisEnd = gizmoPos + zAxisDir * gizmoSize;
        axisVec = axisEnd - axisStart;
        rayToAxisStart = axisStart - rayOrigin;
        cross = glm::cross(rayDir, axisVec);
        crossLen = glm::length(cross);
        perpDist = (crossLen > 0.0001f) ? abs(glm::dot(rayToAxisStart, cross) / crossLen) : FLT_MAX;
        
        rayDist = FLT_MAX;
        if (crossLen > 0.0001f && perpDist < hitThreshold) {
            glm::vec3 w = rayToAxisStart;
            float a = glm::dot(axisVec, axisVec);
            float b = glm::dot(axisVec, rayDir);
            float c = glm::dot(rayDir, rayDir);
            float d = glm::dot(axisVec, w);
            float e = glm::dot(rayDir, w);
            float denom = a * c - b * b;
            float t = (denom != 0.0f) ? (b * e - c * d) / denom : 0.0f;
            
            if (t >= -0.2f && t <= 1.2f) {
                glm::vec3 closestOnAxis = axisStart + axisVec * t;
                rayDist = glm::length(closestOnAxis - rayOrigin);
            }
        }
        
        if (perpDist < hitThreshold && rayDist < closestDist) {
            closestDist = rayDist;
            closest = GizmoAxis::Z;
        }
        
        // Check plane handles FIRST (for both Scale and Translate modes)
        // Planes take priority over axis lines when there's a direct hit
        if (m_gizmoMode == GizmoMode::Scale || m_gizmoMode == GizmoMode::Translate) {
            // Match rendering: planeSize = gizmoSize * 0.33f, planeOffset = planeSize * 1.2f
            float planeSize = gizmoSize * 0.33f;
            float planeOffset = planeSize * 1.2f;
            float planeHalfExtent = planeSize * 0.5f;  // Half-size for bounds check
            
            float planeClosestDist = FLT_MAX;
            GizmoAxis planeClosest = GizmoAxis::None;
            
            // XY Plane (between X and Y axes)
            glm::vec3 planeCenter = gizmoPos + xAxisDir * planeOffset + yAxisDir * planeOffset;
            glm::vec3 planeNormal = glm::normalize(glm::cross(xAxisDir, yAxisDir));
            float denom = glm::dot(rayDir, planeNormal);
            if (abs(denom) > 0.0001f) {
                float t = glm::dot(planeCenter - rayOrigin, planeNormal) / denom;
                if (t > 0) {
                    glm::vec3 hitPoint = rayOrigin + rayDir * t;
                    glm::vec3 localHit = hitPoint - planeCenter;
                    float projX = glm::dot(localHit, xAxisDir);
                    float projY = glm::dot(localHit, yAxisDir);
                    if (abs(projX) < planeHalfExtent && abs(projY) < planeHalfExtent) {
                        float planeDist = glm::length(hitPoint - rayOrigin);
                        if (planeDist < planeClosestDist) {
                            planeClosestDist = planeDist;
                            planeClosest = GizmoAxis::XY;
                        }
                    }
                }
            }
            
            // XZ Plane (between X and Z axes)
            planeCenter = gizmoPos + xAxisDir * planeOffset + zAxisDir * planeOffset;
            planeNormal = glm::normalize(glm::cross(xAxisDir, zAxisDir));
            denom = glm::dot(rayDir, planeNormal);
            if (abs(denom) > 0.0001f) {
                float t = glm::dot(planeCenter - rayOrigin, planeNormal) / denom;
                if (t > 0) {
                    glm::vec3 hitPoint = rayOrigin + rayDir * t;
                    glm::vec3 localHit = hitPoint - planeCenter;
                    float projX = glm::dot(localHit, xAxisDir);
                    float projZ = glm::dot(localHit, zAxisDir);
                    if (abs(projX) < planeHalfExtent && abs(projZ) < planeHalfExtent) {
                        float planeDist = glm::length(hitPoint - rayOrigin);
                        if (planeDist < planeClosestDist) {
                            planeClosestDist = planeDist;
                            planeClosest = GizmoAxis::XZ;
                        }
                    }
                }
            }
            
            // YZ Plane (between Y and Z axes)
            planeCenter = gizmoPos + yAxisDir * planeOffset + zAxisDir * planeOffset;
            planeNormal = glm::normalize(glm::cross(yAxisDir, zAxisDir));
            denom = glm::dot(rayDir, planeNormal);
            if (abs(denom) > 0.0001f) {
                float t = glm::dot(planeCenter - rayOrigin, planeNormal) / denom;
                if (t > 0) {
                    glm::vec3 hitPoint = rayOrigin + rayDir * t;
                    glm::vec3 localHit = hitPoint - planeCenter;
                    float projY = glm::dot(localHit, yAxisDir);
                    float projZ = glm::dot(localHit, zAxisDir);
                    if (abs(projY) < planeHalfExtent && abs(projZ) < planeHalfExtent) {
                        float planeDist = glm::length(hitPoint - rayOrigin);
                        if (planeDist < planeClosestDist) {
                            planeClosestDist = planeDist;
                            planeClosest = GizmoAxis::YZ;
                        }
                    }
                }
            }
            
            // If we hit a plane, it takes priority over axis lines (no distance fudging needed)
            if (planeClosest != GizmoAxis::None) {
                closestDist = planeClosestDist;
                closest = planeClosest;
            }
        }
        
        // Check center handle - different for Translate vs Scale
        if (m_gizmoMode == GizmoMode::Translate) {
            // Translate uses a center CUBE
            float centerCubeSize = gizmoSize * 0.15f;
            float centerRadius = centerCubeSize * 0.866f; // sqrt(3)/2 for cube diagonal
            
            // Sphere-ray intersection test for center cube
            glm::vec3 oc = rayOrigin - gizmoPos;
            float a = glm::dot(rayDir, rayDir);
            float b = 2.0f * glm::dot(oc, rayDir);
            float c = glm::dot(oc, oc) - centerRadius * centerRadius;
            float discriminant = b * b - 4 * a * c;
            
            if (discriminant >= 0) {
                float t = (-b - sqrt(discriminant)) / (2.0f * a);
                if (t > 0) {
                    // Center cube takes absolute priority
                    closestDist = glm::length((rayOrigin + rayDir * t) - rayOrigin);
                    closest = GizmoAxis::Center;
                }
            }
        }
        else if (m_gizmoMode == GizmoMode::Scale) {
            // Scale uses a camera-facing RING around the gizmo
            float ringRadius = gizmoSize * 1.15f;  // Match rendering
            float ringThickness = gizmoSize * 0.08f;  // Hit detection threshold
            
            // Ring is on a plane perpendicular to camera
            glm::vec3 toCamera = glm::normalize(m_camera->getPosition() - gizmoPos);
            
            // Intersect ray with the camera-facing plane at gizmo position
            float denom = glm::dot(rayDir, toCamera);
            if (abs(denom) > 0.0001f) {
                float t = glm::dot(gizmoPos - rayOrigin, toCamera) / denom;
                if (t > 0) {
                    glm::vec3 hitPoint = rayOrigin + rayDir * t;
                    float distFromCenter = glm::length(hitPoint - gizmoPos);
                    
                    // Check if hit is on the ring (within thickness of the ring radius)
                    if (abs(distFromCenter - ringRadius) < ringThickness) {
                        float ringDist = glm::length(hitPoint - rayOrigin);
                        // Ring has highest priority
                        closestDist = ringDist;
                        closest = GizmoAxis::Center;
                    }
                }
            }
        }
    }
    else if (m_gizmoMode == GizmoMode::Rotate) {
        // For rotation, check intersection with torus rings
        // Each ring is on a plane perpendicular to its axis
        float hitThreshold = gizmoSize * 0.1f; // World-space threshold for rotation rings
        
        // Check X axis ring (YZ plane)
        glm::vec3 planeNormal = glm::vec3(1.0f, 0.0f, 0.0f);
        float denom = glm::dot(rayDir, planeNormal);
        if (abs(denom) > 0.0001f) {
            float t = glm::dot(gizmoPos - rayOrigin, planeNormal) / denom;
            if (t > 0) {
                glm::vec3 hitPoint = rayOrigin + rayDir * t;
                float distFromCenter = glm::length(hitPoint - gizmoPos);
                if (abs(distFromCenter - gizmoSize) < hitThreshold * 2.0f) {
                    closest = GizmoAxis::X;
                    closestDist = abs(distFromCenter - gizmoSize);
                }
            }
        }
        
        // Check Y axis ring (XZ plane - vertical) - rotates around Y
        planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        denom = glm::dot(rayDir, planeNormal);
        if (abs(denom) > 0.0001f) {
            float t = glm::dot(gizmoPos - rayOrigin, planeNormal) / denom;
            if (t > 0) {
                glm::vec3 hitPoint = rayOrigin + rayDir * t;
                float distFromCenter = glm::length(hitPoint - gizmoPos);
                if (abs(distFromCenter - gizmoSize) < hitThreshold * 2.0f && abs(distFromCenter - gizmoSize) < closestDist) {
                    closest = GizmoAxis::Y;
                    closestDist = abs(distFromCenter - gizmoSize);
                }
            }
        }
        
        // Check Z axis ring (XY plane - horizontal spin around Z-up)
        planeNormal = glm::vec3(0.0f, 0.0f, 1.0f);
        denom = glm::dot(rayDir, planeNormal);
        if (abs(denom) > 0.0001f) {
            float t = glm::dot(gizmoPos - rayOrigin, planeNormal) / denom;
            if (t > 0) {
                glm::vec3 hitPoint = rayOrigin + rayDir * t;
                float distFromCenter = glm::length(hitPoint - gizmoPos);
                if (abs(distFromCenter - gizmoSize) < hitThreshold * 2.0f && abs(distFromCenter - gizmoSize) < closestDist) {
                    closest = GizmoAxis::Z;
                    closestDist = abs(distFromCenter - gizmoSize);
                }
            }
        }
    }
    
    if (closest != GizmoAxis::None) {
        hitAxis = closest;
        return true;
    }
    
    return false;
}

void Engine::handleGizmoInteraction() {
    if (!m_gizmoActive || m_selectedObjectIndex < 0 || m_activeGizmoAxis == GizmoAxis::None) {
        return;
    }
    
    // Get current mouse ray - use unflipped projection for ray casting
    ImVec2 mousePos = ImGui::GetMousePos();
    float mouseX = mousePos.x - m_viewportPos.x;
    float mouseY = mousePos.y - m_viewportPos.y;
    
    float ndcX = (2.0f * mouseX) / m_viewportSize.x - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY) / m_viewportSize.y;
    
    glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 projection = m_camera->getProjectionMatrixForRayCast(aspectRatio);
    glm::vec4 rayEye = glm::inverse(projection) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    
    glm::mat4 view = m_camera->getViewMatrix();
    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    rayWorld = glm::normalize(rayWorld);
    glm::vec3 rayOrigin = m_camera->getPosition();
    
    SceneObject& obj = m_sceneObjects[m_selectedObjectIndex];
    
    // Determine axis direction
    // For Scale mode, use the visual (rotated) axis direction with flipping
    // For other modes, use world axes
    glm::vec3 axisDir(0.0f);
    bool isPlaneMode = false;
    glm::vec3 planeNormal(0.0f);
    
    // Calculate rotated axis directions for Scale mode (matching hit detection & rendering)
    glm::vec3 xAxisDir = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 yAxisDir = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 zAxisDir = glm::vec3(0.0f, 0.0f, 1.0f);
    
    if (m_gizmoMode == GizmoMode::Scale) {
        glm::mat4 rotMat = glm::mat4_cast(obj.quaternion);
        
        glm::vec3 localX = glm::vec3(rotMat * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        glm::vec3 localY = glm::vec3(rotMat * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        glm::vec3 localZ = glm::vec3(rotMat * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
        
        // Flip axes if they point in negative direction (same as rendering)
        float xSign = (localX.x >= 0.0f) ? 1.0f : -1.0f;
        float ySign = (localY.y >= 0.0f) ? 1.0f : -1.0f;
        float zSign = (localZ.z >= 0.0f) ? 1.0f : -1.0f;
        
        xAxisDir = localX * xSign;
        yAxisDir = localY * ySign;
        zAxisDir = localZ * zSign;
    }
    
    switch (m_activeGizmoAxis) {
        case GizmoAxis::X: axisDir = xAxisDir; break;
        case GizmoAxis::Y: axisDir = yAxisDir; break;
        case GizmoAxis::Z: axisDir = zAxisDir; break;
        case GizmoAxis::XY:
            isPlaneMode = true;
            planeNormal = glm::normalize(glm::cross(xAxisDir, yAxisDir));
            break;
        case GizmoAxis::XZ:
            isPlaneMode = true;
            planeNormal = glm::normalize(glm::cross(xAxisDir, zAxisDir));
            break;
        case GizmoAxis::YZ:
            isPlaneMode = true;
            planeNormal = glm::normalize(glm::cross(yAxisDir, zAxisDir));
            break;
        case GizmoAxis::Center:
            // Camera-relative movement
            break;
        default: return;
    }
    
    // Handle Center axis for camera-relative movement
    if (m_activeGizmoAxis == GizmoAxis::Center) {
        if (m_gizmoMode == GizmoMode::Translate) {
            glm::vec3 cameraForward = m_camera->getFront();
            glm::vec3 cameraPerpPlane = -cameraForward;
            
            float denom = glm::dot(rayWorld, cameraPerpPlane);
            if (abs(denom) > 0.0001f) {
                float t = glm::dot(m_gizmoStartPos - rayOrigin, cameraPerpPlane) / denom;
                glm::vec3 hitPoint = rayOrigin + rayWorld * t;
                glm::vec3 delta = hitPoint - m_gizmoInteractionStart;
                obj.position = m_gizmoStartPos + delta;
            }
        }
        else if (m_gizmoMode == GizmoMode::Scale) {
            // Uniform scaling - use scale factor (ratio) to preserve proportions
            glm::vec3 cameraForward = m_camera->getFront();
            glm::vec3 cameraPerpPlane = -cameraForward;
            
            float denom = glm::dot(rayWorld, cameraPerpPlane);
            if (abs(denom) > 0.0001f) {
                float t = glm::dot(m_gizmoStartPos - rayOrigin, cameraPerpPlane) / denom;
                glm::vec3 hitPoint = rayOrigin + rayWorld * t;
                
                // Calculate scale factor as ratio of distances
                float currentDist = glm::length(hitPoint - m_gizmoStartPos);
                float startDist = glm::length(m_gizmoInteractionStart - m_gizmoStartPos);
                
                // Avoid division by zero
                if (startDist > 0.001f) {
                    float scaleFactor = currentDist / startDist;
                    
                    // Apply uniform scale factor to all axes (preserves proportions)
                    obj.scale.x = glm::max(0.1f, m_gizmoStartScale.x * scaleFactor);
                    obj.scale.y = glm::max(0.1f, m_gizmoStartScale.y * scaleFactor);
                    obj.scale.z = glm::max(0.1f, m_gizmoStartScale.z * scaleFactor);
                }
            }
        }
        return;
    }
    
    if (m_gizmoMode == GizmoMode::Translate) {
        if (isPlaneMode) {
            // Plane translation - intersect ray with the constraint plane
            float denom = glm::dot(rayWorld, planeNormal);
            if (abs(denom) > 0.0001f) {
                float t = glm::dot(m_gizmoStartPos - rayOrigin, planeNormal) / denom;
                glm::vec3 hitPoint = rayOrigin + rayWorld * t;
                glm::vec3 delta = hitPoint - m_gizmoInteractionStart;
                obj.position = m_gizmoStartPos + delta;
            }
        } else {
            // Single axis translation - find closest point on axis line to ray
            // Line 1: gizmo axis (point: m_gizmoStartPos, direction: axisDir)
            // Line 2: mouse ray (point: rayOrigin, direction: rayWorld)
            
            glm::vec3 w0 = m_gizmoStartPos - rayOrigin;
            float a = glm::dot(axisDir, axisDir);
            float b = glm::dot(axisDir, rayWorld);
            float c = glm::dot(rayWorld, rayWorld);
            float d = glm::dot(axisDir, w0);
            float e = glm::dot(rayWorld, w0);
            
            float denom = a * c - b * b;
            if (abs(denom) > 0.0001f) {
                // Current t is the parameter along the axis where the closest point is now
                float currentT = (b * e - c * d) / denom;
                
                // The delta along the axis is the difference from where we started
                float axisDelta = currentT - m_gizmoStartAxisT;
                
                // Move object along the axis by the delta
                obj.position = m_gizmoStartPos + axisDir * axisDelta;
            }
        }
    }
    else if (m_gizmoMode == GizmoMode::Scale) {
        if (isPlaneMode) {
            float denom = glm::dot(rayWorld, planeNormal);
            if (abs(denom) > 0.0001f) {
                float t = glm::dot(m_gizmoStartPos - rayOrigin, planeNormal) / denom;
                glm::vec3 hitPoint = rayOrigin + rayWorld * t;
                glm::vec3 delta = hitPoint - m_gizmoInteractionStart;
                
                // Project delta onto local axis directions (not world space)
                float deltaX = glm::dot(delta, xAxisDir);
                float deltaY = glm::dot(delta, yAxisDir);
                float deltaZ = glm::dot(delta, zAxisDir);
                
                // Apply delta to each axis in the plane
                switch (m_activeGizmoAxis) {
                    case GizmoAxis::XY:
                        obj.scale.x = glm::max(0.1f, m_gizmoStartScale.x + deltaX);
                        obj.scale.y = glm::max(0.1f, m_gizmoStartScale.y + deltaY);
                        break;
                    case GizmoAxis::XZ:
                        obj.scale.x = glm::max(0.1f, m_gizmoStartScale.x + deltaX);
                        obj.scale.z = glm::max(0.1f, m_gizmoStartScale.z + deltaZ);
                        break;
                    case GizmoAxis::YZ:
                        obj.scale.y = glm::max(0.1f, m_gizmoStartScale.y + deltaY);
                        obj.scale.z = glm::max(0.1f, m_gizmoStartScale.z + deltaZ);
                        break;
                    default: break;
                }
            }
        } else {
            // Single axis scaling - the object's face should follow the cursor
            glm::vec3 w0 = m_gizmoStartPos - rayOrigin;
            float a = glm::dot(axisDir, axisDir);
            float b = glm::dot(axisDir, rayWorld);
            float c = glm::dot(rayWorld, rayWorld);
            float d = glm::dot(axisDir, w0);
            float e = glm::dot(rayWorld, w0);
            
            float denom = a * c - b * b;
            if (abs(denom) > 0.0001f) {
                float currentT = (b * e - c * d) / denom;
                
                // Movement delta = currentT - m_gizmoStartAxisT
                // Apply delta directly to scale for 1:1 cursor tracking
                
                float delta = currentT - m_gizmoStartAxisT;
                float newScale;
                
                switch (m_activeGizmoAxis) {
                    case GizmoAxis::X: 
                        newScale = m_gizmoStartScale.x + delta;
                        obj.scale.x = glm::max(0.1f, newScale);
                        break;
                    case GizmoAxis::Y: 
                        newScale = m_gizmoStartScale.y + delta;
                        obj.scale.y = glm::max(0.1f, newScale);
                        break;
                    case GizmoAxis::Z: 
                        newScale = m_gizmoStartScale.z + delta;
                        obj.scale.z = glm::max(0.1f, newScale);
                        break;
                    default: break;
                }
            }
        }
    }
    
    // Handle rotation separately with proper plane intersection
    if (m_gizmoMode == GizmoMode::Rotate) {
        // For rotation, intersect ray with the plane perpendicular to the rotation axis
        // Use world-aligned axis direction (not object-local)
        float denom = glm::dot(rayWorld, axisDir);
        if (abs(denom) > 0.0001f) {
            float t = glm::dot(obj.position - rayOrigin, axisDir) / denom;
            glm::vec3 hitPoint = rayOrigin + rayWorld * t;
            
            // Calculate rotation based on angular movement around the world-aligned axis
            // Project hit point onto the plane perpendicular to the rotation axis
            glm::vec3 toHit = hitPoint - obj.position;
            glm::vec3 toStart = m_gizmoInteractionStart - obj.position;
            
            // Remove component along rotation axis to get vectors in the rotation plane
            toHit = toHit - axisDir * glm::dot(toHit, axisDir);
            toStart = toStart - axisDir * glm::dot(toStart, axisDir);
            
            // Normalize to get direction vectors
            if (glm::length(toHit) > 0.001f && glm::length(toStart) > 0.001f) {
                toHit = glm::normalize(toHit);
                toStart = glm::normalize(toStart);
                
                // Build orthonormal basis in the rotation plane
                // Use toStart as the X axis, and the perpendicular as Y axis
                glm::vec3 xAxis = toStart;
                glm::vec3 yAxis = glm::normalize(glm::cross(axisDir, xAxis));
                
                // Project current direction onto this 2D coordinate system
                float x = glm::dot(toHit, xAxis);
                float y = glm::dot(toHit, yAxis);
                
                // Use atan2 to get the full angle from -π to π
                float angle = atan2(y, x);
                
                // Store accumulated angle for rotation application
                m_rotationAccumulatedAngle = angle;
                
                // Calculate current angle in world-aligned reference frame for feedback disc
                // Use the same reference vectors as in InputHandler
                glm::vec3 refX, refY;
                if (m_activeGizmoAxis == GizmoAxis::X) {
                    refX = glm::vec3(0.0f, 1.0f, 0.0f);
                    refY = glm::vec3(0.0f, 0.0f, 1.0f);
                } else if (m_activeGizmoAxis == GizmoAxis::Y) {
                    refX = glm::vec3(1.0f, 0.0f, 0.0f);
                    refY = glm::vec3(0.0f, 0.0f, 1.0f);
                } else { // Z axis
                    refX = glm::vec3(1.0f, 0.0f, 0.0f);
                    refY = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                float curX = glm::dot(toHit, refX);
                float curY = glm::dot(toHit, refY);
                m_rotationCurrentAngle = atan2(curY, curX);
                
                // Determine rotation direction based on angle sign
                m_rotationIsPositive = (angle >= 0);
                
                // Apply world-space rotation using quaternions
                // Start from the original quaternion rotation
                glm::quat startQuat = m_gizmoStartQuaternion;
                
                // Create rotation quaternion for the world-space axis rotation
                glm::quat deltaQuat = glm::angleAxis(angle, axisDir);
                
                // Apply delta rotation in world space (multiply on the left)
                obj.quaternion = deltaQuat * startQuat;
                
                // Sync Euler angles for UI display
                syncEulerFromQuaternion(obj);
            }
        }
    }
}

void Engine::renderWorldAxes() {
    // TODO: Reimplement with Vulkan - draw world axes at origin
    return;
}

void Engine::renderRotationFeedbackDisc() {
    // TODO: Reimplement with Vulkan - draw rotation feedback indicator
    return;
}


} // namespace Forge
