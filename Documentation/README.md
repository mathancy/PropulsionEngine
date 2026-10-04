# Forge Engine

A modern, lightweight game engine built with OpenGL and C++, designed for rapid game development.

<div align="center">

**Developed by Forgeworks Interactive Limited**

**🚀 [START HERE](START_HERE.md) 🚀**

[Quick Start](QUICKSTART.md) • [Build Guide](BUILD.md) • [Architecture](ARCHITECTURE.md) • [Setup](SETUP.md) • [Troubleshooting](TROUBLESHOOTING.md)

</div>

---

## 🎮 About

Forge Engine is a proprietary 3D game engine built from the ground up for **Forgeworks Interactive Limited**. It combines the power of modern OpenGL with an intuitive, editor-driven workflow. Our goal is to create a versatile engine with visual scripting even simpler than Unreal Engine's Blueprints.

### Core Philosophy
- **Easy to Use** - Artist-friendly interface, minimal learning curve
- **High Performance** - Native C++ with modern OpenGL rendering
- **Visual Scripting** - Create game logic without writing code
- **Rapid Iteration** - Fast build times and hot-reload support

## ✨ Features

### Current (v1.0.0 - Foundation)
- ✅ **Modern OpenGL 4.5** - High-performance 3D rendering
- ✅ **Dear ImGui UI** - Professional, dockable interface with menu system
- ✅ **FPS Camera** - Smooth controls with WASD movement and mouse look
- ✅ **Shader System** - Runtime shader compilation with Phong lighting
- ✅ **Modular Architecture** - Clean, extensible codebase
- ✅ **CMake Build** - Cross-platform build configuration
- ✅ **ForgeScript** - Plain-English scripting (`.forge` files) with a bidirectional node-graph editor: scripts are
  picked per Object asset, run in Play mode, and can be edited as text or as nodes. See the language spec,
  `ForgeScript/SPEC.md`, and how many of the nodes in [NODELIST.md](NODELIST.md) it covers.

### Coming Soon
- 🔄 **Scene Management** - Hierarchy, multi-object support, transform gizmos
- 🔄 **Asset Pipeline** - Model/texture loading, material system
- 🔄 **Visual Scripting** - Mostly there (see ForgeScript above); still to come are the engine systems its nodes call
  (audio, UI, animation, physics, levels), which are logged to the console for now
- 🔄 **Physics Engine** - Collision detection and rigidbody dynamics
- 🔄 **Advanced Rendering** - PBR materials, shadows, post-processing

## 🚀 Quick Start

### Prerequisites

- **Windows 10/11**
- **Visual Studio 2019+** or MinGW
- **CMake 3.20+**
- **Git**
- **OpenGL 4.5+ capable GPU**

### Installation

**1. Run Setup Script**
```powershell
.\setup.ps1
```

**2. Setup GLAD (OpenGL Loader)**
- Visit https://glad.dav1d.de/
- Select: OpenGL 4.5, Core Profile
- Generate and download
- Extract to `external/glad/`

**3. Build**
```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

**4. Run**
```powershell
.\bin\Release\ForgeEngine.exe
```

📖 **Detailed instructions**: See [QUICKSTART.md](QUICKSTART.md) or [BUILD.md](BUILD.md)

## 🎯 Controls

| Input | Action |
|-------|--------|
| **Right Mouse + Move** | Look around (FPS-style) |
| **W / A / S / D** | Move forward / left / backward / right |
| **Q / E** | Move down / up |
| **Left Shift** (hold) | Move faster (2.5x speed) |
| **ESC** | Exit application |

### What You'll See

- 🔴 **Red cube** in the center of the scene
- 🌤️ **Sky blue background** (RGB: 135, 206, 235)
- 🖥️ **Viewport** showing the 3D scene
- 📋 **Menu bar** with File, Edit, Window, Help options

## 📁 Project Structure

```
ForgeEngine/
├── src/              # Engine source code (.cpp)
├── include/          # Engine headers (.h)
├── external/         # Third-party libraries
│   ├── glfw/        # Window management
│   ├── glad/        # OpenGL loader
│   ├── glm/         # Math library
│   └── imgui/       # UI framework
├── build/            # Build output (generated)
├── CMakeLists.txt    # Build configuration
├── setup.ps1         # Dependency setup
└── *.md              # Documentation
```

## 🛠️ Technology Stack

| Component | Technology | Purpose |
|-----------|-----------|---------|
| **Graphics API** | OpenGL 4.5 | 3D rendering |
| **Windowing** | GLFW 3.3 | Cross-platform windows |
| **UI Framework** | Dear ImGui (docking) | Editor interface |
| **Math** | GLM | Vector/matrix operations |
| **Loader** | GLAD | OpenGL function loading |
| **Build System** | CMake 3.20+ | Cross-platform builds |
| **Language** | C++17 | Core engine |

## 📚 Documentation

- **[QUICKSTART.md](QUICKSTART.md)** - Get up and running fast
- **[BUILD.md](BUILD.md)** - Complete build instructions
- **[SETUP.md](SETUP.md)** - Detailed dependency setup
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - Design docs & roadmap
- **[LICENSE](LICENSE)** - Proprietary license terms

## 🗺️ Roadmap

### Phase 1: Foundation ✅ (Current)
- Core rendering system
- Basic UI and camera controls

### Phase 2: Essential Tools (Next)
- Scene hierarchy
- Multi-object support
- Transform gizmos
- Asset loading (models, textures)

### Phase 3: Visual Scripting (largely done: ForgeScript)
- Node-based editor
- Event system
- Logic and math nodes

### Phase 4: Game Features
- Physics engine
- Audio system
- Advanced rendering (PBR, shadows)

See [ARCHITECTURE.md](ARCHITECTURE.md) for complete roadmap.

## 🤝 For Team Members

This engine is built **by and for** Forgeworks Interactive Limited employees and contractors.

### Contributing
1. Follow the existing code style
2. Document your changes
3. Test thoroughly
4. Update relevant documentation

### Getting Help
- Check documentation in the repository
- Contact the development team
- Review the architecture guide

## 📄 License

**Proprietary Software**  
Copyright © 2025 Forgeworks Interactive Limited. All rights reserved.

This software is confidential and proprietary. Licensed exclusively for use by Forgeworks Interactive Limited employees and contractors for company game development. Unauthorized use, copying, or distribution is prohibited.

See [LICENSE](LICENSE) for complete terms.

---

**Forgeworks Interactive Limited**  
*Forging new experiences, one hammer swing at a time.*
