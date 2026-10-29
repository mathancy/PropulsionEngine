#include "nodes/MaterialOutputNode.h"

namespace Propulsion {

MaterialOutputNode::MaterialOutputNode(int nodeId, ImVec2 pos)
    : Node(nodeId, "Final Material", "MaterialOutput", pos, ImVec2(8, 1))  // Height will be calculated dynamically
{
    headerColor = IM_COL32(180, 70, 90, 255);  // Reddish-pink color for output node
    canDelete = false;  // Cannot delete the output node
    
    // Standard PBR inputs
    addInput("Base Color", PinType::Color, 1);
    addInput("Metallic", PinType::Float, 2);
    addInput("Roughness", PinType::Float, 3);
    addInput("Specular", PinType::Float, 4);
    addInput("Normal", PinType::Vector3, 5);
    addInput("Height", PinType::Float, 14);  // Height/displacement map
    addInput("Emissive Color", PinType::Color, 6);
    addInput("Emissive Strength", PinType::Float, 7);
    addInput("Ambient Occlusion", PinType::Float, 8);
    addInput("Opacity", PinType::Float, 9);
    addInput("Opacity Mask", PinType::Float, 10);
    addInput("Refraction", PinType::Float, 11);
    addInput("Subsurface Color", PinType::Color, 12);
    addInput("Subsurface Strength", PinType::Float, 13);
}

} // namespace Propulsion
