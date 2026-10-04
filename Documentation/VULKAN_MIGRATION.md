# Vulkan Migration Guide for ForgeEngine

## Overview
This document outlines the migration of ForgeEngine from OpenGL to Vulkan.

## Completed Changes

### 1. Build System (CMakeLists.txt)
- ✅ Removed GLAD dependency
- ✅ Added Vulkan SDK finding via `find_package(Vulkan REQUIRED)`
- ✅ Updated ImGui backend from OpenGL3 to Vulkan
- ✅ Updated link libraries to use `${Vulkan_LIBRARIES}` instead of `opengl32`

### 2. Core Classes Created
- ✅ **VulkanContext** (`VulkanContext.h/cpp`): Central Vulkan management
  - Instance, device, surface creation
  - Swap chain management
  - Command buffers and synchronization
  - Descriptor pools
  - Helper functions for memory and format selection

### 3. Window System
- ✅ Updated `Window.h/cpp` to remove OpenGL context creation
- ✅ Changed GLFW hints to `GLFW_CLIENT_API, GLFW_NO_API`
- ✅ Removed `swapBuffers()` method (handled by VulkanContext)

### 4. Camera System
- ✅ Updated projection matrix to flip Y-axis for Vulkan coordinate system
- ✅ Added `proj[1][1] *= -1` to handle Vulkan's Y-down convention

### 5. API Headers Updated
- ✅ **Mesh.h**: Changed to use Vulkan buffers instead of VAO/VBO/EBO
- ✅ **Shader.h**: Changed to use Vulkan pipelines and SPIR-V shaders

## Implementation Needed

### 1. Shader Implementation (Shader.cpp)
The new Shader class needs to:
- Load SPIR-V bytecode from `.spv` files (pre-compiled shaders)
- Create Vulkan graphics pipeline with vertex input, rasterization, etc.
- Manage descriptor sets for uniform buffers
- Provide `updateUniformBuffer()` to update MVP matrices

**Required Shaders to Compile:**
You'll need to compile GLSL to SPIR-V using `glslc` or `glslangValidator`:
```bash
# Example commands:
glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv
```

### 2. Mesh Implementation (Mesh.cpp)
The new Mesh class needs to:
- Create vertex buffers using VkBuffer
- Create index buffers using VkBuffer
- Implement `draw()` to use `vkCmdBindVertexBuffers` and `vkCmdDrawIndexed`
- Update all factory methods (createCube, createSphere, etc.) to take VulkanContext*

### 3. Engine Class (Engine.cpp) - MAJOR CHANGES REQUIRED
The Engine class needs extensive updates:

**Initialization:**
- Replace ImGui OpenGL backend with Vulkan backend
- Change from `ImGui_ImplOpenGL3_Init` to `ImGui_ImplVulkan_Init`
- Create VulkanContext after Window creation
- Remove all OpenGL framebuffer code (glGenFramebuffers, glBindFramebuffer, etc.)

**Rendering Loop:**
- Replace `glClear`, `glViewport` with Vulkan render pass
- Use `VulkanContext::beginFrame()` and `endFrame()`
- Update mesh rendering to use Vulkan command buffers
- Replace all `gl*` calls with Vulkan equivalents

**Specific Changes:**
```cpp
// OLD (OpenGL):
glBindFramebuffer(GL_FRAMEBUFFER, m_viewportFBO);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
mesh->draw();
m_window->swapBuffers();

// NEW (Vulkan):
m_vulkanContext->beginFrame();
VkCommandBuffer cmd = m_vulkanContext->getCurrentCommandBuffer();
// Bind pipeline, update uniforms, draw meshes
mesh->draw(cmd);
m_vulkanContext->endFrame();
```

