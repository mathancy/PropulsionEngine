#include "Engine.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#undef max  // Prevent Windows.h max macro from conflicting with std::max
#undef min  // Prevent Windows.h min macro from conflicting with std::min
#endif

namespace Propulsion {

#ifdef _WIN32
// Forward declarations for file dialog functions (defined in Engine.cpp)
extern std::string showSaveFolderDialog(GLFWwindow* window, const std::string& suggestedName);
extern std::string showOpenFileDialog(GLFWwindow* window);
#endif

void Engine::processInput(float deltaTime) {
    GLFWwindow* window = m_window->getNativeWindow();
    
    // Escape ends Play mode. It never closes the editor.
    static bool escapeWasPressed = false;
    bool escapePressed = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (escapePressed && !escapeWasPressed && m_isPlaying)
        stopPlay();
    escapeWasPressed = escapePressed;
    
    // Undo/Redo shortcuts
    bool ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || 
                       glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    bool shiftPressed = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || 
                        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    
    static bool zKeyWasPressed = false;
    static bool yKeyWasPressed = false;
    static bool nKeyWasPressed = false;
    static bool oKeyWasPressed = false;
    static bool sKeyWasPressed = false;
    static bool f3KeyWasPressed = false;
    static bool f4KeyWasPressed = false;

    bool zKeyPressed = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
    bool yKeyPressed = glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS;
    bool nKeyPressed = glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS;
    bool oKeyPressed = glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS;
    bool sKeyPressed = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    bool f3KeyPressed = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
    bool f4KeyPressed = glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS;

    // Undo (Ctrl+Z) and redo (Ctrl+Y, or Ctrl+Shift+Z). Not while typing in a text field, which has its own.
    const bool typingText = ImGui::GetIO().WantTextInput;
    if (ctrlPressed && zKeyPressed && !zKeyWasPressed && !typingText) {
        if (shiftPressed) redo();
        else undo();
    }
    zKeyWasPressed = zKeyPressed;

    if (ctrlPressed && yKeyPressed && !yKeyWasPressed && !typingText) {
        redo();
    }
    yKeyWasPressed = yKeyPressed;
    
    // New Project
    if (ctrlPressed && nKeyPressed && !nKeyWasPressed) {
        newProject();
    }
    nKeyWasPressed = nKeyPressed;
    
    // Note: Ctrl+O removed - use File menu to open projects
    // Note: Ctrl+S is used for asset saving in AssetEditor tabs
    
    // Save All (Ctrl+Shift+S) - Save entire project
    if (ctrlPressed && shiftPressed && sKeyPressed && !sKeyWasPressed) {
        if (!m_currentProjectPath.empty()) {
            // Save to existing location
            saveProject(m_currentProjectPath);
        } else {
            // Show project name dialog
            m_showProjectNameDialog = true;
            m_isNewProject = true;
            strcpy_s(m_projectNameBuffer, "NewProject");
        }
    }
    sKeyWasPressed = sKeyPressed;
    
    // Toggle FPS counter
    if (f3KeyPressed && !f3KeyWasPressed) {
        m_showFPS = !m_showFPS;
    }
    f3KeyWasPressed = f3KeyPressed;

    // Toggle the collision shape overlay
    if (f4KeyPressed && !f4KeyWasPressed) {
        m_showCollisionShapes = !m_showCollisionShapes;
    }
    f4KeyWasPressed = f4KeyPressed;
    
    // Delete removes the selected object (when the viewport is focused, and not while typing or playing)
    {
        static bool deleteWasPressed = false;
        bool deletePressed = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
        if (deletePressed && !deleteWasPressed && m_selectedObjectIndex >= 0 && m_viewportFocused &&
            !m_isPlaying && !ImGui::GetIO().WantTextInput) {
            deleteObject(m_selectedObjectIndex);
        }
        deleteWasPressed = deletePressed;
    }
    
    // Ctrl+D duplicates the selected object (when the viewport is focused, and not while typing or playing)
    {
        static bool dWasPressed = false;
        bool dPressed = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
        if (ctrlPressed && dPressed && !dWasPressed && m_selectedObjectIndex >= 0 && m_viewportFocused &&
            !m_isPlaying && !m_rightMousePressed && !ImGui::GetIO().WantTextInput) {
            duplicateObject(m_selectedObjectIndex);
        }
        dWasPressed = dPressed;
    }

    // Gizmo tool shortcuts (only when object is selected and viewport is focused)
    // (not while playing: W/E/R/etc. belong to the game then)
    if (m_selectedObjectIndex >= 0 && m_viewportFocused && !m_isPlaying) {
        static bool wKeyWasPressed = false;
        static bool eKeyWasPressed = false;
        static bool rKeyWasPressed = false;
        
        bool wKeyPressed = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
        bool eKeyPressed = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        bool rKeyPressed = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        
        // Toggle Move tool with W (only if not moving camera)
        if (wKeyPressed && !wKeyWasPressed && !m_rightMousePressed) {
            m_gizmoMode = (m_gizmoMode == GizmoMode::Translate) ? GizmoMode::None : GizmoMode::Translate;
        }
        wKeyWasPressed = wKeyPressed;
        
        // Toggle Rotate tool with E (only if not moving camera)
        if (eKeyPressed && !eKeyWasPressed && !m_rightMousePressed) {
            m_gizmoMode = (m_gizmoMode == GizmoMode::Rotate) ? GizmoMode::None : GizmoMode::Rotate;
        }
        eKeyWasPressed = eKeyPressed;
        
        // Toggle Scale tool with R
        if (rKeyPressed && !rKeyWasPressed) {
            m_gizmoMode = (m_gizmoMode == GizmoMode::Scale) ? GizmoMode::None : GizmoMode::Scale;
        }
        rKeyWasPressed = rKeyPressed;
    }
    
    // Camera movement (only when right mouse is pressed)
    if (m_rightMousePressed) {
        float speedMultiplier = 1.0f;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            speedMultiplier = 2.5f;
        
        m_camera->setMovementSpeed(5.0f * speedMultiplier);
        
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            m_camera->processKeyboard(CameraMovement::FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            m_camera->processKeyboard(CameraMovement::BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            m_camera->processKeyboard(CameraMovement::LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            m_camera->processKeyboard(CameraMovement::RIGHT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            m_camera->processKeyboard(CameraMovement::UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            m_camera->processKeyboard(CameraMovement::DOWN, deltaTime);
    }
}

void Engine::onMouseButton(int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            // Only activate camera control if we're hovering over the viewport
            // and we're on the Scene tab
            // (no editor fly camera while playing)
            if (m_viewportHovered && m_activeTabIndex == 0 && !m_isPlaying) {
                m_rightMousePressed = true;
                glfwSetInputMode(m_window->getNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                m_firstMouse = true;
            }
        } else if (action == GLFW_RELEASE) {
            m_rightMousePressed = false;
            glfwSetInputMode(m_window->getNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
}

void Engine::onMouseMove(double xpos, double ypos) {
    // Only process camera movement if right mouse is pressed
    // (cursor is captured, so we know we're in camera control mode)
    if (m_rightMousePressed) {
        if (m_firstMouse) {
            m_lastX = static_cast<float>(xpos);
            m_lastY = static_cast<float>(ypos);
            m_firstMouse = false;
        }
        
        float xoffset = static_cast<float>(xpos) - m_lastX;
        float yoffset = m_lastY - static_cast<float>(ypos);
        
        m_lastX = static_cast<float>(xpos);
        m_lastY = static_cast<float>(ypos);
        
        m_camera->processMouseMovement(xoffset, yoffset);
    }
}

void Engine::checkViewportClick() {
    // Get mouse position relative to viewport
    ImVec2 mousePos = ImGui::GetMousePos();
    float mouseX = mousePos.x - m_viewportPos.x;
    float mouseY = mousePos.y - m_viewportPos.y;
    
    // Normalize to NDC (-1 to 1)
    float ndcX = (2.0f * mouseX) / m_viewportSize.x - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY) / m_viewportSize.y; // Flip Y
    
    // Create ray in clip space
    glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
    
    // Transform to view space - use unflipped projection for ray casting
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 projection = m_camera->getProjectionMatrixForRayCast(aspectRatio);
    glm::vec4 rayEye = glm::inverse(projection) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    
    // Transform to world space
    glm::mat4 view = m_camera->getViewMatrix();
    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    rayWorld = glm::normalize(rayWorld);
    
    // Ray origin is camera position
    glm::vec3 rayOrigin = m_camera->getPosition();
    
    // Check if we clicked on a gizmo first (if object is selected and gizmo is active)
    if (m_selectedObjectIndex >= 0 && m_gizmoMode != GizmoMode::None) {
        GizmoAxis hitAxis;
        if (rayCastGizmo(rayOrigin, rayWorld, hitAxis)) {
            // Push undo state before starting gizmo drag
            pushUndoState();
            
            // Start gizmo drag
            m_gizmoActive = true;
            m_activeGizmoAxis = hitAxis;
            m_gizmoStartPos = m_sceneObjects[m_selectedObjectIndex].position;
            m_gizmoStartScale = m_sceneObjects[m_selectedObjectIndex].scale;
            m_gizmoStartQuaternion = m_sceneObjects[m_selectedObjectIndex].quaternion;
            m_rotationAccumulatedAngle = 0.0f;
            
            // Start rotation logging if in rotate mode
            if (m_gizmoMode == GizmoMode::Rotate) {
                startRotationLog();
            }
            
            // Calculate the interaction start point based on axis type
            glm::vec3 gizmoPos = m_sceneObjects[m_selectedObjectIndex].position;
            glm::vec3 axisDir(0.0f);
            glm::vec3 planeNormal(0.0f);
            bool isPlaneMode = false;
            
            // For Scale mode, use rotated axis directions (same as rendering/hit detection)
            glm::vec3 xAxisDir = glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 yAxisDir = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 zAxisDir = glm::vec3(0.0f, 0.0f, 1.0f);
            
            if (m_gizmoMode == GizmoMode::Scale) {
                glm::mat4 rotMat = glm::mat4_cast(m_sceneObjects[m_selectedObjectIndex].quaternion);
                
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
            
            switch (hitAxis) {
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
                    // Camera-facing plane
                    planeNormal = -m_camera->getFront();
                    isPlaneMode = true;
                    break;
                default: break;
            }
            
            m_gizmoStartAxisT = 0.0f; // Default
            
            if (isPlaneMode || m_gizmoMode == GizmoMode::Rotate) {
                // For plane modes and rotation, intersect with plane
                if (m_gizmoMode == GizmoMode::Rotate && !isPlaneMode) {
                    planeNormal = axisDir; // Rotation uses axis as plane normal
                }
                float denom = glm::dot(rayWorld, planeNormal);
                if (abs(denom) > 0.0001f) {
                    float t = glm::dot(gizmoPos - rayOrigin, planeNormal) / denom;
                    m_gizmoInteractionStart = rayOrigin + rayWorld * t;
                    
                    // For rotation mode, calculate the initial angle on the rotation plane
                    if (m_gizmoMode == GizmoMode::Rotate) {
                        glm::vec3 toStart = m_gizmoInteractionStart - gizmoPos;
                        // Remove component along rotation axis
                        toStart = toStart - axisDir * glm::dot(toStart, axisDir);
                        if (glm::length(toStart) > 0.001f) {
                            toStart = glm::normalize(toStart);
                            // The disc mesh is in the XZ plane (local X = 0° angle, rotates around local Y)
                            // After each ring's transformation, we need to know where local X and Z end up
                            // Then calculate the angle relative to those transformed axes
                            glm::vec3 refX, refY;
                            if (m_activeGizmoAxis == GizmoAxis::X) {
                                // Disc rotated 90° around Z: local X→world Y, local Z→world Z
                                refX = glm::vec3(0.0f, 1.0f, 0.0f);
                                refY = glm::vec3(0.0f, 0.0f, -1.0f); // Negative because rotation flips handedness
                            } else if (m_activeGizmoAxis == GizmoAxis::Y) {
                                // No rotation: local X = world X, local Z = world Z
                                refX = glm::vec3(1.0f, 0.0f, 0.0f);
                                refY = glm::vec3(0.0f, 0.0f, -1.0f); // Negative for consistent winding
                            } else { // Z axis
                                // Disc rotated 90° around X: local X→world X, local Z→world Y
                                refX = glm::vec3(1.0f, 0.0f, 0.0f);
                                refY = glm::vec3(0.0f, 1.0f, 0.0f);
                            }
                            float x = glm::dot(toStart, refX);
                            float y = glm::dot(toStart, refY);
                            m_rotationStartAngle = atan2(y, x);
                            m_rotationCurrentAngle = m_rotationStartAngle;
                            m_rotationIsPositive = true;
                        }
                    }
                } else {
                    m_gizmoInteractionStart = gizmoPos;
                }
            } else {
                // For single-axis translation/scale, calculate initial t-parameter
                // Find closest point on axis line to the initial ray
                glm::vec3 w0 = gizmoPos - rayOrigin;
                float a = glm::dot(axisDir, axisDir);
                float b = glm::dot(axisDir, rayWorld);
                float c = glm::dot(rayWorld, rayWorld);
                float d = glm::dot(axisDir, w0);
                float e = glm::dot(rayWorld, w0);
                
                float denom = a * c - b * b;
                if (abs(denom) > 0.0001f) {
                    // Store the initial t value - this is where along the axis the user clicked
                    m_gizmoStartAxisT = (b * e - c * d) / denom;
                }
                m_gizmoInteractionStart = gizmoPos;
            }
            
            m_dragStartMouseWorld = rayOrigin + rayWorld * glm::length(m_gizmoStartPos - rayOrigin);
            return; // Don't check object selection
        }
    }
    
    // Test intersection with scene objects
    float closestDistance = FLT_MAX;
    int closestObject = -1;
    
    for (size_t i = 0; i < m_sceneObjects.size(); i++) {
        float distance;
        bool hit = rayCastObject(rayOrigin, rayWorld, m_sceneObjects[i], distance);
        
        if (hit && distance < closestDistance) {
            closestDistance = distance;
            closestObject = static_cast<int>(i);
        }
    }
    
    // Update selection
    if (closestObject != -1) {
        m_selectedObjectIndex = closestObject;
        char debugMsg[256];
        snprintf(debugMsg, sizeof(debugMsg), "Selected: %s", m_sceneObjects[closestObject].name.c_str());
        OutputLog::verbose(LogCategory::Editor, debugMsg);
    } else {
        m_selectedObjectIndex = -1;
        m_gizmoMode = GizmoMode::None; // Deselect also resets gizmo
    }
}

bool Engine::rayCastObject(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const SceneObject& obj, float& distance) {
    // Get the mesh for this object to get its actual bounds
    // An object with no mesh has nothing to see, but it still has to be clickable (a camera on its own, say), so it
    // is given a small box around its origin.
    Mesh* mesh = getMeshForObject(obj);
    glm::vec3 localMin(-0.5f), localMax(0.5f);
    if (mesh) {
        localMin = mesh->getBoundsMin();
        localMax = mesh->getBoundsMax();
    }
    
    // Get the mesh's local bounds
    
    // Build the object's model matrix (same as in rendering)
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, obj.position);
    model = model * glm::mat4_cast(obj.quaternion);
    model = glm::scale(model, obj.scale);
    
    // Transform ray to object's local space by using inverse model matrix
    glm::mat4 invModel = glm::inverse(model);
    
    // Transform ray origin to local space
    glm::vec3 localRayOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
    
    // Transform ray direction to local space (no translation, just rotation/scale)
    glm::vec3 localRayDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));
    
    // Now do AABB intersection in local space
    float tmin = 0.0f;
    float tmax = FLT_MAX;
    
    for (int i = 0; i < 3; i++) {
        if (std::abs(localRayDir[i]) < 0.0001f) {
            // Ray is parallel to slab
            if (localRayOrigin[i] < localMin[i] || localRayOrigin[i] > localMax[i]) {
                return false;
            }
        } else {
            float t1 = (localMin[i] - localRayOrigin[i]) / localRayDir[i];
            float t2 = (localMax[i] - localRayOrigin[i]) / localRayDir[i];
            
            if (t1 > t2) std::swap(t1, t2);
            
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            
            if (tmin > tmax) {
                return false;
            }
        }
    }
    
    // Calculate the world-space intersection point and distance
    if (tmin >= 0.0f) {
        // Get intersection point in local space
        glm::vec3 localHitPoint = localRayOrigin + localRayDir * tmin;
        // Transform back to world space
        glm::vec3 worldHitPoint = glm::vec3(model * glm::vec4(localHitPoint, 1.0f));
        // Distance is from camera to hit point
        distance = glm::length(worldHitPoint - rayOrigin);
        return true;
    }
    
    
    return false;
}

Mesh* Engine::getMeshForObject(const SceneObject& obj) const {
    // The mesh an object is drawn with, or null if it has none: an object with no mesh set draws nothing.
    if (!obj.meshGUID.empty()) {
        // meshByGUID is null if that mesh isn't in the project (any more).
        return meshByGUID(obj.meshGUID);
    }

    // No mesh is set. An object placed from an Object asset follows the asset, so no mesh there means none here.
    if (sourceAssetOf(obj)) return nullptr;

    // An object that isn't tied to an asset (the scene's starting cube, or a project from before Object assets)
    // keeps the older rules: its mesh index, then its name, then a cube.
    if (obj.meshIndex >= 0 && obj.meshIndex < static_cast<int>(m_contentItems.size())) {
        const auto& meshItem = m_contentItems[obj.meshIndex];
        if (meshItem.name == "Sphere") return m_sphere.get();
        if (meshItem.name == "Cylinder") return m_cylinder.get();
    } else {
        if (obj.name.find("Sphere") != std::string::npos) return m_sphere.get();
        if (obj.name.find("Cylinder") != std::string::npos) return m_cylinder.get();
    }
    return m_cube.get();
}

} // namespace Propulsion