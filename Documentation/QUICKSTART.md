# Forge Engine - Quick Start Guide

## Prerequisites

- **CMake** 3.20 or higher
- **Visual Studio 2019 or newer** (or MinGW-w64)
- **Git**
- **Graphics Card** with OpenGL 4.5+ support

## Setup Instructions

### Step 1: Run the Setup Script

Open PowerShell in the ForgeEngine directory and run:

```powershell
.\setup.ps1
```

This will automatically download:
- GLFW (window management)
- GLM (math library)
- Dear ImGui (UI library)

### Step 2: Set up GLAD (OpenGL Loader)

The script will prompt you to set up GLAD. Follow these steps:

1. **Visit**: https://glad.dav1d.de/
2. **Configure**:
   - Language: **C/C++**
   - Specification: **OpenGL**
   - API gl: **Version 4.5** (or 4.6 if your GPU supports it)
   - Profile: **Core**
   - Options: ☑ **Generate a loader**
3. Click **GENERATE**
4. Download the **glad.zip** file
5. Extract all contents to `G:\ForgeEngine\external\glad\`

After extraction, verify you have:
```
external/glad/
├── include/
│   ├── glad/
│   │   └── glad.h
│   └── KHR/
│       └── khrplatform.h
└── src/
    └── glad.c
```

### Step 3: Build the Project

```powershell
# Create build directory
mkdir build
cd build

# Generate Visual Studio project files
cmake ..

# Build the project (Release mode recommended)
cmake --build . --config Release
```

### Step 4: Run Forge Engine

```powershell
# From the build directory
.\bin\Release\ForgeEngine.exe

# Or from the root directory
.\build\bin\Release\ForgeEngine.exe
```

## Controls

Once the engine is running:

- **Right Mouse Button + Move Mouse** - Look around
- **W** - Move forward
- **S** - Move backward
- **A** - Move left
- **D** - Move right
- **Q** - Move down
- **E** - Move up
- **Left Shift** (while moving) - Move faster
- **ESC** - Exit

## Expected Result

You should see:
- A window titled "Forge Engine"
- Menu bar with File, Edit, Window, and Help menus
- A viewport showing a **red cube**
- **Sky blue background** (RGB: 135, 206, 235)
- Ability to fly around the scene like in an FPS game

## Troubleshooting

### "GLAD files not found" error
- Make sure you've generated and extracted GLAD to `external/glad/`
- Verify the folder structure matches the example above

### "OpenGL version not supported" error
- Update your graphics drivers
- Check if your GPU supports OpenGL 4.5+
- Try generating GLAD with OpenGL 3.3 instead (update CMakeLists.txt accordingly)

### CMake can't find dependencies
- Run `setup.ps1` again
- Ensure all folders exist in `external/`:
  - `glfw/`
  - `glad/`
  - `glm/`
  - `imgui/`

### Build errors in Visual Studio
- Make sure you're using Visual Studio 2019 or newer
- Try cleaning the build directory: `rm -r build; mkdir build`
- Rebuild from scratch

### ImGui linking errors
- Verify ImGui is cloned with the docking branch
- Check that `external/imgui/backends/` exists

## Project Structure

```
ForgeEngine/
├── external/          # Third-party libraries
│   ├── glfw/         # Window management
│   ├── glad/         # OpenGL loader
│   ├── glm/          # Math library
│   └── imgui/        # UI library
├── include/           # Engine headers
│   ├── Window.h
│   ├── Camera.h
│   ├── Shader.h
│   ├── Mesh.h
│   └── Engine.h
├── src/               # Engine implementation
│   ├── Window.cpp
│   ├── Camera.cpp
│   ├── Shader.cpp
│   ├── Mesh.cpp
│   ├── Engine.cpp
│   └── main.cpp
├── build/             # Build output (generated)
├── CMakeLists.txt     # Build configuration
├── setup.ps1          # Dependency setup script
├── README.md          # Project overview
├── QUICKSTART.md      # This file
├── SETUP.md           # Detailed setup guide
└── LICENSE            # Proprietary license
```

## Next Steps

Now that you have the engine running, you can:

1. **Explore the Code** - Check out the classes in `include/` and `src/`
2. **Add Objects** - Modify `Engine.cpp` to add more cubes or shapes
3. **Customize Shaders** - Edit the shader code in `Engine.cpp`
4. **Extend the UI** - Add panels and controls using Dear ImGui
5. **Build Features** - Start implementing the visual scripting system

## Development

To modify and rebuild:

```powershell
# Make your changes to .cpp or .h files

# Rebuild
cd build
cmake --build . --config Release

# Run
.\bin\Release\ForgeEngine.exe
```

## Support

For questions or issues:
- Review the `SETUP.md` for detailed dependency information
- Check the `README.md` for project overview
- Contact Forgeworks Interactive Limited development team

---

**Forgeworks Interactive Limited**  
Copyright © 2025. All rights reserved.
