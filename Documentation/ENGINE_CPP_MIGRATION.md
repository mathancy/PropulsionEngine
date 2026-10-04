# Engine.cpp Vulkan Migration - Critical Changes Required

## Overview
Engine.cpp is ~2004 lines and requires extensive updates. This document outlines the critical changes.

## ⚠️ IMPORTANT NOTE
Due to the size and complexity of Engine.cpp, a complete line-by-line migration would be extremely long. 
Instead, this document provides the key patterns and changes needed.

## Critical Changes by Section

### 1. Constructor Initialization List (Line ~265)
**REMOVE:**
```cpp
m_viewportFBO(0), m_viewportTexture(0), m_viewportRBO(0),
m_moveIconTexture(0), m_rotateIconTexture(0), m_scaleIconTexture(0),
m_logoTexture(0),
```

**ADD:**
```cpp
m_vulkanContext(nullptr),
m_logoImage(VK_NULL_HANDLE), m_logoImageMemory(VK_NULL_HANDLE),
m_logoImageView(VK_NULL_HANDLE), m_logoSampler(VK_NULL_HANDLE),
m_logoDescriptorSet(VK_NULL_HANDLE),
m_moveIconImage(VK_NULL_HANDLE), m_moveIconMemory(VK_NULL_HANDLE),
// ... etc for all Vulkan texture handles
```

### 2. After Window Creation (Line ~304)
**ADD:**
```cpp
m_window = std::make_unique<Window>(width, height, "Forge Engine");
CrashHandler::logInfo("Window created successfully");

// Create Vulkan context
m_vulkanContext = std::make_unique<VulkanContext>(m_window->getNativeWindow());
CrashHandler::logInfo("Vulkan context created successfully");
```

### 3. init() Function - Shader Creation (Line ~323)
**REPLACE:**
```cpp
// OLD:
m_shader = std::make_unique<Shader>(vertexShaderSource, fragmentShaderSource, false);
m_outlineShader = std::make_unique<Shader>(vertexShaderSource, outlineFragmentShaderSource, false);
m_gizmoShader = std::make_unique<Shader>(vertexShaderSource, colorFragmentShaderSource, false);

// NEW:
m_shader = std::make_unique<Shader>(m_vulkanContext.get(), "shaders/basic_vert.spv", "shaders/basic_frag.spv");
// Note: outline and gizmo shaders will need their own SPIR-V files
// For now, you can reuse the basic shader or create simplified versions
```

### 4. init() Function - Mesh Creation (Line ~335)
**REPLACE:**
```cpp
// OLD:
m_cube = MeshLoader::loadFromFile("resources/default_meshes/Cube.fbx");
m_sphere = MeshLoader::loadFromFile("resources/default_meshes/Sphere.fbx");
m_cylinder = MeshLoader::loadFromFile("resources/default_meshes/Cylinder.fbx");
m_gizmoCube = std::unique_ptr<Mesh>(Mesh::createCube());
m_gizmoCone = std::unique_ptr<Mesh>(Mesh::createCone(16));
m_gizmoTorus = std::unique_ptr<Mesh>(Mesh::createTorus(1.0f, 0.015f, 32, 16));
m_cone = std::unique_ptr<Mesh>(Mesh::createCone(16));
m_torus = std::unique_ptr<Mesh>(Mesh::createTorus(1.0f, 0.015f, 32, 16));

// NEW:
// MeshLoader needs to be updated to work with Vulkan
// For now, use procedural meshes:
m_cube = std::unique_ptr<Mesh>(Mesh::createCube(m_vulkanContext.get()));
m_sphere = std::unique_ptr<Mesh>(Mesh::createSphere(m_vulkanContext.get()));
m_cylinder = std::unique_ptr<Mesh>(Mesh::createCylinder(m_vulkanContext.get()));
m_gizmoCube = std::unique_ptr<Mesh>(Mesh::createCube(m_vulkanContext.get()));
m_gizmoCone = std::unique_ptr<Mesh>(Mesh::createCone(m_vulkanContext.get(), 16));
m_gizmoTorus = std::unique_ptr<Mesh>(Mesh::createTorus(m_vulkanContext.get(), 1.0f, 0.015f, 32, 16));
m_cone = std::unique_ptr<Mesh>(Mesh::createCone(m_vulkanContext.get(), 16));
m_torus = std::unique_ptr<Mesh>(Mesh::createTorus(m_vulkanContext.get(), 1.0f, 0.015f, 32, 16));
```

