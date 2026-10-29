#pragma once

#include "core/CameraSettings.h"
#include "core/CollisionSettings.h"
#include "core/LightSettings.h"
#include "core/MaterialSettings.h"
#include "input/InputTypes.h"
#include "utils/Utils.h"

#include <string>
#include <ctime>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace Propulsion {

// Structure to store node data for materials (extended to include all node properties)
struct MaterialNodeData {
    std::string nodeType;  // e.g., "MaterialOutput", "ColorWheel"
    int nodeId;
    float posX;
    float posY;
    
    // ColorWheel-specific data
    glm::vec3 color;       // RGB color (0-1 range)
    float hue;             // HSV hue (0-360)
    float saturation;      // HSV saturation (0-1)
    float value;           // HSV value (0-1)
    
    // Image node-specific data
    std::string texturePath;  // Path to texture file
    int textureType;          // TextureType enum value (0=Albedo, etc.)
    float strength;           // Texture strength (0-2, default 1.0) for all non-albedo textures
    bool invertTexture;       // Invert texture values (for normal/metallic/roughness/AO)
    
    MaterialNodeData() : nodeId(0), posX(0), posY(0), 
                         color(1.0f, 1.0f, 1.0f), hue(0.0f), saturation(0.0f), value(1.0f),
                         textureType(0), strength(1.0f), invertTexture(false) {}
    MaterialNodeData(const std::string& type, int id, float x, float y)
        : nodeType(type), nodeId(id), posX(x), posY(y),
          color(1.0f, 1.0f, 1.0f), hue(0.0f), saturation(0.0f), value(1.0f),
          textureType(0), strength(1.0f), invertTexture(false) {}
};

// Structure to store node connections
struct MaterialConnection {
    int outputNodeId;
    int outputPinId;
    int inputNodeId;
    int inputPinId;
    
    MaterialConnection() : outputNodeId(0), outputPinId(0), inputNodeId(0), inputPinId(0) {}
    MaterialConnection(int outNode, int outPin, int inNode, int inPin)
        : outputNodeId(outNode), outputPinId(outPin), inputNodeId(inNode), inputPinId(inPin) {}
};

// Keep old struct for backwards compatibility during loading
struct MaterialNodePosition {
    std::string nodeType;
    int nodeId;
    float posX;
    float posY;
    
    MaterialNodePosition() : nodeId(0), posX(0), posY(0) {}
    MaterialNodePosition(const std::string& type, int id, float x, float y)
        : nodeType(type), nodeId(id), posX(x), posY(y) {}
};

struct ContentItem {
    std::string id;           // Unique identifier (GUID stored in file)
    std::string name;
    std::string type;
    std::string folder;       // Which folder this asset belongs to
    std::string filepath;     // Relative path to the asset file
    std::time_t creationDate;
    VkDescriptorSet thumbnailTexture;  // Vulkan descriptor set for ImGui
    glm::vec3 materialColor;  // For materials
    int meshReference;        // Index to mesh item (for Objects) - DEPRECATED, use meshReferenceGUID
    int materialReference;    // Index to material item (for Objects) - DEPRECATED, use materialReferenceGUID
    std::string meshReferenceGUID;      // GUID of referenced mesh (for Objects)
    std::string materialReferenceGUID;  // GUID of referenced material (for Objects)
    std::string scriptFile;             // PropulsionScript this Object runs (registry path like "rotator.prop"); empty = no behavior
    CameraSettings camera;              // The Object's Camera component (camera.enabled is false when it has none)
    CollisionSettings collision;        // Whether the Object is solid, and its Character Movement component (collision.character.enabled)
    LightSettings light;                // The Object's Light component (light.enabled is false when it has none)
    std::string inputMappingGUID;       // The Input Mapping asset this Object is played with (empty = no input)
    // Input assets (type "InputAction" and "InputMapping"): the file name is the asset name; the rest is kept here.
    InputActionDef inputAction;         // for an Input Action
    InputMappingDef inputMapping;       // for an Input Mapping
    std::vector<MaterialNodePosition> nodePositions;  // Legacy - for old format compatibility
    std::vector<MaterialNodeData> nodes;              // Full node data for materials
    std::vector<MaterialConnection> connections;      // Node connections for materials
    
    // Thumbnail data for persistence (128x128 RGBA = 65536 bytes)
    std::vector<unsigned char> thumbnailPixels;
    uint32_t thumbnailWidth = 0;
    uint32_t thumbnailHeight = 0;
    
    // Material editor splitter position (0.0 to 1.0)
    float splitterRatio = 0.5f;

    // Material: solid, cut out or see-through, and what see-through does to light (kept at the end of the .pmat file)
    MaterialSettings materialSettings;
    
    ContentItem(const std::string& n, const std::string& t, const std::string& f = "", const std::string& fp = "")
        : id(""), name(n), type(t), folder(f), filepath(fp), thumbnailTexture(VK_NULL_HANDLE), materialColor(0.7f, 0.7f, 0.7f),
          meshReference(-1), materialReference(-1), thumbnailWidth(0), thumbnailHeight(0), splitterRatio(0.5f) {
        creationDate = std::time(nullptr);
        // A mesh file has nowhere to keep an ID, so its ID comes from where it lives (see meshAssetGUID). Every other
        // asset type keeps its own ID in its file, or is given one when it is created.
        if (type == "Mesh") id = PropulsionMKI::Utils::meshAssetGUID(filepath, name);
    }
};

} // namespace Propulsion
