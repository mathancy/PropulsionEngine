#pragma once

#include "NodeTypes.h"

namespace Propulsion {

// Color Wheel Node - provides color output via interactive color picker
struct ColorWheelNode : public Node {
    glm::vec3 color;          // Current RGB color (0-1 range)
    float hue;                // HSV hue (0-360)
    float saturation;         // HSV saturation (0-1)
    float value;              // HSV value/brightness (0-1)
    char hexBuffer[8];        // Hex color input buffer
    char rBuffer[4];          // R component buffer (0-255)
    char gBuffer[4];          // G component buffer (0-255)
    char bBuffer[4];          // B component buffer (0-255)
    int colorInputMode;       // 0 = Hex, 1 = RGB
    
    ColorWheelNode(int nodeId, ImVec2 pos);
    
    // Update RGB buffers from color
    void updateRgbBuffersFromColor();
    
    // Update color from RGB buffers
    bool updateColorFromRgb();
    
    // Convert RGB to HSV
    void rgbToHsv();
    
    // Convert HSV to RGB
    void hsvToRgb();
    
    // Update hex buffer from current color
    void updateHexFromColor();
    
    // Update color from hex buffer
    bool updateColorFromHex();
};

} // namespace Propulsion
