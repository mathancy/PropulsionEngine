#pragma once

// Common includes for all node types
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace Propulsion {

// Pin data types for visual distinction
enum class PinType {
    Color,      // RGB/RGBA color (green)
    Float,      // Single float value (grey)
    Vector2,    // 2D vector (light blue)
    Vector3,    // 3D vector/normal (blue)
    Texture,    // Texture sampler (yellow)
    Exec,       // Order of execution in a script (white)
    Any         // Can connect to anything (white)
};

// Get color for pin type
inline ImU32 getPinColor(PinType type) {
    switch (type) {
        case PinType::Color:    return IM_COL32(100, 200, 100, 255);  // Green
        case PinType::Float:    return IM_COL32(180, 180, 180, 255);  // Grey
        case PinType::Vector2:  return IM_COL32(100, 200, 255, 255);  // Light blue
        case PinType::Vector3:  return IM_COL32(100, 150, 255, 255);  // Blue
        case PinType::Texture:  return IM_COL32(255, 220, 100, 255);  // Yellow
        case PinType::Exec:     return IM_COL32(245, 245, 245, 255);  // White
        case PinType::Any:      return IM_COL32(255, 255, 255, 255);  // White
        default:                return IM_COL32(200, 200, 200, 255);
    }
}

// Represents a pin (input or output) on a node
struct NodePin {
    int id;
    std::string name;
    bool isOutput;
    PinType type;
    ImVec2 position;  // Relative to node position
    
    NodePin(int pinId, const std::string& pinName, bool output, PinType pinType = PinType::Any)
        : id(pinId), name(pinName), isOutput(output), type(pinType), position(0, 0) {}
};

// Represents a connection between two pins
struct NodeConnection {
    int outputNodeId;
    int outputPinId;
    int inputNodeId;
    int inputPinId;
};

// Base class for nodes in the graph
struct Node {
    int id;
    std::string name;
    std::string type;
    ImVec2 position;      // Position in grid units
    ImVec2 size;          // Size in grid units
    ImU32 headerColor;    // Header/title bar color
    std::vector<NodePin> inputs;
    std::vector<NodePin> outputs;
    bool selected;
    bool canDelete;       // Some nodes (like output) cannot be deleted
    int zOrder;           // Rendering order - higher values render on top
    
    Node(int nodeId, const std::string& nodeName, const std::string& nodeType, ImVec2 pos, ImVec2 gridSize = ImVec2(6, 8))
        : id(nodeId), name(nodeName), type(nodeType), position(pos), 
          size(gridSize), headerColor(IM_COL32(80, 80, 100, 255)), 
          selected(false), canDelete(true), zOrder(0) {}
    
    virtual ~Node() = default;
    
    // Helper to add pins
    void addInput(const std::string& name, PinType type, int pinId) {
        inputs.emplace_back(pinId, name, false, type);
    }
    
    void addOutput(const std::string& name, PinType type, int pinId) {
        outputs.emplace_back(pinId, name, true, type);
    }
};

// Texture type enum for PBR workflow
enum class TextureType {
    Albedo = 0,
    AmbientOcclusion,
    Height,
    Metallic,
    Normal,
    Roughness,
    Count  // For iteration
};

// Get string name for texture type
inline const char* getTextureTypeName(TextureType type) {
    switch (type) {
        case TextureType::Albedo:           return "Albedo";
        case TextureType::AmbientOcclusion: return "Ambient Occlusion";
        case TextureType::Height:           return "Height";
        case TextureType::Metallic:         return "Metallic";
        case TextureType::Normal:           return "Normal";
        case TextureType::Roughness:        return "Roughness";
        default:                            return "Unknown";
    }
}

// Get output pin type for texture type
inline PinType getTextureOutputType(TextureType type) {
    switch (type) {
        case TextureType::Albedo:           return PinType::Color;
        case TextureType::AmbientOcclusion: return PinType::Float;
        case TextureType::Height:           return PinType::Float;
        case TextureType::Metallic:         return PinType::Float;
        case TextureType::Normal:           return PinType::Vector3;
        case TextureType::Roughness:        return PinType::Float;
        default:                            return PinType::Color;
    }
}

// Texture info for dropdown display
struct TextureInfo {
    std::string name;
    std::string filepath;
    VkDescriptorSet thumbnail;  // For preview in dropdown
    
    TextureInfo() : thumbnail(VK_NULL_HANDLE) {}
    TextureInfo(const std::string& n, const std::string& fp, VkDescriptorSet thumb)
        : name(n), filepath(fp), thumbnail(thumb) {}
};

} // namespace Propulsion
