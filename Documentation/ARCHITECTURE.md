# Propulsion MK I - Architecture & Roadmap

## Current Features (v1.0.0 - Foundation)

### ✓ Core Engine
- [x] OpenGL 4.5 rendering pipeline
- [x] Window management with GLFW
- [x] Camera system with FPS-style controls
- [x] Shader compilation and management
- [x] Basic mesh rendering (cube primitive)
- [x] Framebuffer for viewport rendering
- [x] Basic lighting (Phong shading model)

### ✓ User Interface
- [x] Dear ImGui integration
- [x] Dockable workspace layout
- [x] Menu bar (File, Edit, Window, Help)
- [x] Viewport window with scene rendering
- [x] Resizable viewport

### ✓ Input System
- [x] Mouse look (right-click + drag)
- [x] WASD movement
- [x] Q/E vertical movement
- [x] Shift speed modifier
- [x] ESC to exit

### ✓ Project Structure
- [x] CMake build system
- [x] Modular class architecture
- [x] Separation of concerns (Window, Camera, Shader, Mesh, Engine)
- [x] Easy-to-extend codebase

## Planned Features

### Phase 2: Essential Tools (Next Priority)

#### Scene Management
- [ ] Scene graph/hierarchy system
- [ ] Multiple objects in scene
- [ ] Object selection and manipulation
- [ ] Transform gizmos (move, rotate, scale)
- [ ] Scene saving/loading (JSON format)

#### Asset Pipeline
- [ ] Model loading (ASSIMP integration)
  - FBX, OBJ, GLTF support
- [ ] Texture loading (STB Image)
  - PNG, JPG, TGA support
- [ ] Material system
- [ ] Asset browser panel

#### Enhanced UI
- [ ] Hierarchy panel (scene tree)
- [ ] Inspector/Properties panel
- [ ] Console/Log window
- [ ] Asset browser panel
- [ ] Performance stats overlay

### Phase 3: Visual Scripting Foundation

#### Node-Based System (PropulsionScript: see `PropulsionScript/SPEC.md`)
- [x] Visual scripting graph editor (the text of a `.prop` script is the source of truth; the graph and the text edit each other)
- [x] Node creation and connection
- [x] Basic node types:
  - Events (Start, Update, Overlap, Hit, Damaged, variable changed)
  - Logic (If, Switch, And, Or, Not, Select)
  - Math (Add, Subtract, Multiply, Divide, Clamp, Map Range, Random, vectors, easing)
  - Variables (Get, Set, lists, enums, structs, data tables)
  - Transform (Position, Rotation, Scale, forward and right vectors, look-at)
  - Flow with memory (Do Once, Do N, Gate, FlipFlop, Timeline)
- [ ] The engine systems the nodes call: audio, UI, animation, physics (shapes, hit and overlap events), levels. Their calls are written to the console for now.

The nodes taken from Unreal's list are in [NODELIST.md](NODELIST.md); `PropulsionScript/SPEC.md` has a table of what each one is called in PropulsionScript.

#### Script Integration
- [x] Blueprint/script component for game objects (scripts belong to Object assets)
- [x] Variable inspector (the Variables panel; exposed variables)
- [x] Script compilation to C++ (`propc cpp`; tested against the interpreter, not yet packaged into a game)
- [x] Hot-reload support (Play reads the `.prop` files each time, so there is no rebuild)

### Phase 4: Game Features

#### Physics
- [ ] Physics engine integration (Bullet or PhysX)
- [ ] Rigidbody component
- [ ] Collider components (Box, Sphere, Mesh)
- [ ] Physics materials
- [ ] Raycast system

#### Rendering Enhancements
- [ ] PBR (Physically Based Rendering)
- [ ] Shadow mapping
- [ ] Post-processing effects
  - Bloom
  - HDR/Tone mapping
  - Antialiasing (FXAA/MSAA)
- [ ] Skybox support
- [ ] Particle systems

#### Audio
- [ ] Audio engine integration (OpenAL or FMOD)
- [ ] 3D spatial audio
- [ ] Audio components
- [ ] Music/SFX playback

### Phase 5: Production Tools

#### Project Management
- [ ] Project creation wizard
- [ ] Build/Export system
- [ ] Packaging for Windows
- [ ] Standalone game builds

#### Collaboration
- [ ] Version control integration
- [ ] Asset import/export tools
- [ ] Prefab system

#### Optimization
- [ ] Frustum culling
- [ ] LOD (Level of Detail) system
- [ ] Occlusion culling
- [ ] Batching/instancing

## Architecture Overview

### Current Class Structure

```
Propulsion::Engine (Main engine controller)
├── Window (GLFW window management)
├── Camera (View and projection)
├── Shader (GLSL program management)
└── Mesh (Geometry data)
```

### Planned Expansion

