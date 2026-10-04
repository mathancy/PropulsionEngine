# Forge Engine - Troubleshooting Guide

Common issues and solutions for Forge Engine development.

---

## Build Issues

### ❌ "Cannot find CMakeLists.txt"

**Error Message:**
```
CMake Error: The source directory "..." does not appear to contain CMakeLists.txt
```

**Solution:**
```powershell
# Make sure you're in the right directory
cd G:\ForgeEngine
# Then create build directory
mkdir build
cd build
cmake ..
```

---

### ❌ "GLAD files not found"

**Error Message:**
```
CMake Error: Cannot find source file: external/glad/src/glad.c
```

**Solution:**

GLAD must be generated manually:

1. Go to https://glad.dav1d.de/
2. Settings:
   - Language: **C/C++**
   - Specification: **OpenGL**
   - API gl: **Version 4.5**
   - Profile: **Core**
   - Options: ✓ **Generate a loader**
3. Click **GENERATE**
4. Download **glad.zip**
5. Extract to `G:\ForgeEngine\external\glad\`

Verify structure:
```powershell
ls external\glad\include\glad\glad.h       # Should exist
ls external\glad\include\KHR\khrplatform.h # Should exist
ls external\glad\src\glad.c                # Should exist
```

---

### ❌ "Cannot find GLFW"

**Error Message:**
```
CMake Error at CMakeLists.txt:XX (add_subdirectory):
  The source directory does not contain a CMakeLists.txt file.
  external/glfw
```

**Solution:**
```powershell
cd external
git clone https://github.com/glfw/glfw.git
cd ..
```

---

### ❌ "GLM not found"

**Error Message:**
```
fatal error: glm/glm.hpp: No such file or directory
```

**Solution:**
```powershell
cd external
git clone https://github.com/g-truc/glm.git
cd ..
# Rebuild
rm -r build; mkdir build; cd build; cmake ..; cmake --build . --config Release
```

---

### ❌ "ImGui undefined reference"

**Error Message:**
```
undefined reference to `ImGui::Begin(char const*, bool*, int)'
```

**Solution:**

ImGui might not be on the docking branch:

```powershell
cd external\imgui
git checkout docking
git pull
cd ..\..
# Clean rebuild
rm -r build
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

### ❌ "LNK2019 unresolved external symbol"

**Error Message:**
```
error LNK2019: unresolved external symbol __imp_glClear
```

**Solution:**

OpenGL libraries not linked. Check:

1. `opengl32.lib` is in CMakeLists.txt (should be there)
2. Clean rebuild:
   ```powershell
   rm -r build
   mkdir build
   cd build
   cmake ..
   cmake --build . --config Release
   ```

---

## Runtime Issues

### ❌ "Failed to initialize GLFW"

**Error Message:**
```
Failed to initialize GLFW
```

**Solution:**

1. Update graphics drivers
2. Check if another application is using OpenGL
3. Try running as administrator

---

### ❌ "Failed to create GLFW window"

**Error Message:**
```
Failed to create GLFW window
```

**Solutions:**

**Check OpenGL Support:**
```powershell
# Check GPU
wmic path win32_VideoController get name

# Update drivers from manufacturer:
# - NVIDIA: https://www.nvidia.com/drivers
# - AMD: https://www.amd.com/support
# - Intel: https://www.intel.com/content/www/us/en/support/detect.html
```

**Lower OpenGL Version (if GPU doesn't support 4.5):**

Edit `src/Window.cpp`:
```cpp
// Change from:
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);

