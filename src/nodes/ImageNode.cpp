#include "nodes/ImageNode.h"

namespace Propulsion {

ImageNode::ImageNode(int nodeId, ImVec2 pos)
    : Node(nodeId, "Image", "Image", pos, ImVec2(6, 9)),
      selectedTextureIndex(-1), textureType(TextureType::Albedo), strength(1.0f), invertTexture(false)
{
    headerColor = IM_COL32(200, 180, 80, 255);  // Yellowish color for texture nodes
    canDelete = true;
    
    // Single output pin - type depends on texture type selection
    addOutput("Output", PinType::Color, 100);
}

void ImageNode::updateOutputPinType() {
    if (!outputs.empty()) {
        outputs[0].type = getTextureOutputType(textureType);
    }
    // Update node size based on texture type
    // Albedo: 9 grid units (no invert/strength controls)
    // Others: 11 grid units (4 rows of controls + image preview)
    if (textureType == TextureType::Albedo) {
        size.y = 9.0f;
    } else {
        size.y = 11.0f;
    }
}

} // namespace Propulsion