### 4. ImGui Integration
Update Engine initialization:
```cpp
// Instead of:
ImGui_ImplGlfw_InitForOpenGL(window, true);
ImGui_ImplOpenGL3_Init("#version 450");

// Use:
ImGui_ImplGlfw_InitForVulkan(m_window->getNativeWindow(), true);

ImGui_ImplVulkan_InitInfo init_info = {};
init_info.Instance = m_vulkanContext->getInstance();
init_info.PhysicalDevice = m_vulkanContext->getPhysicalDevice();
init_info.Device = m_vulkanContext->getDevice();
init_info.QueueFamily = m_vulkanContext->getQueueFamilies().graphicsFamily.value();
init_info.Queue = m_vulkanContext->getGraphicsQueue();
init_info.PipelineCache = VK_NULL_HANDLE;
init_info.DescriptorPool = m_vulkanContext->getDescriptorPool();
init_info.MinImageCount = 2;
init_info.ImageCount = m_vulkanContext->getSwapChainImages().size();
init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

ImGui_ImplVulkan_Init(&init_info, m_vulkanContext->getRenderPass());

// Upload fonts
VkCommandBuffer cmd = m_vulkanContext->beginSingleTimeCommands();
ImGui_ImplVulkan_CreateFontsTexture(cmd);
m_vulkanContext->endSingleTimeCommands(cmd);
ImGui_ImplVulkan_DestroyFontUploadObjects();
```

### 5. Texture Loading
All texture loading code needs updating:
- Replace `glGenTextures`, `glBindTexture`, `glTexImage2D` with Vulkan image creation
- Create VkImage, VkImageView, VkSampler
- Use staging buffers to transfer texture data to GPU

### 6. Shader Files
Create GLSL shaders and compile them to SPIR-V:

**basic.vert:**
```glsl
#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 color;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;

void main() {
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
    fragColor = ubo.color;
}
```

**basic.frag:**
```glsl
#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(normalize(fragNormal), lightDir), 0.0);
    vec3 ambient = fragColor * 0.3;
    vec3 diffuse = fragColor * diff * 0.7;
    outColor = vec4(ambient + diffuse, 1.0);
}
```

Compile with:
```bash
glslc basic.vert -o basic_vert.spv
glslc basic.frag -o basic_frag.spv
```

## Prerequisites

### Vulkan SDK
Ensure Vulkan SDK is installed:
- Windows: Download from https://vulkan.lunarg.com/
- Set environment variable `VULKAN_SDK`
- Verify installation: `vulkaninfo`

### Shader Compiler
The Vulkan SDK includes `glslc` compiler:
- Located in: `%VULKAN_SDK%\Bin\glslc.exe`

## Testing Strategy

1. **Start Simple**: Get a blank window rendering
2. **Add Triangle**: Render a single colored triangle
3. **Add Cube**: Render the basic cube mesh
4. **Add Camera**: Implement camera transformations
5. **Add UI**: Integrate ImGui rendering
6. **Add Full Scene**: Complete engine functionality

## Common Pitfalls

1. **Validation Layers**: Enable in debug builds to catch errors early
2. **Memory Barriers**: Ensure proper synchronization between operations
3. **Command Buffer Recording**: Must record every frame
4. **Coordinate System**: Remember Y is flipped in Vulkan
5. **Depth Buffer**: Must be created explicitly (already done in VulkanContext)

## Performance Considerations

Vulkan gives explicit control but requires proper:
- Command buffer reuse
- Descriptor set pooling
- Memory allocation strategies
- Pipeline state caching

## Next Steps

1. Implement Shader.cpp with pipeline creation
2. Implement Mesh.cpp with Vulkan buffer management
3. Update Engine.cpp render loop
4. Create and compile basic shaders
5. Test with simple scene
6. Gradually add features back

## Resources

- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [Vulkan Spec](https://www.khronos.org/registry/vulkan/specs/)
- [GLM with Vulkan](https://github.com/g-truc/glm)
- [ImGui Vulkan Example](https://github.com/ocornut/imgui/blob/master/examples/example_glfw_vulkan/)
