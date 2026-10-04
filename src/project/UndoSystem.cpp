// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Engine.h"
#include "nodegraph/NodeGraph.h"
#include "scripting/ScriptGraphEditor.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Forge {

void Engine::pushUndoState() {
    if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
        SceneObject& obj = m_sceneObjects[m_selectedObjectIndex];
        m_undoStack.emplace_back(m_selectedObjectIndex, obj.position, obj.rotation, obj.quaternion, obj.scale);
        
        // Clear redo stack when new action is performed
        m_redoStack.clear();
        
        // Limit undo stack size to prevent excessive memory usage
        const size_t maxUndoSteps = 100;
        if (m_undoStack.size() > maxUndoSteps) {
            m_undoStack.erase(m_undoStack.begin());
        }
    }
}

// A node graph tab (a material or a script) has its own undo history; Ctrl+Z / Ctrl+Y in one of those
// tabs act on its graph, not on the objects in the scene.
bool Engine::graphTabUndo(bool redo) {
    if (m_activeTabIndex < 0 || m_activeTabIndex >= static_cast<int>(m_tabs.size())) return false;
    Tab& tab = m_tabs[m_activeTabIndex];
    if (tab.type == TabType::ScriptGraph && tab.scriptGraph) {
        redo ? tab.scriptGraph->redo() : tab.scriptGraph->undo();
        return true;
    }
    if (tab.type == TabType::AssetEditor && tab.nodeGraph) {
        redo ? tab.nodeGraph->redo() : tab.nodeGraph->undo();
        return true;
    }
    return false;
}

void Engine::undo() {
    if (graphTabUndo(false)) return;
    if (m_undoStack.empty()) return;
    
    // Get the last undo state
    UndoState undoState = m_undoStack.back();
    m_undoStack.pop_back();
    
    // Check if object still exists
    if (undoState.objectIndex >= 0 && undoState.objectIndex < static_cast<int>(m_sceneObjects.size())) {
        SceneObject& obj = m_sceneObjects[undoState.objectIndex];
        
        // Save current state to redo stack
        m_redoStack.emplace_back(undoState.objectIndex, obj.position, obj.rotation, obj.quaternion, obj.scale);
        
        // Restore previous state
        obj.position = undoState.position;
        obj.rotation = undoState.rotation;
        obj.quaternion = undoState.quaternion;
        obj.scale = undoState.scale;
        
        // Mark project as modified
        markProjectModified();
    }
}

void Engine::redo() {
    if (graphTabUndo(true)) return;
    if (m_redoStack.empty()) return;
    
    // Get the last redo state
    UndoState redoState = m_redoStack.back();
    m_redoStack.pop_back();
    
    // Check if object still exists
    if (redoState.objectIndex >= 0 && redoState.objectIndex < static_cast<int>(m_sceneObjects.size())) {
        SceneObject& obj = m_sceneObjects[redoState.objectIndex];
        
        // Save current state to undo stack
        m_undoStack.emplace_back(redoState.objectIndex, obj.position, obj.rotation, obj.quaternion, obj.scale);
        
        // Restore redo state
        obj.position = redoState.position;
        obj.rotation = redoState.rotation;
        obj.quaternion = redoState.quaternion;
        obj.scale = redoState.scale;
        
        // Mark project as modified
        markProjectModified();
    }
}

