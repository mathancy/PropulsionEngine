#include "nodes/ColorWheelNode.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace Propulsion {

ColorWheelNode::ColorWheelNode(int nodeId, ImVec2 pos)
    : Node(nodeId, "Color Wheel", "ColorWheel", pos, ImVec2(8, 7)),
      color(1.0f, 1.0f, 1.0f), hue(0.0f), saturation(0.0f), value(1.0f),
      colorInputMode(0)
{
    headerColor = IM_COL32(100, 180, 100, 255);  // Green color for color nodes
    canDelete = true;
    
    // Single output pin for color
    addOutput("Output", PinType::Color, 100);
    
    // Initialize hex and RGB buffers
    updateHexFromColor();
    updateRgbBuffersFromColor();
}

void ColorWheelNode::updateRgbBuffersFromColor() {
    snprintf(rBuffer, sizeof(rBuffer), "%d", static_cast<int>(color.r * 255.0f + 0.5f));
    snprintf(gBuffer, sizeof(gBuffer), "%d", static_cast<int>(color.g * 255.0f + 0.5f));
    snprintf(bBuffer, sizeof(bBuffer), "%d", static_cast<int>(color.b * 255.0f + 0.5f));
}

bool ColorWheelNode::updateColorFromRgb() {
    int r = atoi(rBuffer);
    int g = atoi(gBuffer);
    int b = atoi(bBuffer);
    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
        color.r = r / 255.0f;
        color.g = g / 255.0f;
        color.b = b / 255.0f;
        rgbToHsv();
        updateHexFromColor();
        return true;
    }
    return false;
}

void ColorWheelNode::rgbToHsv() {
    float r = color.r, g = color.g, b = color.b;
    float maxC = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float minC = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float delta = maxC - minC;
    
    value = maxC;
    saturation = (maxC > 0.0f) ? (delta / maxC) : 0.0f;
    
    if (delta < 0.00001f) {
        hue = 0.0f;
    } else if (maxC == r) {
        hue = 60.0f * fmodf(((g - b) / delta), 6.0f);
    } else if (maxC == g) {
        hue = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        hue = 60.0f * (((r - g) / delta) + 4.0f);
    }
    if (hue < 0.0f) hue += 360.0f;
}

void ColorWheelNode::hsvToRgb() {
    float h = hue, s = saturation, v = value;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    
    float r, g, b;
    if (h < 60.0f)       { r = c; g = x; b = 0; }
    else if (h < 120.0f) { r = x; g = c; b = 0; }
    else if (h < 180.0f) { r = 0; g = c; b = x; }
    else if (h < 240.0f) { r = 0; g = x; b = c; }
    else if (h < 300.0f) { r = x; g = 0; b = c; }
    else                 { r = c; g = 0; b = x; }
    
    color.r = r + m;
    color.g = g + m;
    color.b = b + m;
}

void ColorWheelNode::updateHexFromColor() {
    int r = static_cast<int>(color.r * 255.0f + 0.5f);
    int g = static_cast<int>(color.g * 255.0f + 0.5f);
    int b = static_cast<int>(color.b * 255.0f + 0.5f);
    snprintf(hexBuffer, sizeof(hexBuffer), "%02X%02X%02X", r, g, b);
}

bool ColorWheelNode::updateColorFromHex() {
    unsigned int hexValue;
    if (sscanf(hexBuffer, "%06X", &hexValue) == 1) {
        color.r = ((hexValue >> 16) & 0xFF) / 255.0f;
        color.g = ((hexValue >> 8) & 0xFF) / 255.0f;
        color.b = (hexValue & 0xFF) / 255.0f;
        rgbToHsv();
        updateRgbBuffersFromColor();
        return true;
    }
    return false;
}

} // namespace Propulsion