```
Propulsion::Engine
├── Window
├── Renderer
│   ├── RenderPipeline
│   ├── MaterialSystem
│   └── LightingSystem
├── Scene
│   ├── SceneGraph
│   ├── GameObject
│   └── Components
│       ├── Transform
│       ├── MeshRenderer
│       ├── Camera
│       ├── Light
│       ├── Rigidbody
│       └── ScriptComponent
├── AssetManager
│   ├── ModelLoader
│   ├── TextureLoader
│   └── ShaderLoader
├── ScriptSystem
│   ├── VisualScriptGraph
│   ├── NodeLibrary
│   └── ScriptCompiler
├── PhysicsEngine
│   ├── PhysicsWorld
│   └── CollisionSystem
├── AudioEngine
│   ├── AudioSource
│   └── AudioListener
└── UISystem
    ├── EditorUI
    └── GameUI
```

## Technical Goals

### Performance Targets
- 60 FPS minimum on mid-range hardware
- Support for 10,000+ objects in scene
- Sub-100ms load times for typical scenes
- Memory-efficient asset management

### Usability Goals
- Intuitive, artist-friendly interface
- One-click game deployment
- Visual scripting simpler than Blueprints
- Comprehensive documentation

### Code Quality
- Modern C++17 standards
- Well-documented codebase
- Unit tests for core systems
- Continuous integration

## Visual Scripting Vision

Propulsion MK I's visual scripting aims to be **even simpler** than Unreal's Blueprints:

### Key Principles
1. **Minimal Nodes** - Only essential operations
2. **Clear Flow** - Obvious execution order
3. **Type-Safe** - Compile-time error checking
4. **Auto-Complete** - Smart node suggestions
5. **Templates** - Common patterns pre-built

### Example Use Cases
- Character movement
- Door opening/closing
- Pickup collection
- Enemy AI behavior
- UI interactions
- Game state management

### Innovation Points
- **Natural Language** nodes ("Move Forward", not "AddActorLocalOffset")
- **Smart Connections** - Auto-convert compatible types
- **Inline Values** - Edit values directly on nodes
- **Minimap View** - Navigate large graphs easily
- **Group Nodes** - Create custom reusable blocks

## Development Priorities

### Immediate (Next 2-4 Weeks)
1. Scene hierarchy system
2. Multiple object support
3. Inspector panel
4. Object selection

### Short-term (1-2 Months)
1. Model loading
2. Texture support
3. Material system
4. Transform gizmos

### Mid-term (3-6 Months)
1. Visual scripting foundation
2. Basic node types
3. Script execution system
4. Physics integration

### Long-term (6-12 Months)
1. Complete visual scripting
2. Game export/packaging
3. Advanced rendering
4. Full production pipeline

## Technical Debt & Refactoring

### Known Issues to Address
- [ ] Separate render loop from game loop
- [ ] Implement resource management system
- [ ] Add comprehensive error handling
- [ ] Create abstraction layer for OpenGL
- [ ] Implement event system for decoupling

### Future Optimizations
- [ ] Multi-threaded rendering
- [ ] Job system for parallelism
- [ ] Memory pooling
- [ ] Custom allocators

## Comparison to Other Engines

### Propulsion vs Unreal Engine
- **Lighter** - Smaller executable size
- **Simpler** - Less complexity
- **Faster** - Quick iteration times
- **Focused** - Streamlined for specific use cases

### Propulsion vs Unity
- **Visual Scripting** - More intuitive
- **C++** - Better performance
- **Integrated** - No external scripting engine
- **In-house** - Full control over features

### Propulsion vs Godot
- **Purpose-built** - Tailored for Forgeworks
- **Specialized** - Optimized for company workflow
- **Support** - Direct internal support

## Contributing

### For Forgeworks Team Members

When adding features:
1. Follow existing code style
2. Document all public APIs
3. Add comments for complex logic
4. Test thoroughly before committing
5. Update this roadmap

### Code Style
- Use modern C++17 features
- Prefer RAII and smart pointers
- Keep classes focused and small
- Use const correctness
- Namespace everything under `Propulsion::`

## Resources for Developers

### Learning Materials
- **OpenGL**: learnopengl.com
- **Game Engine Architecture**: Book by Jason Gregory
- **Dear ImGui**: github.com/ocornut/imgui
- **CMake**: cmake.org/documentation

### Useful Tools
- **RenderDoc**: Graphics debugging
- **Visual Studio Profiler**: Performance analysis
- **Nsight**: NVIDIA GPU debugging
- **Git**: Version control

## License & Usage

MIT License. Copyright © 2025 Forgeworks Interactive Limited.

Provided for academic and educational purposes only; it is not a production-ready tool. See [LICENSE](../LICENSE) for the full terms.

---

## Version History

### v1.0.0 (2025-11-25) - Foundation
- Initial release
- Core rendering system
- Basic UI
- FPS camera controls
- Red cube demo scene

---

**Last Updated**: November 25, 2025  
**Maintained By**: Forgeworks Interactive Limited Development Team