### 5. init() Function - Remove OpenGL Framebuffer Code (Lines 367-392)
**REMOVE ENTIRELY:**
```cpp
// DELETE ALL OF THIS:
glGenFramebuffers(1, &m_viewportFBO);
glBindFramebuffer(GL_FRAMEBUFFER, m_viewportFBO);
glGenTextures(1, &m_viewportTexture);
// ... all OpenGL framebuffer creation code
glBindFramebuffer(GL_FRAMEBUFFER, 0);
```

**REPLACE WITH:**
```cpp
// Vulkan handles viewport rendering through swap chain - no custom framebuffer needed
CrashHandler::logInfo("Using Vulkan swap chain for viewport rendering");
```

### 6. initImGui() Function (Line ~430)
**REPLACE:**
```cpp
// OLD:
ImGui_ImplGlfw_InitForOpenGL(m_window->getNativeWindow(), true);
ImGui_ImplOpenGL3_Init("#version 450");

// NEW:
ImGui_ImplGlfw_InitForVulkan(m_window->getNativeWindow(), true);

ImGui_ImplVulkan_InitInfo init_info = {};
init_info.Instance = m_vulkanContext->getInstance();
init_info.PhysicalDevice = m_vulkanContext->getPhysicalDevice();
init_info.Device = m_vulkanContext->getDevice();
init_info.QueueFamily = m_vulkanContext->getQueueFamilies().graphicsFamily.value();
init_info.Queue = m_vulkanContext->getGraphicsQueue();
init_info.PipelineCache = VK_NULL_HANDLE;
init_info.DescriptorPool = m_vulkanContext->getDescriptorPool();
init_info.Subpass = 0;
init_info.MinImageCount = 2;
init_info.ImageCount = (uint32_t)m_vulkanContext->getSwapChainImages().size();
init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
init_info.Allocator = nullptr;
init_info.CheckVkResultFn = nullptr;

ImGui_ImplVulkan_Init(&init_info, m_vulkanContext->getRenderPass());

// Upload fonts to GPU
VkCommandBuffer cmd = m_vulkanContext->beginSingleTimeCommands();
ImGui_ImplVulkan_CreateFontsTexture(cmd);
m_vulkanContext->endSingleTimeCommands(cmd);
ImGui_ImplVulkan_DestroyFontUploadObjects();
```

### 7. render() Function - Main Rendering Loop
**This is the most complex change.** Find the render() function and REPLACE the entire OpenGL rendering code:

**OLD PATTERN:**
```cpp
void Engine::render() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_viewportFBO);
    glViewport(0, 0, m_viewportWidth, m_viewportHeight);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    m_shader->use();
    // Set uniforms
    // Draw meshes
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
```

**NEW PATTERN:**
```cpp
void Engine::render() {
    m_vulkanContext->beginFrame();
    
    VkCommandBuffer cmd = m_vulkanContext->getCurrentCommandBuffer();
    uint32_t currentFrame = m_vulkanContext->getCurrentFrame();
    
    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_vulkanContext->getSwapChainExtent().width;
    viewport.height = (float)m_vulkanContext->getSwapChainExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_vulkanContext->getSwapChainExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // Bind shader pipeline
    m_shader->bind(cmd);
    
    // Render scene objects
    float aspectRatio = (float)m_vulkanContext->getSwapChainExtent().width / 
                       (float)m_vulkanContext->getSwapChainExtent().height;
    
    for (size_t i = 0; i < m_sceneObjects.size(); i++) {
        const auto& obj = m_sceneObjects[i];
        if (!obj.visible) continue;
        
        // Calculate model matrix
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, obj.position);
        model *= glm::mat4_cast(obj.quaternion);
        model = glm::scale(model, obj.scale);
        
        // Update uniforms
        UniformBufferObject ubo{};
        ubo.model = model;
        ubo.view = m_camera->getViewMatrix();
        ubo.projection = m_camera->getProjectionMatrix(aspectRatio);
        ubo.color = obj.color;
        
        m_shader->updateUniformBuffer(currentFrame, ubo);
        
        // Bind descriptor set and draw
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_shader->getPipelineLayout(), 0, 1,
                                &m_shader->getDescriptorSet(currentFrame), 0, nullptr);
        
        // Get mesh based on object type
        Mesh* mesh = m_cube.get(); // Default
        if (obj.meshIndex >= 0 && obj.meshIndex < m_contentItems.size()) {
            const auto& item = m_contentItems[obj.meshIndex];
            if (item.name == "Cube") mesh = m_cube.get();
            else if (item.name == "Sphere") mesh = m_sphere.get();
            else if (item.name == "Cylinder") mesh = m_cylinder.get();
        }
        
        mesh->draw(cmd);
    }
    
    // Render world grid (needs Vulkan update - see WorldGrid section)
    if (m_showWorldGrid) {
        // m_worldGrid->render(...); // Needs Vulkan implementation
    }
    
    // Render gizmos (needs Vulkan update - see Gizmo section)
    if (m_selectedObjectIndex >= 0 && m_gizmoMode != GizmoMode::None) {
        // renderGizmo(); // Needs Vulkan implementation
    }
    
    m_vulkanContext->endFrame();
}
```

