#pragma once

#include "NodeTypes.h"

namespace Propulsion {

// Image Node - provides texture sampling output
struct ImageNode : public Node {
    int selectedTextureIndex;     // Index into available textures list (-1 = none)
    TextureType textureType;      // What type of texture this is
    std::string selectedTexturePath;  // Store the actual path for serialization
    float strength;               // Strength multiplier (0.0 - 2.0) for all non-albedo textures
    bool invertTexture;           // Invert texture values (for normal/metallic/roughness/AO)
    
    ImageNode(int nodeId, ImVec2 pos);
    
    // Update output pin type based on texture type
    void updateOutputPinType();
};

} // namespace Propulsion
