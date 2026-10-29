#include "rendering/SVGRenderer.h"
#include "core/OutputLog.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace Propulsion {

SVGRenderer::SVGRenderer() 
    : m_cachedWidth(0), m_cachedHeight(0) {
}

SVGRenderer::~SVGRenderer() {
    cleanupTexture();
}

void SVGRenderer::cleanupTexture() {
    // TODO: Implement Vulkan texture cleanup
    // if (m_textureImage != VK_NULL_HANDLE) {
    //     vkDestroyImage(device, m_textureImage, nullptr);
    // }
}

bool SVGRenderer::loadFromFile(const std::string& filepath) {
    // Try multiple paths
    std::vector<std::string> paths = {
        filepath,
        "../" + filepath,
        "../../" + filepath
    };
    
    for (const auto& path : paths) {
        m_document = lunasvg::Document::loadFromFile(path);
        if (m_document) {
            OutputLog::print(LogCategory::Render, LogLevel::Info, "SVG loaded successfully: ", path);
            return true;
        }
    }
    
    OutputLog::print(LogCategory::Render, LogLevel::Error, "Failed to load SVG: ", filepath);
    return false;
}

void* SVGRenderer::renderToTexture(int width, int height) {
    if (!m_document) {
        OutputLog::print(LogCategory::Render, LogLevel::Error, "No SVG document loaded");
        return nullptr;
    }
    
    // TODO: Implement Vulkan texture rendering
    // For now, just validate the SVG can be rendered
    auto bitmap = m_document->renderToBitmap(width, height);
    if (!bitmap.valid()) {
        OutputLog::print(LogCategory::Render, LogLevel::Error, "Failed to render SVG to bitmap");
        return nullptr;
    }
    
    m_cachedWidth = width;
    m_cachedHeight = height;
    
    // TODO: Create Vulkan texture from bitmap data
    // - Create staging buffer with bitmap.data()
    // - Create VkImage
    // - Copy from staging to image
    // - Create VkImageView and VkDescriptorSet
    // - Return descriptor set
    
    return nullptr; // Placeholder until Vulkan texture loading is implemented
}

void SVGRenderer::setColor(const std::string& elementId, float r, float g, float b, float a) {
    if (!m_document) {
        return;
    }
    
    // Note: LunaSVG doesn't easily support runtime color modification
    // This would require parsing and modifying the SVG DOM
    // For now, this is a placeholder for future implementation
    // You would need to use lunasvg::Element manipulation
}

void SVGRenderer::animateGradient(const std::string& gradientId, float offset) {
    if (!m_document) return;
    
    // Get the document element
    auto element = m_document->documentElement();
    if (!element) return;
    
    // Shift the gradient along the viewBox width (3000 units)
    float viewBoxWidth = 3000.0f;
    float shift = offset * viewBoxWidth;
    
    // Find and modify the gradient element
    auto gradientElement = m_document->getElementById(gradientId);
    if (gradientElement.isNull()) return;
    
    gradientElement.setAttribute("x1", std::to_string(shift));
    gradientElement.setAttribute("x2", std::to_string(shift + viewBoxWidth));
    
    // Invalidate cached texture to force re-render
    m_cachedWidth = 0;
    m_cachedHeight = 0;
}

float SVGRenderer::getWidth() const {
    if (m_document) {
        return m_document->width();
    }
    return 0.0f;
}

float SVGRenderer::getHeight() const {
    if (m_document) {
        return m_document->height();
    }
    return 0.0f;
}

} // namespace Propulsion
