#include <stdexcept>
#include "project/ProjectSerializer.h"
#include "Engine.h"
#include "ui/TabSystem.h"
#include "ui/ContentItem.h"
#include "core/Camera.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace Propulsion {

// Helper write functions
void ProjectSerializer::writeVec3(std::ofstream& file, const glm::vec3& vec) {
    file.write(reinterpret_cast<const char*>(&vec.x), sizeof(float));
    file.write(reinterpret_cast<const char*>(&vec.y), sizeof(float));
    file.write(reinterpret_cast<const char*>(&vec.z), sizeof(float));
}

void ProjectSerializer::writeQuat(std::ofstream& file, const glm::quat& quat) {
    file.write(reinterpret_cast<const char*>(&quat.w), sizeof(float));
    file.write(reinterpret_cast<const char*>(&quat.x), sizeof(float));
    file.write(reinterpret_cast<const char*>(&quat.y), sizeof(float));
    file.write(reinterpret_cast<const char*>(&quat.z), sizeof(float));
}

void ProjectSerializer::writeString(std::ofstream& file, const std::string& str) {
    size_t length = str.length();
    file.write(reinterpret_cast<const char*>(&length), sizeof(size_t));
    file.write(str.c_str(), length);
}

void ProjectSerializer::writeInt(std::ofstream& file, int value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(int));
}

void ProjectSerializer::writeFloat(std::ofstream& file, float value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(float));
}

void ProjectSerializer::writeBool(std::ofstream& file, bool value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(bool));
}

void ProjectSerializer::writeTime(std::ofstream& file, time_t time) {
    file.write(reinterpret_cast<const char*>(&time), sizeof(time_t));
}

// Helper read functions
glm::vec3 ProjectSerializer::readVec3(std::ifstream& file) {
    glm::vec3 vec;
    file.read(reinterpret_cast<char*>(&vec.x), sizeof(float));
    file.read(reinterpret_cast<char*>(&vec.y), sizeof(float));
    file.read(reinterpret_cast<char*>(&vec.z), sizeof(float));
    return vec;
}

glm::quat ProjectSerializer::readQuat(std::ifstream& file) {
    glm::quat quat;
    file.read(reinterpret_cast<char*>(&quat.w), sizeof(float));
    file.read(reinterpret_cast<char*>(&quat.x), sizeof(float));
    file.read(reinterpret_cast<char*>(&quat.y), sizeof(float));
    file.read(reinterpret_cast<char*>(&quat.z), sizeof(float));
    return quat;
}

std::string ProjectSerializer::readString(std::ifstream& file) {
    size_t length = 0;
    file.read(reinterpret_cast<char*>(&length), sizeof(size_t));
    if (!file || length > (size_t(1) << 24)) {
        // Not a string we wrote: the file is damaged or from a format this build doesn't know.
        throw std::runtime_error("the project file is damaged or in an unknown format");
    }
    std::string str(length, '\0');
    file.read(&str[0], length);
    return str;
}

int ProjectSerializer::readInt(std::ifstream& file) {
    int value;
    file.read(reinterpret_cast<char*>(&value), sizeof(int));
    return value;
}

float ProjectSerializer::readFloat(std::ifstream& file) {
    float value;
    file.read(reinterpret_cast<char*>(&value), sizeof(float));
    return value;
}

bool ProjectSerializer::readBool(std::ifstream& file) {
    bool value;
    file.read(reinterpret_cast<char*>(&value), sizeof(bool));
    return value;
}

time_t ProjectSerializer::readTime(std::ifstream& file) {
    time_t time;
    file.read(reinterpret_cast<char*>(&time), sizeof(time_t));
    return time;
}

