# Propulsion MK I - Setup Instructions

## Third-Party Dependencies

This project requires the following libraries to be placed in the `external/` directory:

### 1. GLFW (Window and Input Management)
- **Version**: 3.3 or higher
- **Download**: https://www.glfw.org/download.html
- **Location**: `external/glfw/`
- **License**: zlib/libpng

Clone or download GLFW:
```bash
cd external
git clone https://github.com/glfw/glfw.git
```

### 2. GLAD (OpenGL Loader)
- **Version**: OpenGL 4.5 Core
- **Generate**: https://glad.dav1d.de/
- **Location**: `external/glad/`
- **License**: MIT

Configuration for GLAD:
- Language: C/C++
- Specification: OpenGL
- API gl: Version 4.5 or higher
- Profile: Core
- Generate a loader: ✓

After generating, place the files:
- `glad/include/glad/glad.h`
- `glad/include/KHR/khrplatform.h`
- `glad/src/glad.c`

Or use the provided setup script.

### 3. GLM (OpenGL Mathematics)
- **Version**: 0.9.9.8 or higher
- **Download**: https://github.com/g-truc/glm
- **Location**: `external/glm/`
- **License**: MIT

Clone GLM:
```bash
cd external
git clone https://github.com/g-truc/glm.git
```

### 4. Dear ImGui (UI Library)
- **Version**: 1.89 or higher (docking branch recommended)
- **Download**: https://github.com/ocornut/imgui
- **Location**: `external/imgui/`
- **License**: MIT

Clone ImGui (docking branch):
```bash
cd external
git clone --branch docking https://github.com/ocornut/imgui.git
```

## Quick Setup (Windows)

Run the provided setup script:
```powershell
.\setup.ps1
```

This will automatically clone all dependencies into the correct directories.

## Manual Setup

If you prefer to set up manually:

1. Create the external directory:
```bash
mkdir external
cd external
```

2. Clone each dependency:
```bash
git clone https://github.com/glfw/glfw.git
git clone https://github.com/g-truc/glm.git
git clone --branch docking https://github.com/ocornut/imgui.git
```

3. Set up GLAD:
   - Visit https://glad.dav1d.de/
   - Select OpenGL 4.5, Core profile
   - Generate and download
   - Extract to `external/glad/`

## Verify Setup

Your `external/` directory should look like this:
```
external/
├── glfw/
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
├── glad/
│   ├── include/
│   │   ├── glad/
│   │   └── KHR/
│   └── src/
│       └── glad.c
├── glm/
│   └── glm/
│       ├── glm.hpp
│       └── ...
└── imgui/
    ├── imgui.h
    ├── imgui.cpp
    ├── backends/
    └── ...
```

## Building

After dependencies are set up:

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Troubleshooting

### CMake can't find dependencies
- Ensure all directories are in the correct locations
- Check that submodules are initialized if using git

### OpenGL version errors
- Update your graphics drivers
- Ensure your GPU supports OpenGL 4.5+

### ImGui linker errors
- Verify the docking branch is checked out
- Ensure `backends/` directory exists with GLFW and OpenGL3 implementations
