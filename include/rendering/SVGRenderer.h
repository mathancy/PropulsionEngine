#pragma once

#include <string>
#include <memory>
#include <lunasvg.h>
#include <vulkan/vulkan.h>

namespace Forge {

class SVGRenderer {
public:
    SVGRenderer();
    ~SVGRenderer();
    
    // Load SVG from file
    bool loadFromFile(const std::string& filepath);
    
    // Render SVG to texture with specified size
    // TODO: Returns Vulkan texture/descriptor (nullptr if failed)
    void* renderToTexture(int width, int height);
    
    // Animate gradient by shifting its position
    // offset: 0.0 to 1.0, represents position in animation cycle
    void animateGradient(const std::string& gradientId, float offset);
    
    // Modify SVG colors at runtime (before rendering)
    void setColor(const std::string& elementId, float r, float g, float b, float a = 1.0f);
    
    // Get dimensions
    float getWidth() const;
    float getHeight() const;
    
private:
    std::unique_ptr<lunasvg::Document> m_document;
    // TODO: VkImage m_textureImage;
    // TODO: VkDescriptorSet m_textureDescriptor;
    int m_cachedWidth;
    int m_cachedHeight;
    
    void cleanupTexture();
};

} // namespace Forge
