#include <stdexcept>
#include "project/ProjectSerializer.h"
#include "Engine.h"
#include "ui/TabSystem.h"
#include "ui/ContentItem.h"
#include "core/Camera.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace Forge {

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
    // filepath is the full path to the .fproj file (e.g., C:\Projects\MyProject\MyProject.fproj)
    std::filesystem::path forgeFile(filepath);
    std::filesystem::path projectDir = forgeFile.parent_path();
    std::string projectName = projectDir.filename().string();
    
    // Create project directory and subdirectories
    try {
        std::filesystem::create_directories(projectDir);
        std::filesystem::create_directories(projectDir / "Assets");
        std::filesystem::create_directories(projectDir / "Assets" / "Meshes");
        std::filesystem::create_directories(projectDir / "Assets" / "Textures");
        std::filesystem::create_directories(projectDir / "Assets" / "Materials");
        std::filesystem::create_directories(projectDir / "Assets" / "Objects");
        
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
        std::cerr << "Failed to create project directories: " << e.what() << std::endl;
        return false;
    }
    
    // Save the .fproj file
    std::ofstream file(forgeFile, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << forgeFile << std::endl;
        return false;
    }
    
    // Write file header and version
    const char* header = "FORGE";
    file.write(header, 5);
    // Version 2 adds each scene object's mesh and material GUIDs, version 3 whether each is overridden
    // on the instance (otherwise it follows its Object asset). Older files still load.
    int version = 3;
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
    }
    writeInt(file, data.activeTabIndex);
    
    // Write UI state
    writeBool(file, data.showObjectsPanel);
    writeBool(file, data.showContentBrowser);
    
    file.close();
    std::cout << "Project saved successfully to: " << projectDir << std::endl;
    std::cout << "Project file: " << forgeFile << std::endl;
    return true;
}

bool ProjectSerializer::loadProject(const std::string& filepath, ProjectData& data) {
    try {
        return loadProjectFile(filepath, data);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load project: " << e.what() << std::endl;
        return false;
    }
}

bool ProjectSerializer::loadProjectFile(const std::string& filepath, ProjectData& data) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << filepath << std::endl;
        return false;
    }
    
    // Read and verify header
    char header[6] = {0};
    file.read(header, 5);
    if (std::string(header) != "FORGE") {
        std::cerr << "Invalid project file format" << std::endl;
        return false;
    }
    
    int version = readInt(file);
    if (version < 1 || version > 3) {
        std::cerr << "Unsupported project file version: " << version << std::endl;
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
        tab.needsSelection = false; // Don't auto-select on load
        data.tabs.push_back(tab);
    }
    data.activeTabIndex = readInt(file);
    
    // Read UI state
    data.showObjectsPanel = readBool(file);
    data.showContentBrowser = readBool(file);
    
    file.close();
    std::cout << "Project loaded successfully from: " << filepath << std::endl;
    return true;
}

} // namespace Forge