// To:
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
```

Then regenerate GLAD with OpenGL 3.3 at glad.dav1d.de.

---

### ❌ "Failed to initialize GLAD"

**Error Message:**
```
Failed to initialize GLAD
```

**Solution:**

1. Ensure GLAD was generated for the correct OpenGL version
2. Check that GLAD files are correctly placed
3. Regenerate GLAD from glad.dav1d.de
4. Make sure to select "Generate a loader" option

---

### ❌ Black Screen / No Rendering

**Symptoms:**
- Window opens but viewport is black
- No cube visible

**Solutions:**

1. **Check Clear Color:**
   ```cpp
   // In Engine.cpp, render() function:
   glClearColor(0.53f, 0.81f, 0.92f, 1.0f); // Should be sky blue
   ```

2. **Check Camera Position:**
   ```cpp
   // In Engine.cpp constructor:
   m_camera = std::make_unique<Camera>(glm::vec3(0.0f, 2.0f, 5.0f));
   // Camera should be at y=2, z=5 looking at origin
   ```

3. **Check Depth Test:**
   ```cpp
   // In Window.cpp or Engine.cpp init:
   glEnable(GL_DEPTH_TEST); // Must be enabled
   ```

4. **Verify Shader Compilation:**
   - Look for shader errors in console
   - Check that shaders compile successfully

---

### ❌ Cube is Wrong Color

**Symptoms:**
- Cube renders but isn't red

**Solution:**

Check shader uniform in `Engine.cpp`:
```cpp
// In render() function:
m_shader->setVec3("objectColor", glm::vec3(0.8f, 0.1f, 0.1f)); // RGB for red
```

Values should be approximately (0.8, 0.1, 0.1) for red.

---

### ❌ Camera Controls Don't Work

**Symptoms:**
- Right-click doesn't enable camera movement
- WASD doesn't move camera

**Solutions:**

1. **Click in Viewport:**
   - Make sure you're clicking inside the Viewport window, not other panels

2. **Check Input Mode:**
   ```cpp
   // In Engine::onMouseButton:
   glfwSetInputMode(m_window->getNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
   // Should hide cursor when right-clicking
   ```

3. **Verify Callbacks:**
   ```cpp
   // In Engine::init():
   m_window->setMouseButtonCallback([this](int btn, int action, int mods) { 
       onMouseButton(btn, action, mods); 
   });
   // Make sure all callbacks are set
   ```

---

### ❌ Application Crashes on Startup

**Solutions:**

1. **Run in Debug Mode:**
   ```powershell
   cd build
   cmake --build . --config Debug
   .\bin\Debug\ForgeEngine.exe
   ```
   Check console for error messages.

2. **Check Dependencies:**
   - Ensure all DLLs are in the same folder as .exe
   - Visual Studio should automatically copy needed DLLs

3. **Verify Graphics Drivers:**
   - Update to latest drivers
   - Check Event Viewer for crash details

---

## Performance Issues

### ⚠️ Low FPS / Stuttering

**Solutions:**

1. **Use Release Build:**
   ```powershell
   cmake --build . --config Release
   ```
   Debug builds are 3-5x slower.

2. **Check VSync:**
   ```cpp
   // In Window.cpp constructor:
   glfwSwapInterval(1); // 1 = enable VSync (60 FPS cap)
   glfwSwapInterval(0); // 0 = disable VSync (unlimited FPS)
   ```

3. **Update Graphics Drivers**

4. **Check Background Processes:**
   - Close unnecessary applications
   - Check Task Manager for high CPU/GPU usage

---

### ⚠️ High Memory Usage

**Solutions:**

1. Memory leaks in custom code - use smart pointers
2. Too many objects - implement LOD system
3. Large textures - compress and resize assets

---

## ImGui Issues

### ❌ ImGui Not Rendering

**Symptoms:**
- No UI visible
- No menu bar

**Solutions:**

1. **Check ImGui Initialization:**
   ```cpp
   // In Engine::initImGui():
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui_ImplGlfw_InitForOpenGL(window, true);
   ImGui_ImplOpenGL3_Init("#version 450");
   ```

2. **Check Rendering Order:**
   - ImGui should render AFTER scene
   - `ImGui::Render()` must be called
   - `ImGui_ImplOpenGL3_RenderDrawData()` must be called

---

### ❌ Viewport Not Updating

**Symptoms:**
- Viewport shows old frame or frozen image

**Solution:**

Check framebuffer binding in render loop:
```cpp
// Render to framebuffer
glBindFramebuffer(GL_FRAMEBUFFER, m_viewportFBO);
// ... render scene ...
glBindFramebuffer(GL_FRAMEBUFFER, 0);

// Display in ImGui
ImGui::Image((void*)(intptr_t)m_viewportTexture, viewportSize, 
             ImVec2(0, 1), ImVec2(1, 0)); // Note: flipped Y
```

---

## CMake Issues

### ❌ "CMake is not recognized"

**Solution:**

Add CMake to PATH:

1. Install CMake from cmake.org
2. During installation, select "Add CMake to system PATH"
3. OR manually add: `C:\Program Files\CMake\bin` to PATH
4. Restart PowerShell

---

### ❌ "Generator not found"

**Error Message:**
```
CMake Error: Could not create named generator Visual Studio 16 2019
```

**Solution:**

Specify generator manually:
```powershell
cmake -G "Visual Studio 17 2022" ..
# Or for MinGW:
cmake -G "MinGW Makefiles" ..
```

---

## Still Having Issues?

### Debug Checklist

- [ ] All dependencies in `external/` folder?
- [ ] GLAD generated for correct OpenGL version?
- [ ] Graphics drivers updated?
- [ ] Building in Release mode?
- [ ] All .dll files present with .exe?
- [ ] Antivirus not blocking?
- [ ] Running from correct directory?

### Get More Information

**Enable Verbose CMake:**
```powershell
cmake .. --debug-output
```

**Check OpenGL Info:**
Add to `Engine::init()`:
```cpp
std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
std::cout << "GPU: " << glGetString(GL_RENDERER) << std::endl;
```

**Enable All Warnings:**
Add to CMakeLists.txt:
```cmake
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()
```

---

## Contact Support

For Forgeworks Interactive team members:

1. Check this troubleshooting guide
2. Review documentation (BUILD.md, SETUP.md)
3. Search existing issues in repository
4. Contact development team with:
   - Error message (full text)
   - Build configuration (Debug/Release)
   - CMake version (`cmake --version`)
   - Compiler version
   - Steps to reproduce

---

**Last Updated**: November 25, 2025  
**Forgeworks Interactive Limited**
