#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Forge {

// Forward declarations
struct SceneObject;
struct Tab;
struct ContentItem;
class Camera;

struct ProjectData {
    // Set by loadProject: the version of the file that was read. Before version 3 a file didn't say which
    // properties of a placed object were overridden, so the engine works that out from the assets.
    int fileVersion = 0;

    // Window state
    int windowWidth;
    int windowHeight;
    bool windowMaximized;
    
    // Camera state
    glm::vec3 cameraPosition;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    float cameraYaw;
    float cameraPitch;
    
    // Scene objects
    std::vector<SceneObject> sceneObjects;
    int selectedObjectIndex;
    
    // Content items (Materials, Meshes, Objects)
    std::vector<ContentItem> contentItems;
    int selectedContentIndex;
    
    // Content Browser state
    std::vector<std::string> folders;
    std::string currentFolder;
    int selectedFolderIndex;
    
    // Tabs
    std::vector<Tab> tabs;
    int activeTabIndex;
    
    // UI state
    bool showObjectsPanel;
    bool showContentBrowser;
    
    ProjectData() : windowWidth(1280), windowHeight(720), windowMaximized(false),
                   cameraPosition(0.0f), cameraFront(0.0f, 0.0f, -1.0f), cameraUp(0.0f, 1.0f, 0.0f),
                   cameraYaw(-90.0f), cameraPitch(0.0f),
                   selectedObjectIndex(-1), selectedContentIndex(-1),
                   currentFolder("Meshes"), selectedFolderIndex(0),
                   activeTabIndex(0), showObjectsPanel(true), showContentBrowser(true) {}
};

class ProjectSerializer {
public:
    static bool saveProject(const std::string& filepath, const ProjectData& data);
    static bool loadProject(const std::string& filepath, ProjectData& data);
    
private:
    static bool loadProjectFile(const std::string& filepath, ProjectData& data);
    static void writeVec3(std::ofstream& file, const glm::vec3& vec);
    static void writeQuat(std::ofstream& file, const glm::quat& quat);
    static void writeString(std::ofstream& file, const std::string& str);
    static void writeInt(std::ofstream& file, int value);
    static void writeFloat(std::ofstream& file, float value);
    static void writeBool(std::ofstream& file, bool value);
    static void writeTime(std::ofstream& file, time_t time);
    
    static glm::vec3 readVec3(std::ifstream& file);
    static glm::quat readQuat(std::ifstream& file);
    static std::string readString(std::ifstream& file);
    static int readInt(std::ifstream& file);
    static float readFloat(std::ifstream& file);
    static bool readBool(std::ifstream& file);
    static time_t readTime(std::ifstream& file);
};

} // namespace Forge