void Engine::startRotationLog() {
    if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) return;
    
    // Create Logs directory if it doesn't exist
    std::filesystem::path logsDir = "Logs";
    std::filesystem::create_directories(logsDir);
    
    // Generate timestamped filename
    auto now = std::time(nullptr);
    auto localTime = *std::localtime(&now);
    std::stringstream filename;
    filename << "Logs/rotation_" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".log";
    
    // Open log file
    m_rotationLogFile.open(filename.str());
    if (!m_rotationLogFile.is_open()) return;
    
    m_isLoggingRotation = true;
    m_rotationLogFrameCount = 0;
    
    const SceneObject& obj = m_sceneObjects[m_selectedObjectIndex];
    const char* axisNames[] = { "None", "X", "Y", "Z" };
    
    // Write header
    m_rotationLogFile << "=== FORGE ENGINE ROTATION LOG ===\n";
    m_rotationLogFile << "Timestamp: " << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << "\n";
    m_rotationLogFile << "Object: " << obj.name << "\n";
    m_rotationLogFile << "Active Axis: " << axisNames[static_cast<int>(m_activeGizmoAxis)] << "\n";
    m_rotationLogFile << "Initial Rotation (deg): X=" << std::fixed << std::setprecision(3) 
                      << obj.rotation.x << ", Y=" << obj.rotation.y << ", Z=" << obj.rotation.z << "\n";
    m_rotationLogFile << "\n=== FRAME DATA ===\n";
    m_rotationLogFile << "Frame | Rotation X | Rotation Y | Rotation Z | Accumulated Angle (rad) | Accumulated Angle (deg)\n";
    m_rotationLogFile << "------+------------+------------+------------+-------------------------+------------------------\n";
}

void Engine::logRotationFrame() {
    if (!m_isLoggingRotation || !m_rotationLogFile.is_open()) return;
    if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) return;
    
    const SceneObject& obj = m_sceneObjects[m_selectedObjectIndex];
    m_rotationLogFrameCount++;
    
    // Log frame data
    m_rotationLogFile << std::setw(5) << m_rotationLogFrameCount << " | "
                      << std::fixed << std::setprecision(3) << std::setw(10) << obj.rotation.x << " | "
                      << std::setw(10) << obj.rotation.y << " | "
                      << std::setw(10) << obj.rotation.z << " | "
                      << std::setw(23) << m_rotationAccumulatedAngle << " | "
                      << std::setw(22) << (m_rotationAccumulatedAngle * 180.0f / 3.14159265359f) << "\n";
}

void Engine::endRotationLog() {
    if (!m_isLoggingRotation || !m_rotationLogFile.is_open()) return;
    
    m_rotationLogFile << "\n=== SUMMARY ===\n";
    if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
        const SceneObject& obj = m_sceneObjects[m_selectedObjectIndex];
        m_rotationLogFile << "Final Rotation (deg): X=" << std::fixed << std::setprecision(3)
                          << obj.rotation.x << ", Y=" << obj.rotation.y << ", Z=" << obj.rotation.z << "\n";
    }
    m_rotationLogFile << "Total Frames: " << m_rotationLogFrameCount << "\n";
    m_rotationLogFile << "Total Angle Rotated: " << std::fixed << std::setprecision(3) 
                      << (m_rotationAccumulatedAngle * 180.0f / 3.14159265359f) << " degrees\n";
    
    m_rotationLogFile.close();
    m_isLoggingRotation = false;
    m_rotationLogFrameCount = 0;
}

void Engine::syncEulerFromQuaternion(SceneObject& obj) {
    // Convert quaternion to rotation matrix
    glm::mat4 rotMatrix = glm::mat4_cast(obj.quaternion);
    
    // Extract Euler angles using custom method to avoid gimbal lock
    float sy = sqrt(rotMatrix[0][0] * rotMatrix[0][0] + rotMatrix[1][0] * rotMatrix[1][0]);
    bool singular = sy < 1e-6f;
    
    float x_rot, y_rot, z_rot;
    if (!singular) {
        x_rot = atan2(rotMatrix[2][1], rotMatrix[2][2]);
        y_rot = atan2(-rotMatrix[2][0], sy);
        z_rot = atan2(rotMatrix[1][0], rotMatrix[0][0]);
    } else {
        x_rot = atan2(-rotMatrix[1][2], rotMatrix[1][1]);
        y_rot = atan2(-rotMatrix[2][0], sy);
        z_rot = 0;
    }
    
    obj.rotation.x = glm::degrees(x_rot);
    obj.rotation.y = glm::degrees(y_rot);
    obj.rotation.z = glm::degrees(z_rot);
}

void Engine::syncQuaternionFromEuler(SceneObject& obj) {
    // Convert Euler angles (in degrees) to quaternion
    obj.quaternion = glm::quat(glm::vec3(
        glm::radians(obj.rotation.x),
        glm::radians(obj.rotation.y),
        glm::radians(obj.rotation.z)
    ));
}

} // namespace Forge
