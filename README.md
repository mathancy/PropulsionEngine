# Propulsion MK I

A lightweight 3D game engine and editor, written in C++20 on Vulkan, with **PropulsionScript**: a plain-English scripting language you can edit as text or as a node graph.

Developed by Forgeworks Interactive Limited. Released under the [MIT License](#license).

> [!WARNING]
> **For academic purposes only. This is not a production-ready tool.**
> Propulsion MK I is a learning and research project. It is incomplete, Windows-only, and changes without notice. Expect missing features, bugs and breaking changes. Don't use it to ship games or for any other production work.

## Contents

- [What it is](#what-it-is)
- [What it does](#what-it-does)
- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Running](#running)
- [PropulsionScript](#propulsionscript)
- [Tests](#tests)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [License](#license)

## What it is

Propulsion MK I is an experimental game engine built to explore how an engine and its tools fit together, from the Vulkan renderer up to the editor UI. It has two parts:

- **The engine and editor** (C++20, Vulkan, Dear ImGui). A desktop application where you import meshes, build materials, place objects in a scene and press Play to run them.
- **PropulsionScript** (Rust). A scripting language for game logic that reads like short English sentences instead of code. Scripts are plain `.prop` text files, and the editor can also show and edit any script as a node graph. The two views always stay in sync.

The goal is to study engine architecture and to test an idea: game scripting that non-programmers can read and write.

## What it does

- **Edit scenes.** A dockable editor with a 3D viewport, content browser, transform gizmos and undo/redo.
- **Render with Vulkan.** Lighting, shadows, ambient occlusion, HDRI and sky environments, selection outlines, and hardware ray tracing on GPUs that support it.
- **Import and author assets.** FBX mesh import, built-in primitives, and a node-based material editor.
- **Run game logic.** Objects run their PropulsionScript in Play mode through an interpreter. Edit a script, save and press Play again; no rebuild is needed.
- **Tool the language.** A command-line checker, formatter and runner (`propc`), a language server, and a VS Code extension.

## Features

**Editor**
- Dockable Dear ImGui interface with a 3D viewport, content browser, tabbed asset editors and undo/redo
- Fly camera, world grid, and move/rotate/scale gizmos
- Mesh import from FBX (via [ufbx](https://github.com/ufbx/ufbx)), plus built-in cube, sphere and cylinder primitives
- Node-based material editor
- Project save/load, with scripts attached to Object assets

**Rendering**
- Vulkan renderer with SPIR-V shaders compiled from the GLSL in `shaders/`
- Selection outline, thumbnail generation and SVG icon rendering (LunaSVG)

**Scripting**
- PropulsionScript (`.prop` files) runs in Play mode through an interpreter. Edit a script, save it, press Play. No rebuild is needed.
- A bidirectional node-graph editor for scripts: the graph and the text file always agree

## Requirements

- Windows 10/11 (the build and tooling are Windows-only for now)
- A GPU and driver with Vulkan support
- [Vulkan SDK](https://vulkan.lunarg.com/), including `glslc`. The installer sets `VULKAN_SDK`; restart your terminal afterwards.
- Visual Studio 2019 or newer with the "Desktop development with C++" workload
- [CMake](https://cmake.org/download/) 3.20 or newer
- [Rust](https://rustup.rs/) (stable), to build the PropulsionScript interpreter
- Git

## Building

**1. Clone with submodules**

```powershell
git clone --recurse-submodules https://github.com/mathancy/PropulsionEngine.git PropulsionMKI
cd PropulsionMKI
```

Already cloned without them? Run `git submodule update --init --recursive`.

Third-party libraries (GLFW, GLM, Dear ImGui, LunaSVG, stb) live in `external/` as submodules.

**2. Build the PropulsionScript interpreter**

The editor links against `propulsion_ffi.dll`, which CMake looks for in `PropulsionScript/target/release`. Configuring fails with a clear message if it is missing.

```powershell
cd PropulsionScript
cargo build --release -p propulsion-ffi
cd ..
```

To use a PropulsionScript checkout somewhere else, pass `-DPROPULSIONSCRIPT_DIR=<path>` to CMake.

**3. Compile the shaders**

```powershell
.\compile_shaders.ps1
```

This turns the GLSL files in `shaders/` into `.spv` files using `glslc`.

**4. Configure and build the engine**

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The executable is written to the repository root, not `build/`, and `propulsion_ffi.dll` is copied next to it.

`.\rebuild.ps1` does a clean Debug rebuild, including the shaders, in one step.

## Running

```powershell
.\PropulsionMKI.exe
```

Run it from the repository root so it can find `shaders/` and `resources/`.

## PropulsionScript

PropulsionScript is a controlled natural language for game logic: a fixed set of sentence patterns, with no braces or operator soup. A file has a `header:` section that declares what it uses and an `implement:` section that holds the behavior. Examples are in [PropulsionScript/examples/](PropulsionScript/examples/) and [PropulsionScript/behaviors/](PropulsionScript/behaviors/).

Scripts are plain files in a project's `Scripts/` folder. In the editor, open an Object asset and use its **Script** section to pick a script, create a starter, check it for errors, open it in your text editor, or open it as a node graph with **Edit Graph**. Each object placed from that asset runs its script in Play mode.

The PropulsionScript workspace ([PropulsionScript/](PropulsionScript/)) is a Cargo workspace:

| Crate | Purpose |
|-------|---------|
| `propulsion` | Lexer, parser, type checker, node graph model, and interpreter |
| `propc` | Command-line tool |
| `propulsion-ffi` | C interface to the interpreter, built as `propulsion_ffi.dll` for the editor |
| `propulsion-lsp` | Language server (diagnostics, hover, go to definition, rename, completion) |

`vscode-propulsionscript/` is a VS Code extension with syntax highlighting that uses the language server.

The `propc` tool:

```
propc <file.prop>... [--ast | --tokens]            check files
propc run <file.prop> [--frames N] [--press <input>]...   run one script headlessly
propc fmt <file.prop>... [--check | --write]       format
propc nodes <file.prop> [--json]                   show the file's node view
propc nodes --table [--json]                        list all node types
propc cpp <file.prop>... --out <dir> [--no-runtime]   generate C++
```

The full language reference is [PropulsionScript/SPEC.md](PropulsionScript/SPEC.md). Details on the C++ runtime and the engine integration are in [PropulsionScript/cpp/README.md](PropulsionScript/cpp/README.md).

Some of the engine systems that scripts can call (audio, UI, animation, physics, levels) are not implemented yet. Calls to them are written to the debug console.

## Tests

PropulsionScript (Rust):

```powershell
cd PropulsionScript
cargo test
```

Script graph editor (headless C++ test; not part of the default build):

```powershell
cd build
cmake --build . --target ScriptGraphTest --config Release
.\tests\ScriptGraphTest.exe ..\PropulsionScript
```

## Repository layout

```
PropulsionMKI/
├── src/              Engine and editor source
│   ├── core/         Window, camera, crash handler
│   ├── rendering/    Vulkan context, shaders, meshes, grid, thumbnails
│   ├── ui/           Content browser, asset editor, gizmos, tabs, theme
│   ├── nodegraph/    Node graph canvas shared by materials and scripts
│   ├── nodes/        Material node types
│   ├── scripting/    Play mode and the script graph editor
│   └── project/      Project serialization and undo
├── include/          Headers, mirroring src/
├── shaders/          GLSL sources and compiled SPIR-V
├── resources/        Fonts, icons, default meshes
├── PropulsionScript/      Scripting language: compiler, interpreter, LSP, VS Code extension
├── tests/            Script graph editor test
├── external/         Third-party libraries (submodules)
├── Documentation/    Design notes and guides
├── CMakeLists.txt
├── compile_shaders.ps1
└── rebuild.ps1
```

## Documentation

Guides and design notes are in [Documentation/](Documentation/); start with [Documentation/DOCS_INDEX.md](Documentation/DOCS_INDEX.md).

Some of these pages were written before the move from OpenGL to Vulkan and still mention OpenGL and GLAD. Where they disagree with this README or `CMakeLists.txt`, trust those.

## License

Copyright © 2025 Forgeworks Interactive Limited.

Released under the MIT License. See [LICENSE](LICENSE) for the full terms.

This project is provided for academic and educational purposes only. It is not a production-ready tool, and it comes with no warranty or support.

Third-party libraries in `external/` and `include/external/` keep their own licenses.