bool ProjectSerializer::saveProject(const std::string& filepath, const ProjectData& data) {
    // filepath is the full path to the .pproj file (e.g., C:\Projects\MyProject\MyProject.pproj)
    std::filesystem::path propulsionFile(filepath);
    std::filesystem::path projectDir = propulsionFile.parent_path();
    std::string projectName = projectDir.filename().string();
    
    // Create project directory and subdirectories
    try {
        std::filesystem::create_directories(projectDir);
        std::filesystem::create_directories(projectDir / "Assets");
        std::filesystem::create_directories(projectDir / "Assets" / "Meshes");
        std::filesystem::create_directories(projectDir / "Assets" / "Textures");
        std::filesystem::create_directories(projectDir / "Assets" / "Materials");
        std::filesystem::create_directories(projectDir / "Assets" / "Objects");
        std::filesystem::create_directories(projectDir / "Assets" / "Input");
        
        // Copy default mesh files to Assets/Meshes
        std::filesystem::path sourceMeshDir = "resources/default_meshes";
        std::filesystem::path targetMeshDir = projectDir / "Assets" / "Meshes";
        
        std::vector<std::string> meshFiles = {"Cube.fbx", "Sphere.fbx", "Cylinder.fbx"};
        for (const auto& meshFile : meshFiles) {
            std::filesystem::path sourceFile = sourceMeshDir / meshFile;
            std::filesystem::path targetFile = targetMeshDir / meshFile;
            
            if (std::filesystem::exists(sourceFile)) {
                std::filesystem::copy_file(sourceFile, targetFile, std::filesystem::copy_options::overwrite_existing);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        OutputLog::print(LogCategory::Project, LogLevel::Error, "Failed to create project directories: ", e.what());
        return false;
    }
    
    // Save the .pproj file
    std::ofstream file(propulsionFile, std::ios::binary);
    if (!file.is_open()) {
        OutputLog::print(LogCategory::Project, LogLevel::Error, "Failed to open file for writing: ", propulsionFile);
        return false;
    }
    
    // Write file header and version
    const char* header = "PROPU";
    file.write(header, 5);
    // Version 2 adds each scene object's mesh and material GUIDs, version 3 whether each is overridden
    // on the instance (otherwise it follows its Object asset), version 4 which asset each asset-editor tab is
    // showing (its GUID), so reopening a project shows the same assets and never opens one twice, version 5 the scene's
    // environment (its HDRI, intensity, rotation and whether the sky is drawn), version 6 its shadow and ambient occlusion
    // settings, version 7 how the viewport is drawn (Raster, Hybrid RT or Path Tracer, and the rays they spend). Older files still load.
    int version = 7;
    writeInt(file, version);
    
    // Write window state
    writeInt(file, data.windowWidth);
    writeInt(file, data.windowHeight);
    writeBool(file, data.windowMaximized);
    
    // Write camera state
    writeVec3(file, data.cameraPosition);
    writeVec3(file, data.cameraFront);
    writeVec3(file, data.cameraUp);
    writeFloat(file, data.cameraYaw);
    writeFloat(file, data.cameraPitch);
    
    // Write scene objects
    writeInt(file, static_cast<int>(data.sceneObjects.size()));
    for (const auto& obj : data.sceneObjects) {
        writeString(file, obj.name);
        writeVec3(file, obj.position);
        writeVec3(file, obj.rotation);
        writeQuat(file, obj.quaternion);
        writeVec3(file, obj.scale);
        writeVec3(file, obj.color);
        writeBool(file, obj.visible);
        writeInt(file, obj.sourceObjectIndex);
        writeInt(file, obj.meshIndex);
        writeInt(file, obj.materialIndex);
        writeString(file, obj.sourceObjectGUID);  // the Object asset this was placed from (its script runs in Play)
        writeString(file, obj.meshGUID);          // what it looks like: indices change when assets are rescanned, GUIDs don't
        writeString(file, obj.materialGUID);
        writeBool(file, obj.overridesMesh);
        writeBool(file, obj.overridesMaterial);
    }
    writeInt(file, data.selectedObjectIndex);
    
    // Note: Content items and folders are no longer serialized
    // They are now dynamically scanned from the filesystem
    
    // Write tabs
    writeInt(file, static_cast<int>(data.tabs.size()));
    for (const auto& tab : data.tabs) {
        writeString(file, tab.name);
        writeInt(file, static_cast<int>(tab.type));
        writeInt(file, tab.contentItemIndex);
        writeInt(file, tab.viewportWidth);
        writeInt(file, tab.viewportHeight);
        writeFloat(file, tab.cameraDistance);
        writeFloat(file, tab.cameraYaw);
        writeFloat(file, tab.cameraPitch);
        writeBool(file, tab.hasUnsavedChanges);
        writeString(file, tab.assetId);
    }
    writeInt(file, data.activeTabIndex);
    
    // Write UI state
    writeBool(file, data.showObjectsPanel);
    writeBool(file, data.showContentBrowser);

    // Write the environment (version 5)
    writeString(file, data.environment.hdriPath);
    writeFloat(file, data.environment.intensity);
    writeFloat(file, data.environment.rotationDegrees);
    writeBool(file, data.environment.showBackground);

    // Write the shadow and ambient occlusion settings (version 6)
    writeBool(file, data.environment.shadows);
    writeFloat(file, data.environment.shadowDistance);
    writeBool(file, data.environment.ambientOcclusion);
    writeFloat(file, data.environment.aoRadius);
    writeFloat(file, data.environment.aoStrength);

    // Write how the viewport is drawn (version 7)
    writeInt(file, static_cast<int>(data.environment.renderMode));
    writeInt(file, data.environment.rtShadowRays);
    writeInt(file, data.environment.rtAoRays);
    
    file.close();
    OutputLog::print(LogCategory::Project, LogLevel::Info, "Project saved successfully to: ", projectDir);
    OutputLog::print(LogCategory::Project, LogLevel::Info, "Project file: ", propulsionFile);
    return true;
}

bool ProjectSerializer::loadProject(const std::string& filepath, ProjectData& data) {
    try {
        return loadProjectFile(filepath, data);
    } catch (const std::exception& e) {
        OutputLog::print(LogCategory::Project, LogLevel::Error, "Failed to load project: ", e.what());
        return false;
    }
}

bool ProjectSerializer::loadProjectFile(const std::string& filepath, ProjectData& data) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        OutputLog::print(LogCategory::Project, LogLevel::Error, "Failed to open file for reading: ", filepath);
        return false;
    }
    
    // Read and verify header
    char header[6] = {0};
    file.read(header, 5);
    if (std::string(header) != "PROPU" && std::string(header) != "FORGE") {  // "FORGE" is what files from before the rename start with
        OutputLog::print(LogCategory::Project, LogLevel::Error, "Invalid project file format");
        return false;
    }
    
    int version = readInt(file);
    if (version < 1 || version > 7) {
        OutputLog::print(LogCategory::Project, LogLevel::Error, "Unsupported project file version: ", version);
        return false;
    }
    data.fileVersion = version;
    
    // Read window state
    data.windowWidth = readInt(file);
    data.windowHeight = readInt(file);
    data.windowMaximized = readBool(file);
    
    // Read camera state
    data.cameraPosition = readVec3(file);
    data.cameraFront = readVec3(file);
    data.cameraUp = readVec3(file);
    data.cameraYaw = readFloat(file);
    data.cameraPitch = readFloat(file);
    
    // Read scene objects
    int sceneObjectCount = readInt(file);
    data.sceneObjects.clear();
    data.sceneObjects.reserve(sceneObjectCount);
    for (int i = 0; i < sceneObjectCount; i++) {
        std::string name = readString(file);
        SceneObject obj(name);
        obj.position = readVec3(file);
        obj.rotation = readVec3(file);
        obj.quaternion = readQuat(file);
        obj.scale = readVec3(file);
        obj.color = readVec3(file);
        obj.visible = readBool(file);
        obj.sourceObjectIndex = readInt(file);
        obj.meshIndex = readInt(file);
        obj.materialIndex = readInt(file);
        obj.sourceObjectGUID = readString(file);
        if (version >= 2) {
            obj.meshGUID = readString(file);
            obj.materialGUID = readString(file);
        }
        if (version >= 3) {
            obj.overridesMesh = readBool(file);
            obj.overridesMaterial = readBool(file);
        }
        data.sceneObjects.push_back(obj);
    }
    data.selectedObjectIndex = readInt(file);
    
    // Note: Content items and folders are no longer serialized
    // They will be dynamically scanned from the filesystem after load
    data.contentItems.clear();
    data.folders.clear();
    data.currentFolder = "Meshes"; // Default folder
    data.selectedContentIndex = -1;
    data.selectedFolderIndex = 0;
    
    // Read tabs
    int tabCount = readInt(file);
    data.tabs.clear();
    data.tabs.reserve(tabCount);
    for (int i = 0; i < tabCount; i++) {
        std::string name = readString(file);
        TabType type = static_cast<TabType>(readInt(file));
        int contentItemIndex = readInt(file);
        Tab tab(name, type, contentItemIndex);
        tab.viewportWidth = readInt(file);
        tab.viewportHeight = readInt(file);
        tab.cameraDistance = readFloat(file);
        tab.cameraYaw = readFloat(file);
        tab.cameraPitch = readFloat(file);
        readBool(file); // Read hasUnsavedChanges but ignore it - always start as saved
        tab.hasUnsavedChanges = false; // Force to false on load
        if (version >= 4) tab.assetId = readString(file);  // older files didn't say which asset a tab showed
        tab.needsSelection = false; // Don't auto-select on load
        data.tabs.push_back(tab);
    }
    data.activeTabIndex = readInt(file);
    
    // Read UI state
    data.showObjectsPanel = readBool(file);
    data.showContentBrowser = readBool(file);

    // Read the environment: a file from before version 5 has the default sky
    data.environment = EnvironmentSettings();
    if (version >= 5) {
        data.environment.hdriPath = readString(file);
        data.environment.intensity = readFloat(file);
        data.environment.rotationDegrees = readFloat(file);
        data.environment.showBackground = readBool(file);
        if (!file) throw std::runtime_error("the project file ends in the middle of its environment settings");
    }
    if (version >= 6) {
        data.environment.shadows = readBool(file);
        data.environment.shadowDistance = readFloat(file);
        data.environment.ambientOcclusion = readBool(file);
        data.environment.aoRadius = readFloat(file);
        data.environment.aoStrength = readFloat(file);
        if (!file) throw std::runtime_error("the project file ends in the middle of its render settings");
    }
    if (version >= 7) {
        data.environment.renderMode = renderModeFromInt(readInt(file));
        data.environment.rtShadowRays = readInt(file);
        data.environment.rtAoRays = readInt(file);
        if (!file) throw std::runtime_error("the project file ends in the middle of its ray tracing settings");
    }
    data.environment.sanitize();  // a damaged value shouldn't turn the whole scene black or blow it out
    
    file.close();
    OutputLog::print(LogCategory::Project, LogLevel::Info, "Project loaded successfully from: ", filepath);
    return true;
}

} // namespace Propulsion
