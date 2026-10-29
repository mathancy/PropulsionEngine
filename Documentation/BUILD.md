# Propulsion MK I Build Guide

## Complete Build Process

This guide walks you through building Propulsion MK I from scratch on Windows.

## Phase 1: Verify Prerequisites

### Required Software

1. **Visual Studio 2019 or newer** with C++ desktop development
   - Download from: https://visualstudio.microsoft.com/downloads/
   - During installation, select "Desktop development with C++"

2. **CMake 3.20+**
   - Download from: https://cmake.org/download/
   - During installation, select "Add CMake to system PATH"

3. **Git**
   - Download from: https://git-scm.com/download/win
   - Use default installation options

4. **OpenGL 4.5+ capable GPU**
   - Check by running: `wmic path win32_VideoController get name`
   - Update graphics drivers if needed

### Verify Installation

Open PowerShell and verify:

```powershell
cmake --version      # Should show 3.20 or higher
git --version        # Should show git version
```

## Phase 2: Setup Dependencies

### Automatic Setup (Recommended)

Run the provided setup script:

```powershell
cd G:\PropulsionMKI
.\setup.ps1
```

This will:
- Clone GLFW from GitHub
- Clone GLM from GitHub  
- Clone Dear ImGui (docking branch) from GitHub
- Provide instructions for GLAD setup

### Manual GLAD Setup

When prompted by the script:

1. Open browser to: **https://glad.dav1d.de/**

2. Fill out the web form:
   ```
   Language:       C/C++
   Specification:  OpenGL
   gl:             Version 4.5 (Core)
   Profile:        Core
   Options:        ☑ Generate a loader
   ```

3. Click the blue **"GENERATE"** button

4. Click **"glad.zip"** to download

5. Extract the ZIP file contents to create this structure:
   ```
   G:\PropulsionMKI\external\glad\
   ├── include\
   │   ├── glad\
   │   │   └── glad.h
   │   └── KHR\
   │       └── khrplatform.h
   └── src\
       └── glad.c
   ```

### Verify Dependencies

Your `external` folder should now contain:

```
external/
├── glfw/           ← Window management library
├── glad/           ← OpenGL function loader  
├── glm/            ← Math library for graphics
└── imgui/          ← Immediate mode GUI
```

Verify with:

```powershell
ls external
```

You should see all four folders listed.

## Phase 3: Build with CMake

### Generate Build Files

```powershell
# Create and enter build directory
mkdir build
cd build

# Generate Visual Studio solution
cmake ..
```

**Expected Output:**
```
-- Selecting Windows SDK version...
-- The CXX compiler identification is MSVC...
-- Configuring done
-- Generating done
-- Build files have been written to: G:/PropulsionMKI/build
```

### Compile the Project

```powershell
# Build Release configuration (recommended)
cmake --build . --config Release

# Or build Debug configuration
cmake --build . --config Debug
```

**Build Time:** First build takes 2-5 minutes depending on your system.

**Expected Output:**
```
Building Custom Rule G:/PropulsionMKI/CMakeLists.txt
...
Building CXX object CMakeFiles/PropulsionMKI.dir/src/main.cpp.obj
...
PropulsionMKI.vcxproj -> G:\PropulsionMKI\build\bin\Release\PropulsionMKI.exe
```

## Phase 4: Run the Engine

From build directory:

```powershell
.\bin\Release\PropulsionMKI.exe
```

Or from root directory:

```powershell
.\build\bin\Release\PropulsionMKI.exe
```

### First Run Checklist

When the engine starts, you should see:

- ✓ Window titled "Propulsion MK I" (1280x720)
- ✓ Menu bar with File, Edit, Window, Help
- ✓ Viewport panel showing 3D scene
- ✓ Red cube in the center
- ✓ Sky blue background (like a clear sky)
- ✓ Smooth camera movement with right-click + mouse

### Test Camera Controls

1. **Hold Right Mouse Button** in the viewport
2. **Move Mouse** to look around
3. **Press W/A/S/D** to move
4. **Press Q/E** to move down/up
5. **Hold Shift** while moving for speed boost

## Common Build Issues

### Issue: "GLAD files not found"

**Cause:** GLAD was not properly set up

**Solution:**
```powershell
# Verify GLAD structure
ls external\glad\include\glad\glad.h
ls external\glad\src\glad.c

# If files missing, re-download from glad.dav1d.de
```

### Issue: "Cannot open include file: 'GLFW/glfw3.h'"

**Cause:** GLFW not cloned correctly

**Solution:**
```powershell
cd external
git clone https://github.com/glfw/glfw.git
cd ..
# Rebuild
rm -r build; mkdir build; cd build; cmake ..; cmake --build . --config Release
```

### Issue: "LNK2019: unresolved external symbol"

**Cause:** Missing library or incorrect linkage

**Solution:**
- Clean build directory: `rm -r build`
- Re-run setup.ps1
- Verify all dependencies exist
- Rebuild from scratch

### Issue: "Failed to create GLFW window"

**Cause:** OpenGL version not supported

**Solution:**
1. Update graphics drivers
2. Check OpenGL support: Run `external\glfw\examples` to test
3. If GPU doesn't support 4.5, modify CMakeLists.txt to use OpenGL 3.3

### Issue: ImGui symbols undefined

**Cause:** ImGui docking branch not checked out

**Solution:**
```powershell
cd external\imgui
git checkout docking
git pull
cd ..\..
# Rebuild
cd build
cmake --build . --config Release
```

## Advanced Build Options

### Debug Build

For development with debugging symbols:

```powershell
cd build
cmake --build . --config Debug
.\bin\Debug\PropulsionMKI.exe
```

### Clean Build

To rebuild from scratch:

```powershell
rm -r build
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Build with MinGW

If you prefer MinGW over Visual Studio:

```powershell
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

## Build Performance Tips

1. **Use Release mode** - 3-5x faster than Debug
2. **Close other applications** during first build
3. **Use SSD** - Significantly faster compilation
4. **Parallel builds** - CMake uses all CPU cores by default

## Project Files Generated

After building, you'll have:

```
build/
├── bin/
│   └── Release/
│       └── PropulsionMKI.exe    ← Your executable
├── lib/                        ← Static libraries
├── CMakeFiles/                 ← CMake internals
└── PropulsionMKI.sln            ← Visual Studio solution (if using VS)
```

## Rebuilding After Changes

After modifying `.cpp` or `.h` files:

```powershell
cd build
cmake --build . --config Release
```

CMake only recompiles changed files, so subsequent builds are much faster.

## Development Workflow

**Recommended workflow:**

1. Open `build/PropulsionMKI.sln` in Visual Studio (optional)
2. Edit source files in `src/` or `include/`
3. Rebuild: `cd build; cmake --build . --config Release`
4. Run: `.\bin\Release\PropulsionMKI.exe`
5. Repeat

**Or use VS Code:**
1. Install C/C++ extension
2. Open PropulsionMKI folder
3. Edit files
4. Build with Terminal: `cd build; cmake --build . --config Release`

## Next Steps

✓ **Engine is built and running!**

Now you can:
- Explore the codebase in `src/` and `include/`
- Add new 3D objects to the scene
- Create custom shaders
- Build UI panels with ImGui
- Start implementing game features

See `QUICKSTART.md` for usage guide and controls.

---

**Forgeworks Interactive Limited**  
Technical Documentation © 2025