### 8. renderUI() Function
**UPDATE ImGui Rendering:**

Find where ImGui is rendered and UPDATE:
```cpp
// OLD:
ImGui::Render();
ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

// NEW:
ImGui::Render();
VkCommandBuffer cmd = m_vulkanContext->getCurrentCommandBuffer();
ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
```

### 9. run() Function - Main Loop
**UPDATE to integrate Vulkan rendering:**

```cpp
void Engine::run() {
    while (!m_window->shouldClose()) {
        // ... timing code ...
        
        m_window->pollEvents();
        processInput(m_deltaTime);
        update(m_deltaTime);
        
        // Begin Vulkan frame
        m_vulkanContext->beginFrame();
        
        render(); // Renders scene to command buffer
        
        // Render ImGui
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        renderUI(); // Builds ImGui draw data
        
        ImGui::Render();
        VkCommandBuffer cmd = m_vulkanContext->getCurrentCommandBuffer();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        
        // End Vulkan frame and present
        m_vulkanContext->endFrame();
    }
    
    m_vulkanContext->waitIdle();
}
```

### 10. shutdown() / Destructor
**REPLACE:**
```cpp
// OLD:
ImGui_ImplOpenGL3_Shutdown();
ImGui_ImplGlfw_Shutdown();
glDeleteFramebuffers(1, &m_viewportFBO);
glDeleteTextures(1, &m_viewportTexture);
glDeleteRenderbuffers(1, &m_viewportRBO);
glDeleteTextures(1, &m_moveIconTexture);
glDeleteTextures(1, &m_rotateIconTexture);
glDeleteTextures(1, &m_scaleIconTexture);

// NEW:
m_vulkanContext->waitIdle();
ImGui_ImplVulkan_Shutdown();
ImGui_ImplGlfw_Shutdown();
// Vulkan resources are cleaned up by their destructors
```

### 11. loadToolIcons() Function
**REQUIRES COMPLETE REWRITE** - OpenGL texture loading to Vulkan image loading
This is complex and requires:
1. Load image data with stb_image
2. Create staging buffer
3. Create VkImage
4. Allocate device memory
5. Copy from staging to device local
6. Create VkImageView
7. Create VkSampler
8. Create descriptor set for ImGui

**Simplified version:** Comment out icon loading for initial testing, use text buttons instead.

### 12. generateThumbnail() Function
Similar to loadToolIcons - requires complete Vulkan rewrite. Consider deferring this.

### 13. onResize() Function
**UPDATE:**
```cpp
void Engine::onResize(int width, int height) {
    m_viewportWidth = width;
    m_viewportHeight = height;
    
    // Recreate Vulkan swap chain
    m_vulkanContext->recreateSwapChain(width, height);
}
```

## Files That Also Need Updates

1. **WorldGrid.h/cpp** - If it uses OpenGL rendering
2. **MeshLoader.cpp** - Update to create Vulkan meshes
3. **SVGRenderer.cpp** - Update texture loading to Vulkan

## Testing Strategy

1. **Phase 1:** Comment out complex features:
   - Texture loading (icons, thumbnails)
   - World grid
   - Gizmos
   - Outline rendering
   
2. **Phase 2:** Get basic rendering working:
   - Just render colored cubes
   - Get ImGui displaying
   
3. **Phase 3:** Gradually add back features

## Quick Start - Minimal Changes

To get a compiling version quickly:

1. Update includes (imgui_impl_vulkan.h)
2. Add m_vulkanContext creation after window
3. Update shader creation to use SPIR-V files
4. Update mesh creation to pass m_vulkanContext
5. COMMENT OUT all framebuffer code
6. COMMENT OUT texture loading
7. Update ImGui initialization
8. Simplify render() to just draw cubes
9. Update shutdown

This will give you a basic Vulkan engine that renders, even if many features are temporarily disabled.
