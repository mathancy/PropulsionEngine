-------------------------------------------------------------------------------
# Propulsion MK I – Design Document
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
## 0. Index
-------------------------------------------------------------------------------

* **1. Vision & Design Principles**  --------------------------------------  30
* **2. User Experience Pillars**  -----------------------------------------  55
* **3. High-Level Architecture**  ----------------------------------------  300
* **4. Project & Asset Management**  -------------------------------------  557
* **5. Scene & Object Model**  -------------------------------------------  838
* **6. Visual Scripting / Logic Graph**  --------------------------------  1100
    * 6.9 C++ Generation System (Core Feature)
    * 6.10 Bidirectional Editing (Graph ↔ Code)
    * 6.11 Sync Conflict Resolution
    * 6.12 Advanced: Full Code Export
    * 6.13 Implementation Phases
* **7. Materials & Rendering**  -----------------------------------------  1491
* **8. Physics & Collisions**  ------------------------------------------  1848
* **9. Input System**  --------------------------------------------------  2110
* **10. Audio**  --------------------------------------------------------  2358
* **11. Animation**  ----------------------------------------------------  2607
* **12. UI (In-Game)**  -------------------------------------------------  2874
* **13. Play & Build Pipeline**  ----------------------------------------  3150
* **14. Extensibility & Advanced Use**  ---------------------------------  3391
* **15. Quality-of-Life & Polish**  -------------------------------------  3570





-------------------------------------------------------------------------------
## 1. Vision & Design Principles
-------------------------------------------------------------------------------

**Goal**
Propulsion MK I is a Vulkan-based, beginner-friendly 3D game engine with an Unreal-style editor and Blueprint-like visual scripting, but designed so *literally anyone* can pick it up and make something move in 5 minutes.

**Key principles:**

* **Lego, not low-level:** Nodes and tools read as plain language (“When Player presses [Space], move forward”), not engine jargon.
* **Safe by default:** Hard to break things; smart defaults everywhere.
* **Immediate feedback:** Changes show instantly in the viewport (WYSIWYG).
* **Progressive depth:** Simple on the surface, advanced users can dive into C++/shaders when they want.
* **Minimal ceremony:** Projects are just folders. Assets live on disk. No magical hidden formats.
* **No ceiling:** Visual scripting compiles to C++ that users can view, learn from, and modify. The visual graph is a stepping stone, not a limitation.

**Core Differentiator: Bidirectional Code Generation**

Unlike other visual scripting systems that interpret graphs at runtime, Propulsion MK I compiles visual logic to native C++ code. Users can:

* **View** the generated code to learn how their nodes translate to real programming.
* **Modify** the generated code directly, with changes tracked per-node.
* **Seamlessly switch** between visual and code editing as their skills grow.

This creates a genuine learning path from complete beginner to professional programmer, all within the same tool. See Section 6.9–6.13 for full details.

---









-------------------------------------------------------------------------------
## 2. User Experience Pillars
-------------------------------------------------------------------------------

This section defines how Propulsion MK I should *feel* to a new and returning user. It focuses on clarity, low friction, and immediate success.

### 2.1 Target Users

Propulsion MK I should be usable by:

* **Complete beginners**

  * No prior coding or engine experience.
  * Goal: "Make something move" or "place some objects" on day one.
* **Indie/hobby developers**

  * May know Unity/Unreal/Godot but want a cleaner, more opinionated workflow.
  * Goal: Rapid prototyping without wrestling with project setup.
* **Technical users**

  * Comfortable with C++ / graphics, but want a nice editor and visual tools.
  * Goal: Use visual tools for speed, drop to code for control.

All UX decisions should be evaluated against: *"Does this confuse a beginner?"* If yes, default to a simpler presentation and move the complexity into an "Advanced" foldout or separate screen.

---

### 2.2 First-Run Experience (Onboarding Flow)

**Goal:** A user who opens Propulsion MK I for the first time should:

1. Create a project in under 30 seconds.
2. Place an object and hit Play in under 2 minutes.
3. Make something move with basic logic in under 5–10 minutes.

**First-run flow:**

1. **Welcome Screen**

   * Options:

     * `New Project`
     * `Open Project`
     * `Try Example Project` (recommended for true beginners).
   * Short tagline: *"Build your first scene in minutes. No coding required."*

2. **New Project Wizard**

   * Minimal required fields:

     * Project Name (text field).
     * Location (folder picker, default to Documents/PropulsionMKI/ProjectName).
     * Template dropdown:

       * `Empty Project`
       * `First Person Playground`
       * `Third Person Template`
       * `Top-Down Prototype`
   * "Create" button creates folder structure and opens directly into the editor.

3. **Guided Overlay (Optional, but recommended)**

   * A non-intrusive overlay highlights key areas with short callouts:

     1. **Scene Viewport** – "This is your world. Right-click and drag to look around. Use WASD to fly." (or similar controls)
     2. **Scene Objects Panel** – "Everything in your scene is listed here. Click an object to edit it."
     3. **Content Browser** – "Here are your assets. Drag them into the scene to add them."
     4. **Details Panel** – "Change position, rotation, and properties here."
   * Each step has `Next` and `Skip Tour`.

4. **First Interaction Goal**

   * At the end of the tour, user is prompted: *"Try placing an object."*
   * Quick hint: "Drag the cube icon from the content browser into the scene." (Example template should include a simple `Cube` and `Floor` asset.)

---

### 2.3 Built-in Example Projects

**Purpose:** Give users ready-made sandboxes that show what the engine can do without any setup.

Each template should:

* Have a clean, minimal scene.
* Use consistent naming and folder structures.
* Demonstrate **one core gameplay pattern** plus **one simple logic graph**.

**2.3.1 First Person Playground**

* Small environment with a floor plane and a few simple shapes.
* Player can look around and walk.
* Logic graph example: `WASD movement` and `Space to jump`.
* Clear naming: `BP_Player_FirstPerson` (or `Obj_Player_FirstPerson` if using your object naming scheme).

**2.3.2 Third Person Template**

* Simple character mesh with idle/walk/run animations.
* Follow camera orbiting the character.
* Logic graph examples:

  * Character movement.
  * Simple interaction: walk into a trigger volume -> door opens.

**2.3.3 Top-Down Prototype**

* Top-down camera, small arena.
* Click-to-move or WASD movement.
* Logic graph examples:

  * Move to cursor.
  * Basic health variable and a damage test object.

Each example project should include a short **"Read Me" note** that appears as a small panel when the project is first opened: *"This template shows X. Open Obj_Player and look at the Logic tab to see how movement works."*

---

### 2.4 Plain-English Nodes & Copy Guidelines

**Goal:** If a user reads a node chain out loud, it should sound like a sentence describing the behavior.

**Design rules:**

* Node titles use **verb-first, natural language**:

  * `When Action is Pressed` instead of `InputActionEvent`.
  * `Move Object` instead of `AddMovementInput`.
* Inputs are labelled as blanks in a sentence:

  * `When [Object] presses [Key]`
  * `Move [Object] in [Direction] at [Speed]`
* Tooltips avoid engine jargon:

  * Bad: "Executes every frame with DeltaTime input."
  * Good: "This runs many times per second while the game is playing. Use it to update movement or animations."

**Example of clear node copy:**

* Node: **When Key is Pressed**

  * Description: "Runs once when the chosen key is pressed down."
  * Inputs: `Key` (default: Space)
  * Output: `On Pressed` execution pin.

* Node: **Move Object**

  * Description: "Moves an object in a direction at a certain speed."
  * Inputs:

    * `Target` (default: This Object)
    * `Direction` (options: Forward, Backward, Right, Left, Up, Down, Custom Vector)
    * `Speed` (number, default: 600)
    * `Use Gravity` (bool, default: true)

**Consistency:**

* Stick to a small set of verbs: *When, Set, Get, Move, Rotate, Play, Spawn, Destroy, Show, Hide*.
* Keep descriptions under 2 lines. If more is needed, link to docs.

---

### 2.5 No-Setup Flow & Smart Defaults

Propulsion MK I should assume sensible defaults so a user rarely needs to dive into settings.

**Defaults for a new project:**

* One default **Level** created automatically.
* Default **Player Object**:

  * Camera.
  * Basic movement logic already wired up (WASD + mouse look in 3D templates).
* Default **Lighting**:

  * One directional light, one sky/ambient light.
* Default **Floor**:

  * Simple plane at `Z = 0` so objects have a reference surface.

**Smart defaults for new Objects:**

* When user creates a **New Object**:

  * It automatically gets a Transform component.
  * If they add a Mesh, engine can suggest: "Add Collider and Rigidbody?" with a single checkbox.

**Play Button Behavior:**

* Single prominent **Play** button in the top bar.
* No configuration required to start:

  * Automatically uses current level.
  * Automatically uses default player unless user overrides.

---

### 2.6 Accessibility & UX Guidelines

Even early on, UX should consider accessibility:

* **Text Size & Contrast**

  * Editor text should be legible with strong contrast (dark theme/light text or vice versa).
  * Allow scaling UI (e.g., 75% / 100% / 125% / 150% zoom).

* **Color Usage**

  * Never rely on color alone to convey meaning (e.g., use icons or labels along with color for node states/errors).

* **Controls**

  * Support remapping editor shortcuts (later in roadmap).
  * Provide simple mouse+keyboard camera controls help overlay (`F1`): "Right mouse to look, WASD to fly, scroll to zoom."

---

### 2.7 Error Messaging & Guidance

**Philosophy:** The engine should not just say what went wrong, but how to fix it.

* Error messages and warnings should:

  * Use plain language.
  * Point to a specific object/graph/field.
  * Offer a suggested fix or a link to a short help note.

**Examples:**

* "This connection doesn’t make sense: the `Move Object` node has no Target. Connect an Object or leave it as `This Object`."
* "Your scene has no Player. The game will start, but you won’t be able to move. Add a Player Object or choose one in Project Settings → Gameplay."

**Visual cues:**

* Problem nodes outlined in a subtle warning color.
* Hovering over the warning icon shows the explanation and potential fix.

---









-------------------------------------------------------------------------------
## 3. High-Level Architecture
-------------------------------------------------------------------------------

This section describes how Propulsion MK I is structured under the hood: the main layers, modules, and data flow between them.

### 3.1 Architectural Overview

Propulsion MK I is organized into three main strata:

1. **Platform Layer**

   * OS, windowing, input, file system, timing, threads.
   * Provides a minimal, unified API to the engine core (e.g., `Platform::OpenWindow`, `Platform::GetTimeSeconds`).

2. **Engine Core**

   * Rendering, scene and object system, physics integration, audio, resource management, serialization.
   * Pure C++ with minimal direct OS calls (always through the platform layer).

3. **Editor Layer**

   * ImGui-based UI, tools, inspectors, graph editors, content browser.
   * Talks to the engine core through a clear API (e.g., "create object", "load scene", "set material value").

The same engine core is used for both **in-editor play** and **standalone game builds**.

---

### 3.2 Modules & Responsibilities

At a high level, the engine core is split into modules, each with a clear responsibility:

* **Core / Foundation**

  * Memory utilities (allocators, containers if needed), logging, math (vectors, matrices, quaternions), UUIDs/handles.

* **Platform**

  * Window creation, input polling, file I/O, timing, threads.
  * Abstracted behind interfaces so additional platforms (Linux, etc.) can be added later.

* **Renderer (Vulkan)**

  * [Implemented] Vulkan initialization and swapchain management.
  * Render graph / frame graph (longer-term design) for draw ordering and post-process.
  * Handles GPU resources: buffers, textures, pipelines, descriptor sets.

* **Scene & Object System**

  * Scene graph / world representation.
  * Objects (Actors) and Components.
  * Spatial partitioning (later) for culling.

* **Logic / Scripting System**

  * Visual graph representation.
  * Graph execution model.
  * C++ code generation and integration.

* **Physics**

  * Optional module.
  * Physics world, rigidbodies, colliders, raycasts.

* **Audio**

  * Audio device interface.
  * 2D/3D sound playback.

* **UI (In-Game)**

  * Simple immediate or retained-mode 2D UI.

* **Serialization**

  * [Implemented] Synchronous save/load for projects and scenes.
  * Handles binary/JSON/other format(s) for engine data.

* **Editor**

  * [Implemented] ImGui integration.
  * Panels: Scene Objects, Content Browser, Details/Inspector, Material Editor, Logic Graph Editor, Viewport, Console, etc.

Each module should be loosely coupled. For example, the Editor should not directly depend on Vulkan types; it should only see high-level engine APIs and resource handles.

---

### 3.3 Main Loop & Update Order

The main loop is responsible for pumping events, updating the world, and rendering frames. At a high level:

1. Poll platform events (window close, resize, input).
2. Calculate `DeltaTime`.
3. Update engine modules:

   * Input system (update action/axis states).
   * Logic graphs / game scripts.
   * Physics simulation.
   * Animation.
   * Audio updates.
4. Build render data for the current frame (visible objects, camera info, lights).
5. Submit frame to the renderer (Vulkan).
6. Present to screen.

**Editor vs Standalone:**

* In the **Editor**, there are two modes:

  * **Edit Mode**: Only editor UI runs; scene is updated minimally (no gameplay, or only selected systems like gizmos).
  * **Play Mode**: Full main loop runs with logic, physics, etc., while editor panels continue to render.

* In a **Standalone Build**, only the core loop runs without any ImGui editor panels.

Update order (simplified):

```text
Platform::PollEvents()
DeltaTime = Time::GetDelta()

if (GameIsPlaying)
    Input::Update()
    LogicSystem::Update(DeltaTime)
    Physics::StepSimulation(DeltaTime)
    Animation::Update(DeltaTime)
    Audio::Update(DeltaTime)

Renderer::RenderFrame()
Platform::Present()
```

The precise order is important for determinism and predictability, and should be documented so users know when events like "Begin Play" and "Tick" happen relative to physics and rendering.

---

### 3.4 Platform Abstraction

Propulsion MK I will initially target desktop (e.g., Windows), but the design should avoid hard-coding platform-specific details.

**Platform Layer Responsibilities:**

* Window creation & management.
* Vulkan surface creation.
* Input devices: keyboard, mouse (later gamepads).
* File system operations (open, read, write, enumerate files).
* Timing (high-resolution clocks, sleep/yield).
* Threads and synchronization primitives.

**Design Notes:**

* Expose a small set of platform functions in `Platform::*` namespace or via a `IPlatform` interface.
* Keep engine code free from direct system calls (e.g., `Win32` or `POSIX`) as much as possible.

---

### 3.5 Data Model: Objects, Components, and Scenes

Propulsion MK I’s core data model is **Scene → Objects → Components**.

* **Scene**

  * A collection of objects, plus high-level metadata (skybox, environment settings, navigation data, etc.).
  * Scenes are saved as separate assets (e.g., `Level_MainScene.pscene`).

* **Object (Actor)**

  * Has a unique ID.
  * Holds a Transform component by default.
  * Can own multiple components (Mesh Renderer, Collider, Audio Source, etc.).
  * May be flagged as a **Prefab instance** (later) for reuse across scenes.

* **Component**

  * Holds data & simple behavior tied to an object.
  * Examples:

    * Transform
    * Mesh Renderer
    * Light
    * Camera
    * Rigidbody
    * Collider
    * Script (Logic Graph owner / C++ link)

**Execution model:**

* During updates, systems iterate over entities with specific components:

  * Render system looks at all objects with `Transform + MeshRenderer`.
  * Physics system looks at `Transform + Rigidbody/Collider`.

This is essentially an ECS-like pattern, but you can keep the public-facing concept as "Objects with Components" to remain beginner-friendly.

---

### 3.6 Resource & Asset Management

**Goals:**

* Avoid loading the same asset multiple times.
* Provide stable handles that the editor and runtime can reference.
* Make save files robust even when assets move around on disk.

**Key Concepts:**

* **Asset Handle / ID**

  * Each asset (mesh, texture, material, etc.) gets a unique persistent ID.
  * Scene/Object data stores references by ID, not by file path only.

* **Asset Registry**

  * Central table that maps asset IDs to file paths and metadata.
  * Updated whenever files are imported, moved, or deleted via the Content Browser.

* **Resource Lifetime**

  * Editor can keep some assets hot-loaded (e.g., currently edited material, selected mesh).
  * Use reference counting or explicit lifetime management for GPU resources.

* **Import vs Source Files**

  * Initially, you can treat source files (e.g., `myMesh.fbx`) as the runtime assets.
  * Later, you may introduce imported/processed runtime formats (e.g., `.pmesh`, `.ptex`) for faster loading.

---

### 3.7 Build Configurations & Targets

Even early, it helps to define build configurations:

* **Editor Build**

  * Includes editor layer + engine core.
  * Contains ImGui, debug overlays, and all tools.

* **Game Build**

  * Stripped-down executable using the same engine core.
  * No ImGui editor, only in-game UI.

**Configurations:**

* `Debug` – full asserts, logging, debug symbols.
* `Release` – optimized, minimal logging.
* Later: `Development` – a middle ground with some debug features enabled.

---









-------------------------------------------------------------------------------
## 4. Project & Asset Management
-------------------------------------------------------------------------------

This section defines how projects are structured on disk, how assets are discovered and referenced, and how the editor interacts with the file system.

### 4.1 Project Structure & Metadata

**Goals:**

* Projects are just folders on disk.
* Users can move/copy projects like regular directories.
* Engine metadata is transparent and human-inspectable where possible.

**Default structure (example):**

```text
MyProject/
  Content/                 # User assets (source files)
    Meshes/
    Materials/
    Textures/
    Audio/
    Scenes/
    Objects/
  Propulsion/                   # Engine metadata & generated data
    Project.pproj      # Project settings & metadata
    AssetRegistry.pregistry    # Asset IDs ↔ file paths
    AutoSaves/             # Optional auto-save snapshots
    Cache/                 # Imported/processed asset data (optional, later)
```

#### 4.1.1 Project.pproj

A small metadata file that describes the project. Likely stored as a simple text-based format (e.g., JSON, TOML or a custom key/value format) for easy manual inspection.

**Core fields (initial):**

* `ProjectName`
* `EngineVersion`
* `DefaultScene` (asset ID or path)
* `DefaultPlayerObject` (object asset ID)
* `WindowWidth`, `WindowHeight`
* `TargetFrameRate` (optional)

Later fields:

* `InputMappings` (or reference to separate input config asset).
* `BuildSettings` (output path, icon, etc.).

#### 4.1.2 Asset Types & Extensions

This is the file format required for all Propulsion MK I specific files.

* Project Settings: `Propulsion/Project.pproj`
* Asset Registry: `Propulsion/AssetRegistry.pregistry`
* Scenes: `Content/Scenes/*.pscene`
* Objects: `Content/Objects/*.pobj`
* Materials: `Content/Materials/*.pmat`
* UI Canvases: `Content/UI/*.pui`
* Anim Controllers: `Content/Animations/*.fanimctrl`
* Audio: `Content/Audio/*.wav / *.ogg`
* Meshes: `Content/Meshes/*.fbx / *.obj`

---

### 4.2 Content Browser Behavior

The **Content Browser** is a visual representation of the `/Content` folder.

**Key rules:**

* 1:1 mapping between folders in the browser and folders on disk.
* Supported file types are recognized as assets (meshes, textures, audio, etc.).
* Non-recognized files can still be displayed (or optionally hidden) for transparency.

**Core interactions:**

* **Create Folder** – creates a subdirectory under the selected folder.
* **Rename** – renames the file/folder on disk and updates references via `AssetRegistry`.
* **Delete** – moves file to a trash area or deletes from disk (configurable), and removes from registry.
* **Duplicate** – copies the file and assigns a new asset ID.

**Opening assets (Unreal-like workflow):**

* **Double-click** on an asset in the Content Browser:

  * Opens the asset in its dedicated editor **in a new tab** within the main editor window.
  * Example:

    * Double-click `.pmat` → opens **Material Editor** tab.
    * Double-click `.pobj` → opens **Object Editor** tab (components + logic graph).
    * Double-click `.pscene` → opens the scene in a new **Scene tab** (or focuses it if already open).
* **Middle-click** on a tab closes that tab (optional but familiar to many users).
* Tabs are dockable and re-orderable, similar to Unreal’s layout system.

**Drag & Drop:**

* Dragging a mesh/material/object from Content into the viewport should:

  * Create a new scene object using that asset (mesh or prefab).
* Dragging assets between folders updates the file path and registry mapping.

**Tab persistence:**

* Recently opened assets remain in tabs until explicitly closed.
* On editor restart (later feature), the engine may optionally restore the previously open tabs.

---

### 4.3 Asset Types

Propulsion MK I will treat certain file types as specific asset types. This can be extended over time.

**Initial assets:**

* **Scenes** – `.pscene` (or similar custom extension)
* **Objects** – `.pobj`
* **Materials** – `.pmat`
* **Meshes** – source formats like `.fbx`, `.obj` (later: processed formats)
* **Textures** – `.png`, `.jpg`, `.tga` (later: compressed GPU-ready formats)
* **Audio** – `.wav`, `.ogg`
* **Logic Graphs** – always stored inside `.pobj` Object assets (no separate graph file type)

Each asset type has:

* A **type ID** (enum) – e.g., `Scene`, `Object`, `Material`, `Mesh`, `Texture`, `Audio`.
* A **default editor** – what panel opens when you double-click it.

---

### 4.4 Asset Registry

The **Asset Registry** is the central index that tracks all known assets and their IDs. It allows scenes and objects to reference assets without depending solely on file paths.

#### 4.4.1 Registry File

Stored in `Propulsion/AssetRegistry.pregistry` (format TBD: initial could be JSON or binary).

Each entry contains:

* `AssetID` (UUID or 64-bit hash).
* `RelativePath` (e.g., `Content/Meshes/Crate.fbx`).
* `Type` (Mesh, Texture, Material, etc.).
* Optional: `FriendlyName`.

Example (pseudo-JSON):

```json
{
  "assets": [
    {
      "id": "8f3c-...-1",
      "path": "Content/Meshes/Crate.fbx",
      "type": "Mesh"
    },
    {
      "id": "9b12-...-5",
      "path": "Content/Materials/Wood.pmat",
      "type": "Material"
    }
  ]
}
```

#### 4.4.2 Registry Lifecycle

* **On project load:**

  * Registry is loaded into memory.
  * Engine verifies that referenced files still exist (and can log warnings for missing assets).

* **On asset import:**

  * New asset ID is generated.
  * Entry added to registry.

* **On asset move/rename:**

  * File is moved/renamed on disk.
  * Registry entry path updated.

* **On asset delete:**

  * Entry removed from registry.
  * References may become "missing" and should be highlighted in scenes/objects.

---

### 4.5 Importing Assets

**Goal:** Drag-and-drop simplicity with sensible defaults.

**Import sources:**

* Dragging files from the OS (Explorer/Finder) into the Content Browser.
* Using `File → Import` menu.

**Import pipeline (simplified):**

1. User drags a file into a folder in Content.
2. Engine copies the file into the project’s `Content` directory.
3. Engine detects the file type and:

   * Creates or updates an entry in the Asset Registry.
   * Optionally runs a pre-processing step (e.g., generate thumbnails, basic mesh processing).
4. Content Browser shows the new asset with a preview icon.

**Default settings per type:**

* Meshes:

  * Default scale (e.g., 1.0 or project scale).
  * Combine meshes or keep separate (later setting).

* Textures:

  * Default sRGB flag for color textures.
  * Default filtering/wrapping modes.

* Audio:

  * Default 2D/3D flag.
  * Default volume.

Each type can later get an **Import Settings** panel for more control.

---

### 4.6 Scenes & Saving

Scenes represent the placed objects and their configuration.

**Scene file contains:**

* List of objects with:

  * Object ID.
  * Object type / link to base object asset (if prefab).
  * Transform data (position, rotation, scale).
  * Component overrides (e.g., material overrides on mesh renderer).
* Scene-level settings:

  * Sky/ambient light settings.
  * Environment settings.

**Saving behavior:**

* [Implemented] Synchronous saving of project state.
* When user hits `Ctrl+S` or `File → Save`:

  * Current scene data is serialized.
  * Any modified assets (materials, objects, graphs) are serialized.

**Auto-save (planned):**

* Periodically saves a snapshot of open scenes and key assets.
* Stored under `Propulsion/AutoSaves/` with timestamps.

---

### 4.7 Versioning & Backups (Future Considerations)

Even if not implemented immediately, the design should make it easy to add:

* **Per-asset history** – simple rollback for materials/objects.
* **Scene snapshots** – bookmark certain states ("Before experiment", "After lighting changes").
* **External VCS integration** – projects should play nicely with Git/Perforce/etc. thanks to clear text/binary formats.

For now, the minimal requirement is that project files are stable and safe to commit to a version control system without unexpected noise or non-deterministic changes.

---









-------------------------------------------------------------------------------
## 5. Scene & Object Model
-------------------------------------------------------------------------------

This section defines how levels (Scenes) are structured, how Objects live inside them, and how that maps to files on disk.

### 5.1 Scene System

**Concept:** A **Scene** is a level or map – a collection of placed Objects, plus some global settings (lighting, environment, etc.). At runtime, one or more Scenes can be loaded.

**Key properties:**

* Each Scene is saved as a `.pscene` asset in `Content/Scenes/` (or subfolders).
* A Scene references **Objects** by asset ID (for prefab-like usage) or contains **inline Objects** (for one-off scene-only actors).
* The editor can have multiple Scenes open, each in its own tab.

#### 5.1.1 Scene Contents

A Scene file contains:

* **Scene Metadata**

  * Scene name.
  * Unique Scene ID.
  * Optional description.

* **Scene Settings**

  * Sky/ambient light configuration.
  * Fog settings.
  * Default environment map / skybox.
  * Optional navigation/pathfinding data (future).

* **Placed Objects** (Scene Actors)
  For each placed object instance:

  * `InstanceID` – unique per-scene identifier.
  * `BaseObjectID` – reference to a `.pobj` asset (can be `null` for pure inline objects).
  * `Transform` – position, rotation, scale.
  * `ComponentOverrides` – any overrides applied on top of the base object definition (e.g., different material on one mesh).
  * `Name` – user-facing name (shown in Scene Objects panel).
  * `EditorState` – optional data like visibility toggles, lock state, selection history.

#### 5.1.2 Scene Objects Panel (Scene Outliner)

The **Scene Objects** panel shows a hierarchical list of all placed objects in the current Scene.

* Each entry corresponds to a placed object instance (not the `.pobj` asset itself).
* Display format:

  * Name (e.g., `Player`, `Door_01`, `Light_Sun`), with small type icon.
  * Icons for visibility and lock state.
* Hierarchy support (planned):

  * Parent/child relationships for grouping (e.g., attach a camera to a player).

**Core interactions:**

* Click to select an object → updates viewport selection and Details panel.
* Double-click to focus camera on that object.
* Right-click menu:

  * Rename.
  * Duplicate instance.
  * Delete from Scene.
  * "Open Base Object" → opens the `.pobj` defining its behavior.

#### 5.1.3 Multiple Scenes & Loading

* **Editor support (planned):**

  * Open multiple Scenes in tabs (e.g., `MainMenu.pscene`, `Level01.pscene`).
  * Only one Scene is "active" for Play in Editor at a time (configurable in Project Settings or via a dropdown).

* **Runtime loading (future):**

  * `SceneManager` can:

    * Load a Scene by asset ID.
    * Additively load a Scene (for streaming or modular level design).
    * Unload a Scene while keeping others.

---

### 5.2 Objects (Blueprint-Equivalent)

**Concept:** An **Object** (`.pobj`) is a reusable definition of an Actor: the components it has and the logic that drives it. Scenes place instances of Objects.

Current implementation:

* [Implemented] Basic **Object** that can:

  * Add a mesh.
  * Assign a material.

Target design:

* **Object = Actor Definition = Components + Logic Graph + Defaults**

  * `.pobj` lives in `Content/Objects/`.
  * Edited in a dedicated **Object Editor** tab.

#### 5.2.1 `.pobj` File Structure (Conceptual)

A `.pobj` file contains at minimum:

* **Metadata**

  * `ObjectID` (unique persistent ID).
  * `Name`.
  * Optional `Category` / folder grouping.

* **Components**

  * List of components with their types and properties (see 5.3 below).

* **Logic Graph**

  * Embedded visual script definition for this Object.
  * Saved as a list of nodes, pins, connections, and variables.

This makes `.pobj` the single source of truth for that actor type.

#### 5.2.2 Instancing & Overrides

When a `.pobj` is placed in a Scene:

* The Scene stores an **instance** referencing `BaseObjectID`.
* By default, the instance inherits all components and default values from the `.pobj`.
* The Scene entry can store **per-instance overrides**, for example:

  * Different Transform (position/rotation/scale).
  * Different material on a Mesh Renderer.
  * Different starting health value.

Design guideline:

* Keep overrides simple at first (Transform + a small set of exposed variables).
* Later, support "Exposed Properties" in `.pobj` that are intentionally overridable, similar to public variables.

---

### 5.3 Components

Components are the building blocks that give an Object its behavior and representation.

Initial planned components:

* **TransformComponent**

  * Position (Vector3).
  * Rotation (Quaternion or Euler, but displayed simply for users).
  * Scale (Vector3).
  * Always present; cannot be removed.

* **MeshRendererComponent**

  * `Mesh` (asset ID reference).
  * `Material` (single or array of `.pmat` asset IDs).
  * `CastShadows` (bool).
  * `Visible` (bool).

* **ColliderComponent**

  * `Shape` (Box, Sphere, Capsule, Mesh).
  * Shape parameters (size, radius, height, etc.).
  * `IsTrigger` (bool).

* **RigidbodyComponent**

  * `BodyType` (Static, Dynamic, Kinematic).
  * `Mass`.
  * `UseGravity`.
  * `Constraints` (lock position/rotation on axes).

* **AudioSourceComponent**

  * `Clip` (audio asset ID).
  * `Volume`.
  * `Loop` (bool).
  * `3D` (bool) and attenuation settings.

* **CameraComponent**

  * `FieldOfView`.
  * `NearPlane`, `FarPlane`.
  * `ProjectionType` (Perspective/Orthographic).

* **LightComponent**

  * `Type` (Directional, Point, Spot).
  * `Color`.
  * `Intensity`.
  * `Range` / `Angle` (for point/spot).
  * `CastsShadows`.

* **ScriptComponent**

  * Link to the embedded logic graph (for this `.pobj`).
  * Potential link to generated C++ class/backend.

Each component has a **Details** UI representation in the Object Editor and in the Scene instance Details panel, exposing only the most important fields by default, with advanced settings hidden in foldouts.

---

### 5.4 Object Editor (Layout & UX)

When the user double-clicks a `.pobj` in the Content Browser, the **Object Editor** opens in a new tab.

**Layout (conceptual):**

* **Left:** Component list & hierarchy

  * Shows all components on the Object.
  * `+ Add Component` button to attach new components.

* **Center:** Preview viewport

  * Renders the Object in isolation (with simple environment lighting and a ground plane).
  * Supports orbit/pan/zoom.

* **Right:** Details panel

  * Shows properties of the selected component.

* **Bottom or separate tab:** Logic Graph

  * Visual scripting canvas for this Object.
  * Users can switch between "Components" and "Logic" views, or see them side-by-side if desired.

This mirrors Unreal’s Blueprint editor conceptually, but with a tighter, simplified layout.

---

### 5.5 Prefabs & Inline Objects (Future)

**Prefabs (based on `.pobj`):**

* A `.pobj` is, in effect, a **prefab definition**.
* Any Scene instance that references a `.pobj` will receive changes when the `.pobj` is edited (except for per-instance overrides).

**Inline Objects (Scene-only):**

* The editor may also support creating **inline objects** directly in a Scene without backing `.pobj` assets.
* These are stored fully inside the `.pscene` file.
* Intended for one-off things that do not need reuse.

**Guidance for UX:**

* For beginners, steer them towards `.pobj`-based Objects when they click "Create New Object" in the Content Browser.
* Inline objects might be an advanced/optional feature.

---









-------------------------------------------------------------------------------
## 6. Visual Scripting / Logic Graph
-------------------------------------------------------------------------------

This section defines how the visual scripting system works: data structures, execution model, editor UX, and how it maps to generated C++.

### 6.1 Goals

* Looks like Blueprint, but simplified to plain English and “lanes” of logic.
* Compiles to C++ under the hood:

  * Each Object has **one primary Graph** that becomes a C++-backed behavior.
* Designed so a non-programmer can read it like a flowchart.
* Keeps **instance state** (variables) per placed object, while sharing the same graph logic across all instances.

---

### 6.2 Core Concepts

* **Graph**

  * Owned by a `.pobj`.
  * Contains Nodes, Pins, Connections, and Variables.

* **Node**

  * A single unit of logic (event, action, math op, etc.).
  * Has input and output pins (execution pins and data pins).

* **Pins**

  * **Execution pins** (flow)

    * Represent the order of operations (e.g., `On Pressed` → `Then` → next node).
  * **Data pins** (values)

    * Carry numbers, booleans, vectors, colors, object references, etc.

* **Variables**

  * Named values stored per object instance.
  * Can be read/written by nodes.

* **Instance Context**

  * When a Scene places a `.pobj`, each instance gets its own context for:

    * Variable values.
    * Internal timers / latent actions (e.g., Delay).

---

### 6.3 Data Model (Internal Representation)

At save-time inside `.pobj`, the graph can be represented conceptually as:

* **Graph**

  * `GraphID` (local ID inside `.pobj`).
  * List of `Nodes`.
  * List of `Variables`.

* **Node**

  * `NodeID` (unique within the graph).
  * `NodeTypeID` (e.g., `Event_BeginPlay`, `Event_KeyPressed`, `Action_MoveObject`, `Math_AddFloat`).
  * `Position` (for editor layout: x, y in canvas space).
  * `Properties` (per-node configuration, e.g., selected Key, chosen Direction).
  * `InputPins` / `OutputPins`.

* **Pin**

  * `PinID` (unique within the node).
  * `Name`.
  * `PinKind` (Execution or Data).
  * `DataType` (for Data pins: Number, Bool, Vector, Color, ObjectRef, etc.).
  * `DefaultValue` (if nothing connected).

* **Connection**

  * `FromNodeID`, `FromPinID`.
  * `ToNodeID`, `ToPinID`.
  * Execution connections must be Exec→Exec; data connections must be compatible types.

* **Variable**

  * `Name`.
  * `DataType`.
  * `DefaultValue`.
  * Flags:

    * `IsExposed` (visible/overridable per instance in Scene).

This structure is serialized inside the `.pobj` alongside components.

---

### 6.4 Node Categories (Expanded)

Node types are grouped to keep the palette simple and discoverable.

#### 6.4.1 Event Nodes

* **On Begin Play**

  * Runs once when the object is first spawned/activated in the Scene.

* **On Tick**

  * Runs every frame while the game is playing.
  * Properties:

    * `UseInterval` (bool).
    * `IntervalSeconds` (for simple "every X seconds" updates).

* **When Key is Pressed**

  * Runs once when a specific key is pressed.
  * Properties:

    * `Key` (default Space).

* **When Action is Pressed** (integrated with Input System)

  * Uses named input actions (e.g., "Jump").

* **On Overlap Begin / End**

  * Triggered when this object’s collider overlaps another collider.

* **On Variable Changed**

  * Triggered when a specific variable’s value changes.

#### 6.4.2 Action Nodes

* **Move Object**

  * Inputs:

    * `Target` (default: This Object).
    * `Direction` (Forward/Backward/Right/Left/Up/Down/Custom Vector).
    * `Speed` (Number).
    * `UseGravity` (Bool).

* **Rotate Object**

  * Inputs:

    * `Target`.
    * `Axis` / `Rotation`.
    * `Speed` / `Degrees`.

* **Set Location / Set Rotation / Set Scale**

* **Spawn Object**

  * Inputs:

    * `ObjectType` (`.pobj` asset reference).
    * `Location`, `Rotation`.

* **Destroy Object**

  * Inputs:

    * `Target`.

* **Play Sound**

  * Inputs:

    * `Sound`.
    * `Volume`.
    * `Location` (optional for 3D).

* **Play Animation** (later, when animation is implemented).

#### 6.4.3 Variables & Flow

* **Set Variable / Get Variable**

  * Operates on variables defined in the graph.

* **Branch (If)**

  * `Condition` (Bool).
  * Exec outputs: `True`, `False`.

* **Sequence**

  * Multiple exec outputs: `Then 0`, `Then 1`, `Then 2`.

* **Loop (Simple)**

  * `For i from 0 to Count-1` with `Index` output and `LoopBody` exec.

#### 6.4.4 Math & Utility

* **Math (Numbers)**

  * Add/Subtract/Multiply/Divide (Number).
  * Clamp.
  * Lerp (Number).

* **Math (Vectors)**

  * Add/Subtract.
  * Scale Vector (Vector * Number).

* **Utility**

  * **Print Debug Message** (with optional duration & screen/world location).
  * **Delay**

    * Pauses execution on a branch for X seconds, then continues.

The goal is to ship with a **small but powerful core set** and expand based on real needs.

---

### 6.5 Execution Model

**Per-Instance Execution:**

* Each Scene instance of a `.pobj` runs its own copy of the graph logic (sharing the graph definition but with separate state).
* Instance state includes:

  * Variable values.
  * Active timers (Delays).
  * Any in-progress latent actions.

**Event-driven:**

* Graph execution begins from **Event nodes**:

  * `On Begin Play` fires once when the instance is created/activated.
  * `On Tick` fires each frame while the game is running.
  * Input and overlap events fire based on engine events.

**Tick integration:**

* During `LogicSystem::Update(DeltaTime)`:

  1. For each active Scene instance of each Object:

     * Collect all pending events (BeginPlay, Tick, Input events, Overlaps, Variable changes).
     * Execute the associated event entry points.
  2. Process latent actions (e.g., `Delay`), decrement timers and trigger their continuations.

**Execution safety:**

* Guardrails for beginners:

  * Detect and warn about potential infinite loops.
  * Timeout overly-long execution chains in debug builds with a clear error.

---

### 6.6 Variables & Exposed Properties

Variables provide state for each object instance.

**Definition:**

* Defined at the graph level.
* Types: Number, Boolean, Vector, Color, Object Reference, etc.
* Default values stored in `.pobj`.

**Instance Values:**

* When an object is instantiated in a Scene:

  * Variables are initialized from defaults.
  * If `IsExposed` is true, the Scene instance can override that value.

**Exposed Variables in the Editor:**

* In the Object Editor (for `.pobj`):

  * Variables panel lists all graph variables.
  * Each variable has a checkbox `Expose to Instance`.

* In the Scene Details panel (for an instance):

  * Exposed variables appear as editable fields under a section like `Object Properties`.

This gives a simple, Unity-like "public fields" pattern without needing to teach scripting.

---

### 6.7 Editor UX for Graphs

**Opening the Graph:**

* Double-click a `.pobj` → Object Editor tab.
* Click the `Logic` tab or button to open the graph canvas for that Object.

**Canvas Basics:**

* Pan with middle mouse (or right mouse + modifier).
* Zoom with scroll wheel.
* Nodes can be dragged around; positions are saved in `.pobj`.

**Adding Nodes:**

* Right-click context menu on canvas or drag from a Node Palette panel.
* Search box for quick filtering (e.g., type "move" to see `Move Object`).

**Connections:**

* Click and drag from an output pin to an input pin.
* Incompatible types show a visual warning and do not connect.

**Node Wizards / Recipes:**

* Clicking `Add Logic` on a new Object offers a few starter recipes:

  * "Move with WASD".
  * "Rotate towards mouse".
  * "Open door when player enters area".
* Selecting a recipe auto-creates a small subgraph wired to typical components.

**Error Display:**

* Invalid or broken connections marked with a subtle warning icon.
* Hovering shows a plain-English explanation:

  * e.g., "This node needs a Target. Connect an Object or leave it as This Object."

---

### 6.8 Debugging & Visualization (Planned)

To help beginners understand what’s happening:

* **Live Execution Highlights**

  * While the game is playing in the Editor, nodes that are executing briefly highlight or pulse.
  * Execution wires can flash or animate when triggered.

* **Watch Values**

  * Users can right-click a variable or pin and choose "Watch".
  * A small panel shows current values per selected instance.

* **Breakpoints (Advanced)**

  * Ability to mark a node as a breakpoint in Debug builds.
  * When hit, pauses the game and focuses the graph.

---

### 6.9 C++ Generation System (Core Feature)

The C++ generation system is a **foundational differentiator** for Propulsion MK I. Unlike other visual scripting systems that interpret graphs at runtime, Propulsion MK I compiles visual logic to native C++ code. This provides:

* **Performance:** Native compiled code, not interpreted.
* **Learning path:** Users can see exactly what their nodes produce.
* **Progressive mastery:** Advanced users can modify generated code directly.
* **No ceiling:** Visual scripting is a stepping stone, not a limitation.

#### 6.9.1 Generation Model

Each `.pobj` with a logic graph generates a corresponding C++ class:

**File structure:**

```text
Content/Objects/
  Player.pobj              # Visual graph (source of truth)
  Generated/
    FG_Player.generated.h   # Auto-generated header
    FG_Player.generated.cpp # Auto-generated implementation
```

**Generated code quality:**

The generator produces **educational-quality, readable code** - not optimized spaghetti. Each node maps to clearly commented code:

```cpp
// ═══════════════════════════════════════════════════════════════
// FG_Player.generated.cpp
// Generated from: Content/Objects/Player.pobj
// ═══════════════════════════════════════════════════════════════

#include "FG_Player.generated.h"

void FG_Player::OnBeginPlay() {
    // ─── Node: "On Begin Play" (ID: 1) ───
    
    // ─── Node: "Set Variable [Health]" (ID: 2) ───
    Health = 100.0f;
}

void FG_Player::OnTick(float DeltaTime) {
    // ─── Node: "On Tick" (ID: 3) ───
    
    // ─── Node: "Get Axis [MoveForward]" (ID: 4) ───
    float MoveForward = GetAxis("MoveForward");
    
    // ─── Node: "Get Axis [MoveRight]" (ID: 5) ───
    float MoveRight = GetAxis("MoveRight");
    
    // ─── Node: "Move Object" (ID: 6) ───
    glm::vec3 Direction = GetForwardVector() * MoveForward + GetRightVector() * MoveRight;
    MoveObject(this, Direction, 500.0f);
}

void FG_Player::OnKeyPressed_Space() {
    // ─── Node: "When Key Pressed [Space]" (ID: 7) ───
    
    // ─── Node: "Play Sound" (ID: 8) ───
    PlaySound(JumpSound, 1.0f);
    
    // ─── Node: "Apply Force [Up]" (ID: 9) ───
    ApplyForce(this, glm::vec3(0, 1, 0), 800.0f);
}
```

**Runtime binding:**

* The Object system maintains a registry mapping `.pobj` asset IDs to generated class types.
* Scene instances create instances of the generated C++ class.
* Per-instance state (variables) is held by each instance.

---

### 6.10 Bidirectional Editing (Graph ↔ Code)

This is what makes Propulsion MK I revolutionary: **seamless bidirectional editing** between visual graphs and C++ code.

#### 6.10.1 The Code View

Every Object Editor has a **Code View** toggle button (`</>`) in the toolbar:

```text
┌─────────────────────────────────────────────────────────────────────────┐
│ Player.pobj                                    [Graph] [</>]  [▶ Play] │
├─────────────────────────────────────────────────────────────────────────┤
```

Clicking `</>` opens the integrated IDE showing the generated code for this object.

**IDE Features:**

* Syntax highlighting for C++.
* Line numbers with node ID annotations.
* Click-to-navigate: clicking a line highlights the corresponding node in Graph view.
* Reverse navigation: selecting a node in Graph view scrolls to its code.
* Inline error/warning markers.
* Autocomplete for engine APIs (future).

**Editor choice:**

The built-in IDE will be based on a proven embeddable editor component. Monaco (VS Code's editor) is the gold standard for this, providing syntax highlighting, autocomplete, and familiar keybindings. Legal considerations for Monaco (MIT licensed) should be verified, but it is permissively licensed for embedding.

#### 6.10.2 Live Modification Detection

The moment a user types **a single character** in the Code View, Propulsion MK I:

1. Parses which node's code block was modified (using the `// ─── Node: ... (ID: X) ───` markers).
2. Immediately marks that node as **"Modified"** in the graph.

**Visual indicators for modified nodes:**

* A small `</>` badge appears on the node's corner.
* The node header gains a subtle accent color (e.g., a blue tint).
* Tooltip shows: *"This node has custom code modifications."*

```text
┌─────────────────────────────┐
│ ◈ Move Object         </> │  ← Badge indicates custom code
├─────────────────────────────┤
│ ▶ Exec In        Exec Out ▶│
│ ○ Target                    │
│ ○ Direction                 │
│ ○ Speed                     │
└─────────────────────────────┘
```

This feedback is **instant** - users see the badge appear as they type.

#### 6.10.3 Modified Node Behavior

When a node has custom code:

* **Graph inputs become read-only** (greyed out with lock icon).
* **Double-clicking the node** opens Code View scrolled to that node's code.
* **A "Revert to Generated" option** appears in the node's right-click menu.
* **The node can still be moved/deleted** in the graph, but changing its inputs has no effect until reverted.

**Revert behavior:**

* "Revert to Generated" discards custom code and regenerates from node inputs.
* A confirmation dialog warns: *"This will discard your custom code changes. Continue?"*
* After reverting, the node returns to normal visual appearance and editable inputs.

#### 6.10.4 Data Model for Custom Code

The `.pobj` file stores custom code per-node:

```json
{
  "graph": {
    "nodes": [
      {
        "id": 6,
        "type": "MoveObject",
        "position": [450, 200],
        "inputs": {
          "Target": { "connection": "self" },
          "Direction": { "connection": { "nodeId": 5, "pin": "Result" } },
          "Speed": { "value": 500.0 }
        },
        "customCode": null
      },
      {
        "id": 9,
        "type": "ApplyForce",
        "position": [450, 350],
        "inputs": {
          "Target": { "connection": "self" },
          "Direction": { "value": [0, 1, 0] },
          "Force": { "value": 800.0 }
        },
        "customCode": "// Custom: variable jump height based on hold time\nfloat JumpForce = 800.0f * GetJumpChargePercent();\nApplyForce(this, glm::vec3(0, 1, 0), JumpForce);"
      }
    ]
  }
}
```

When `customCode` is non-null, the generator emits the custom code instead of generating from inputs.

---

### 6.11 Sync Conflict Resolution

Bidirectional editing introduces potential conflicts. Propulsion MK I handles these carefully.

#### 6.11.1 Code Editing Breaks Graph Connections

If a user modifies code in a way that **removes or changes a dependency** on a node input:

**Example scenario:**

* Node "Move Object" has `Speed` input connected to a "Get Variable [WalkSpeed]" node.
* User edits code to hardcode speed: `MoveObject(this, dir, 500.0f);`
* The connection from "Get Variable" is now **orphaned**.

**Resolution:**

1. Propulsion MK I detects the orphaned connection during code parsing.
2. The connection wire in Graph View changes to a **dashed red line** with warning icon.
3. Tooltip explains: *"This connection is no longer used in custom code."*
4. User can either:
   * Delete the orphaned connection (clean up the graph).
   * Revert the node to regenerate and restore the connection.

#### 6.11.2 Adding New Dependencies in Code

If a user writes code that references something not represented in the graph:

**Example:**

* User adds: `float boost = GetVariable("SpeedBoost");` in custom code.
* "SpeedBoost" variable exists but isn't connected in the graph.

**Resolution:**

* A subtle info marker appears on the node: *"Custom code references: SpeedBoost"*
* The graph doesn't automatically add connections (that would be confusing).
* The reference is tracked in `.pobj` metadata for documentation purposes.

#### 6.11.3 Concurrent Edit Prevention

To prevent complex merge conflicts:

* When Code View is **focused and has unsaved changes**, the Graph View shows a subtle overlay: *"Code View has unsaved changes"*
* Node inputs in Graph View become **temporarily read-only**.
* When Code View changes are saved (auto-save on defocus, or Ctrl+S), the graph updates to reflect modified node states.

This is simpler than real-time bidirectional sync and avoids race conditions.

#### 6.11.4 External File Changes

If the generated `.cpp` file is edited externally (e.g., in Visual Studio):

* Propulsion MK I watches the file for changes.
* On external change detection, a notification appears: *"Player.generated.cpp was modified externally. Reload?"*
* Reloading parses the file and applies detected custom code blocks to nodes.

---

### 6.12 Advanced: Full Code Export

For expert users who want to fully "eject" from the visual system:

**Export to Standalone C++:**

* Right-click object → "Export as C++ Class"
* Generates a clean, non-generated C++ class without the `// Node:` markers.
* The `.pobj` graph is preserved but flagged as "exported" (editing generates warnings).

**Use cases:**

* Integrating with existing C++ codebases.
* Distributing code to teams without Propulsion MK I.
* Performance-critical code that needs manual optimization.

This is an **advanced, one-way operation** with clear warnings that the visual graph will no longer be the source of truth.

---

### 6.13 Implementation Phases

The bidirectional editing system should be built incrementally:

**Phase 1: One-Way Generation (MVP)**

* Graph → C++ generation with readable output.
* Code View shows generated code (read-only).
* Click-to-navigate from code to nodes.

**Phase 2: Modification Detection**

* Code View becomes editable.
* Typing triggers modification detection.
* Modified nodes get visual badge.
* Custom code stored in `.pobj`.

**Phase 3: Full Bidirectional Sync**

* Orphaned connection detection.
* Revert-to-generated functionality.
* External file change detection.
* Concurrent edit prevention.

**Phase 4: IDE Polish**

* Autocomplete for Propulsion MK I APIs.
* Inline documentation tooltips.
* Refactoring tools (rename variable updates both graph and code).
* Breakpoint support (see 6.8 Debugging).

---









-------------------------------------------------------------------------------
## 7. Materials & Rendering
-------------------------------------------------------------------------------

This section defines how materials are authored and how the Vulkan renderer consumes them. The goals are:

* Make simple materials extremely quick to set up.
* Keep a node-graph workflow for flexibility, but with far fewer, higher-level nodes than Unreal.
* Use a physically-based shading model (metallic/roughness) as the default.

### 7.1 Current State

* [Implemented] Material Node Graph with:

  * **Final Material** node

    * Inputs: Base Color, Metallic, Roughness, Specular, Normal, Emissive, AO, Opacity, Opacity Mask, Refraction, Subsurface Color, Subsurface Strength, Emissive Strength.
  * **Image** node for PBR textures.
  * **Color Wheel** node (hex/RGB color selection).

These are already sufficient to build basic PBR materials by wiring textures and colors into the `Final Material` node.

---

### 7.2 Material Editor Design

Materials are stored as `.pmat` assets and edited in a dedicated **Material Editor** tab.

#### 7.2.1 Opening & Layout

* Double-clicking a `.pmat` in the Content Browser opens the **Material Editor** in a new tab.
* Layout (conceptual):

  * **Left:** Material properties overview

    * Material name.
    * Shading model (PBR/Unlit, later).
    * Blend mode (Opaque, Masked, Translucent – later).
    * Simple flags (Two-Sided, Cast Shadows).
  * **Center:** Graph Canvas

    * Node-based editor for material logic.
    * `Final Material` node is always present and cannot be deleted.
  * **Right:** Preview & Details

    * **Preview**: A small viewport with a test mesh (sphere/cube/plane) and simple lighting.
    * **Details**: Properties for the selected node (e.g., texture path, color value, scalar parameters).

#### 7.2.2 Material Templates (Recipes)

To make common workflows trivial, the user can create a new material from a **template**.

When the user clicks `Create → Material` or `New Material`:

* A dialog offers templates such as:

  * `Basic Lit` – Base Color + Metallic + Roughness + Normal inputs.
  * `Unlit/Emissive` – Color/Texture driving Emissive.
  * `Metal` – Assumes higher metallic and specific roughness defaults.
  * `Translucent` – Placeholder for future transparency workflows.

Selecting a template:

* Auto-creates a minimal graph with:

  * A `Final Material` node.
  * Appropriate `Image` nodes and/or `Color` nodes already connected.
* Sets sane defaults (e.g., Roughness = 0.5, Metallic = 0.0 for Basic Lit).

#### 7.2.3 High-Level Nodes (Common Operations)

To avoid low-level math spaghetti, common operations are provided as high-level nodes:

* **Texture + Color** node

  * Purpose: Combine a base color texture with a tint.
  * Inputs:

    * `Texture` (Image).
    * `Tint Color` (Color).
    * `Strength` (Number, 0–1).
  * Output: `Result Color` → connect to `Base Color`.

* **Metal/Rough Pack** node

  * Purpose: Help wire metallic and roughness from a single or dual texture source.
  * Inputs:

    * `Metallic` (Number or texture channel).
    * `Roughness` (Number or texture channel).
  * Outputs:

    * `Metallic` → Final Material.
    * `Roughness` → Final Material.

* **Normal Map** node

  * Purpose: Handle normal texture with proper interpretation.
  * Inputs:

    * `Texture` (Image).
    * `Normal Strength` (Number, default 1.0).
  * Output:

    * `Normal` → Final Material.

* **Blend Materials** node (future)

  * Purpose: Blend between two sets of material inputs.
  * Inputs:

    * `Material A` (struct-like group or multiple pins: ColorA, NormalA, etc.).
    * `Material B`.
    * `Mask` (Number or texture).
  * Output:

    * Blended outputs for Base Color, Normal, etc.

These nodes should appear at the top of the node palette for discoverability.

#### 7.2.4 Parameterization & Reuse

Materials should support parameters that can be exposed to Objects and Scene instances.

* Inside a material graph, the user can add **Scalar Parameter**, **Vector/Color Parameter**, or **Texture Parameter** nodes.
* Each parameter has:

  * `Name` (e.g., `TintColor`, `RoughnessMultiplier`).
  * `DefaultValue`.

In Objects/Scene:

* When an Object’s MeshRenderer uses a `.pmat` that has parameters:

  * The Details panel shows those parameters under a `Material Parameters` section.
  * Instances can override parameter values per object (e.g., same material, different tint or roughness).

This allows a single `.pmat` to be used for many variations of an object.

---

### 7.3 Material Graph & Shading Model

Propulsion MK I uses a **metallic/roughness PBR** shading model by default.

#### 7.3.1 Final Material Inputs

The `Final Material` node defines the contract between the material graph and the renderer.

Core inputs:

* `Base Color` (Color)
* `Metallic` (Number 0–1)
* `Roughness` (Number 0–1)
* `Specular` (Number 0–1, default small value e.g. 0.5)
* `Normal` (Vector3 in tangent space)
* `Emissive` (Color)
* `Emissive Strength` (Number)
* `Ambient Occlusion` (Number 0–1)
* `Opacity` (Number 0–1)
* `Opacity Mask` (Number 0–1 for masked materials)
* `Refraction` (Index of refraction or scalar, future)
* `Subsurface Color` (Color, future)
* `Subsurface Strength` (Number, future)

In the initial implementation, the renderer may only fully support a subset (BaseColor/Metallic/Roughness/Normal/Emissive/AO/Opacity), but the node is designed for forward compatibility.

#### 7.3.2 `.pmat` Storage

A `.pmat` file conceptually contains:

* Metadata (MaterialID, name, shading model, blend mode).
* Graph definition (nodes, pins, connections, parameters).
* Preview settings (preview mesh type, preview camera angle).

The graph structure is similar to the logic graph in `.pobj`, but evaluated on the GPU (via shader code generation) rather than per-frame CPU logic.

#### 7.3.3 Shader Generation (Concept)

Long term, the material graph will generate shader code.

* The engine compiles a material by walking its node graph from `Final Material` backwards.
* High-level nodes expand into GLSL/HLSL-like snippets or SPIR-V modules.
* For the early implementation, it is acceptable to:

  * Use a small set of pre-written shader variants.
  * Map pins directly to material constant buffers and texture samplers.

The design should not lock Propulsion MK I into a specific shading language, but the initial backend is Vulkan, so SPIR-V (via GLSL/HLSL) is expected.

---

### 7.4 Renderer Architecture (Vulkan)

The renderer is responsible for turning Scenes + Objects + Materials into pixels.

#### 7.4.1 Initial Rendering Path

For the first implementation, Propulsion MK I can adopt a **Forward+** (or straightforward Forward) renderer with:

* A single main geometry pass for opaque objects.
* Optional additional passes for transparent objects.
* Simple dynamic lights with a limited count.

Later, a Deferred renderer can be introduced if needed.

#### 7.4.2 Frame Flow (High-Level)

Per frame, the renderer roughly:

1. Collects visible objects (culling based on camera frustum, simple for now).
2. Groups draw calls by material/shader to minimize state changes.
3. Updates global GPU buffers:

   * Per-frame data (view/projection, time, etc.).
   * Per-light data.
4. For each visible object:

   * Binds the appropriate pipeline (shader, depth/stencil, blending state).
   * Binds material resources (textures, samplers, material constants).
   * Binds per-object transform.
   * Issues draw call.

In Vulkan terms, this means:

* Building command buffers per frame.
* Using descriptor sets for:

  * Globals (frame, camera, lights).
  * Per-material resources.
  * Per-object transforms (or via a structured buffer indexed by object ID).

#### 7.4.3 Render Targets & Depth

Initial design:

* One main color buffer (swapchain image or intermediate HDR buffer).
* One depth buffer.
* Optional separate HDR off-screen color target for post-processing (tone mapping to LDR at the end).

---

### 7.5 Lighting & Shadows

Lighting should be simple but physically plausible.

#### 7.5.1 Light Types

Initial light support (matching planned `LightComponent`):

* **Directional Light**

  * Typically used for sun/sky.
  * Parallel light direction, uniform across the scene.

* **Point Light**

  * Emits light in all directions from a point.
  * Attenuation based on distance.

* **Spot Light**

  * Emits light in a cone.
  * Inner/outer angle and falloff.

Each light has:

* Color (RGB).
* Intensity.
* Range (for point/spot).
* CastsShadows flag.

#### 7.5.2 Shadow Mapping (Future)

Basic shadow mapping for directional lights:

* Render a shadow map from the light’s perspective.
* Use it in the main lighting pass to determine whether a pixel is lit.

Design considerations:

* Keep the shadow system modular so it can be improved later (cascades, different filter types).
* Provide clear quality settings (shadow resolution, distance) in Project/Scene settings.

---

### 7.6 Post-Processing & Camera Controls

#### 7.6.1 Post-Processing Stack

Planned effects:

* **Bloom**

  * Based on bright areas in the HDR buffer.
* **Exposure/Auto-Exposure** (future)

  * Simple eye adaptation to varying brightness.
* **Tone Mapping**

  * Convert HDR to LDR for display.
* **Vignette** (optional, stylistic).

The post-processing system should:

* Operate on a full-screen pass after the main geometry pass.
* Be toggleable per project/scene.

#### 7.6.2 Camera Controls

Camera is controlled by `CameraComponent` and editor tools.

* **Runtime Camera**

  * Fields: FOV, Near/Far plane, Projection type.
  * Future: simple camera shake and depth of field based on focus distance.

* **Editor Camera**

  * Separate from in-game cameras.
  * Supports fly-style navigation:

    * Right mouse to look.
    * WASD to move.
    * Scroll to change speed.

---

### 7.7 Debug Views & Tools (Planned)

To help users understand what the renderer is doing:

* **View Modes**

  * Lit (default).
  * Unlit.
  * Wireframe.
  * Normals.
  * Roughness/Metallic/AO visualization.

* **Overlays**

  * Show light volumes (spheres/cones for point/spot lights).
  * Show shadow cascades/frustums (advanced).

These can be accessed via a small dropdown in the viewport toolbar.

---









-------------------------------------------------------------------------------
## 8. Physics & Collisions
-------------------------------------------------------------------------------

This section defines how Propulsion MK I handles physics simulation and collisions, how it integrates with Objects and Scenes, and how it exposes functionality to the visual logic graph.

### 8.1 Goals

* Provide a simple, predictable physics system for beginners.
* Use components (Rigidbody, Collider) that map cleanly to how users think about objects.
* Keep the integration with the visual logic graph straightforward ("On Collision", "Apply Force", etc.).
* Allow the physics backend (custom or third-party library) to be swapped or upgraded later.

### 8.2 Physics Architecture

**Concept:**

* A single **Physics World** per active Scene (or combined world for multiple loaded Scenes).
* Physics runs as part of the main update loop via a `PhysicsSystem`.
* Physics System owns:

  * The physics world/scene in the underlying library.
  * Mappings between engine Objects and physics bodies/shapes.

**Responsibilities:**

* Step the simulation with a fixed or semi-fixed time step.
* Synchronize transforms between physics bodies and engine Objects.
* Generate collision/trigger events and forward them to the Logic System.

### 8.3 Physics Components

Physics behavior is driven primarily by two components:

#### 8.3.1 RigidbodyComponent

Attached to an Object to make it participate in physics simulation.

Fields (initial):

* `BodyType`

  * `Static` – Does not move, used for level geometry.
  * `Dynamic` – Fully simulated by physics.
  * `Kinematic` – Moved by code, but can affect dynamic bodies.
* `Mass` (Number, ignored for Static).
* `UseGravity` (Bool).
* `LinearDamping` (Number).
* `AngularDamping` (Number).
* `Constraints`

  * Lock position X/Y/Z.
  * Lock rotation X/Y/Z.

Notes:

* For beginners, only `BodyType`, `Mass`, and `UseGravity` need to be surfaced prominently; the rest can be under an "Advanced" foldout.

#### 8.3.2 ColliderComponent

Defines the shape used for collisions.

Fields:

* `Shape`

  * `Box`
  * `Sphere`
  * `Capsule`
  * `Mesh` (later, for complex geometry)
* Shape parameters:

  * Box: `HalfExtents` or `Size`.
  * Sphere: `Radius`.
  * Capsule: `Radius`, `Height`.
* `IsTrigger` (Bool)

  * If true: generates overlap events but does not physically block objects.
* `Material` (future)

  * Friction, restitution (bounciness).

#### 8.3.3 Character Controller (Future)

Optionally, a higher-level **CharacterControllerComponent** can be introduced later:

* Handles typical character movement (walk, jump, step offset) without relying on full rigidbody simulation.
* Exposed to graphs via nodes like "Move Character" and "IsOnGround".

### 8.4 Simulation Loop & Transform Sync

Physics is integrated into the main loop (see High-Level Architecture), typically in `PhysicsSystem::Update`.

**Step order (per frame):**

1. **Pre-physics sync**

   * For each Object with Rigidbody/Collider:

     * If the Object has been moved directly by logic (e.g., Set Location on a kinematic body), update the corresponding physics body transform.

2. **Simulate**

   * Advance the physics world by `FixedDeltaTime` (e.g., 60 Hz = ~16.67 ms).
   * If the render DeltaTime is variable, accumulate time and step physics in one or more fixed increments as needed.

3. **Post-physics sync**

   * For each dynamic Rigidbody:

     * Read back the new transform from the physics body.
     * Update the Object’s TransformComponent (position/rotation).

4. **Event collection**

   * Physics library reports collision and trigger events.
   * These are translated into engine-level events (OnCollision, OnOverlap) and queued for the Logic System.

**Design note:**

* Use a fixed time step for simulation stability.
* Keep a clear separation between logic-driven position changes and physics-driven motion to avoid jitter.

### 8.5 Collisions, Triggers & Layers

#### 8.5.1 Collision vs Trigger

* **Collision**

  * Two solid colliders contacting.
  * Objects respond physically (bounce, slide) according to their Rigidbody settings.

* **Trigger (Overlap)**

  * At least one collider is marked `IsTrigger = true`.
  * No physical response; instead, events are fired:

    * `OnOverlapBegin`
    * `OnOverlapEnd`

#### 8.5.2 Collision Layers (Future)

To keep the UX simple initially, only a single default layer may be used. Later, add:

* **Collision Layer Matrix** in Project/Scene settings:

  * Define named layers (e.g., Default, Player, Enemy, World, TriggerOnly).
  * Checkboxes to define which layers collide with which.

* ColliderComponent fields:

  * `Layer` (dropdown of named layers).

This allows typical use cases like player ignoring triggers for certain objects or projectiles only colliding with specific targets.

### 8.6 Editor & Debug Visualization

To help users understand and debug their physics setup, provide clear visualization tools.

#### 8.6.1 Show Colliders Toggle

* Viewport toolbar includes:

  * `Show Colliders` checkbox/button.
* When enabled:

  * Colliders are drawn as wireframe shapes on top of objects.
  * Different colors can distinguish Colliders vs Triggers (e.g., solid vs dashed outlines).

#### 8.6.2 Physics Debug Overlays (Future)

Additional optional overlays:

* Show center of mass for dynamic bodies.
* Show velocity vectors.
* Highlight sleeping vs active bodies.

### 8.7 Visual Scripting Integration

Physics functionality is exposed to the visual logic graph via nodes.

#### 8.7.1 Event Nodes

* **On Collision**

  * Fired when this object collides with another object.
  * Inputs/Outputs:

    * Exec output: `On Hit`.
    * Data outputs:

      * `OtherObject` (Object reference).
      * `HitLocation` (Vector).
      * `HitNormal` (Vector).

* **On Overlap Begin / On Overlap End**

  * Fired when another collider enters or leaves a trigger collider.
  * Data outputs:

    * `OtherObject`.

These events are generated by the PhysicsSystem during event collection and queued for the LogicSystem for the next update.

#### 8.7.2 Action Nodes

* **Apply Force**

  * Inputs:

    * `Target` (Object, default This Object).
    * `Force` (Vector, in world or local space).
    * `Mode` (e.g., Force/Impulse).
  * Applies a force/impulse to the target’s Rigidbody (if dynamic).

* **Apply Torque** (future)

  * Similar to Apply Force, but for rotation.

* **Set Velocity**

  * Inputs:

    * `Target`.
    * `LinearVelocity` (Vector).

These nodes are primarily for dynamic bodies. For kinematic bodies or character controllers, separate movement nodes may be more appropriate.

#### 8.7.3 Query Nodes

* **Get Velocity**

  * Returns current linear velocity of the Rigidbody.

* **IsOnGround** (for character-like behavior, future)

  * Returns a Bool indicating whether the character/body is considered grounded.

* **Raycast / Line Trace** (future)

  * Inputs:

    * `Start`, `End`.
    * Optional `LayerMask`.
  * Outputs:

    * `Hit` (Bool).
    * `HitObject`.
    * `HitLocation`, `HitNormal`.

These nodes enable simple physical interaction and detection from visual scripts.

---









-------------------------------------------------------------------------------
## 9. Input System
-------------------------------------------------------------------------------

This section defines how keyboard/mouse (and later gamepad) input is mapped to game actions and exposed to the visual logic graph in a beginner-friendly way.

### 9.1 Goals

* A beginner should be able to say "W moves forward" without editing config files.
* Input should be defined in **one central place** and reused across Objects.
* Visual scripting nodes should read like plain English ("When Action Jump is pressed").
* The system must be extensible to support multiple devices and players later.

---

### 9.2 Core Concepts

* **Action**

  * A named digital input (on/off), e.g., `Jump`, `Interact`, `Attack`, `OpenMenu`.
  * Bound to one or more keys/buttons (e.g., Space or Gamepad A).

* **Axis**

  * A named analog or directional input, e.g., `MoveForward`, `MoveRight`, `LookX`, `LookY`.
  * Combines one or more keys/axes with scale values (e.g., W = +1, S = -1).

* **Input Mapping**

  * A collection of Actions and Axes and their bindings.
  * Stored per project in `Project.pproj` (or a separate `.pinput` asset later).

* **Input System**

  * Runtime system that polls devices each frame and resolves Action/Axis values.
  * Provides query and event data to the Logic System.

---

### 9.3 Input Mapping UI

The Input Mapping UI is the main way users define how hardware input controls their game.

#### 9.3.1 Location

* Accessible via a top menu item: `Edit → Project Settings → Input`.
* Optionally, a button in the main toolbar: `Input Settings`.

#### 9.3.2 Layout

* Split into two sections:

  * **Actions** table
  * **Axes** table

**Actions Table Columns:**

* `Name` – e.g., `Jump`, `Interact`.
* `Bindings` – one or more key/button bindings.
* `Delete` – remove action.

**Axes Table Columns:**

* `Name` – e.g., `MoveForward`, `MoveRight`.
* `Type` – Keyboard/Mouse/Gamepad (informational).
* `Bindings` – multiple entries with:

  * `PositiveKey/Button` (e.g., W)
  * `NegativeKey/Button` (e.g., S)
  * `Scale` (e.g., 1.0, -1.0)

#### 9.3.3 Adding/Editing Bindings

* **Add Action** button:

  * Creates a new row with a default name (`NewAction`) that can be renamed.

* **Add Binding** (per Action/Axis):

  * Click "Add Binding" → enters **listening mode**.
  * Engine waits for the next key or mouse button press.
  * Captured input is displayed (e.g., `Space`, `Left Mouse Button`).

* Bindings can be:

  * Reordered (if order matters later).
  * Removed via a small `X` button.

**Beginner-Friendly Default Mapping:**

* When a new project is created from a 3D template, predefine common mappings:

  * Actions:

    * `Jump` → `Space`.
    * `Interact` → `E`.
  * Axes:

    * `MoveForward` → W (+1), S (-1).
    * `MoveRight` → D (+1), A (-1).
    * `LookX` → Mouse X.
    * `LookY` → Mouse Y.

---

### 9.4 Runtime Input System

At runtime, the Input System is responsible for collecting raw input from devices and resolving Action/Axis values for the current frame.

#### 9.4.1 Update Flow

Each frame:

1. Poll keyboard/mouse (and other devices later) via the Platform layer.
2. For each defined **Action**:

   * Determine `IsPressed`, `WasPressedThisFrame`, `WasReleasedThisFrame`.
3. For each **Axis**:

   * Combine all bindings into a single float value (typically in range -1..1, but not strictly enforced).
4. Store results in an `InputState` structure accessible to the Logic System.

#### 9.4.2 Data Model (Conceptual)

* `ActionState`

  * `Name`
  * `IsPressed`
  * `JustPressed`
  * `JustReleased`

* `AxisState`

  * `Name`
  * `Value` (float)

`InputSystem` provides APIs like:

* `bool IsActionPressed(name)`
* `bool WasActionJustPressed(name)`
* `float GetAxisValue(name)`

The visual logic nodes will wrap these calls.

---

### 9.5 Visual Scripting Integration

Input is exposed to the graph in a way that matches how users think about controls.

#### 9.5.1 Event Nodes

* **When Action is Pressed**

  * Fires once when an Action transitions from not pressed to pressed.
  * Properties:

    * `ActionName` (dropdown based on defined Actions).
  * Exec output: `On Pressed`.

* **When Action is Released** (future)

  * Fires once when an Action transitions from pressed to not pressed.

These nodes internally subscribe to the Input System’s per-frame results and generate events during the LogicSystem update.

#### 9.5.2 Axis Query Nodes

* **Get Axis**

  * Inputs:

    * `AxisName` (dropdown).
  * Output:

    * `Value` (Number, usually -1..1).

Typical use case (movement):

* `Get Axis MoveForward` → moves player along local forward.
* `Get Axis MoveRight` → moves player along local right.

#### 9.5.3 Raw Key Nodes (Advanced)

For advanced users, allow bypassing the mapping system when needed.

* **When Key is Pressed**

  * Properties:

    * `Key` (e.g., `F`, `Escape`).

* **Is Key Down**

  * Returns a Bool for a specific key.

These nodes should be visually separated or marked as "Advanced" so beginners are guided toward Actions/Axes first.

---

### 9.6 Players & Devices (Future)

Initial implementation can assume a single local player using keyboard/mouse.

Future extension:

* **Multiple Players**

  * Input mapping profiles per player index.
  * Logic nodes that specify which player (e.g., `Get Axis MoveForward (Player 1)`).

* **Gamepad Support**

  * Bindings for gamepad buttons and axes (e.g., `Gamepad A`, `Left Stick X`).
  * Expose dead zone and sensitivity settings in the Input UI.

The data model should keep a notion of `PlayerIndex` and `DeviceType` even if the initial editor hides it.

---

### 9.7 Editor UX Considerations

To keep input approachable:

* Provide **tooltips** in the Input Settings UI explaining:

  * "Actions are on/off events like Jump or Interact."
  * "Axes are continuous values like movement or look direction."

* Include a small **"Test Input"** panel (future):

  * Shows live values for Actions and Axes as the user presses keys.
  * Helps debug bindings.

* When adding common logic recipes (e.g., "Move with WASD") in the Object Logic tab:

  * If the required Actions/Axes are missing, the engine can offer to **auto-create** them or guide the user to the Input Settings.

---









-------------------------------------------------------------------------------
## 10. Audio
-------------------------------------------------------------------------------

This section defines how Propulsion MK I handles audio: importing sounds, playing them in 2D/3D, and exposing controls to the visual logic graph.

### 10.1 Goals

* Make it trivial for beginners to play a sound on an event (e.g., button click, jump, door open).
* Support both **2D (non-positional)** and **3D (positional)** audio.
* Use a component-based approach (AudioSource) that fits naturally into the Object system.
* Provide enough structure to extend to more advanced features (mixers, buses, effects) later.

---

### 10.2 Audio Architecture

**Concept:**

* A central **AudioSystem** manages playback and routing of sounds.
* AudioSystem owns:

  * The audio device/context (via chosen audio backend).
  * Loaded audio clips (sound data in memory or streamed from disk).
  * Active audio instances (currently playing sounds).

**Responsibilities:**

* Load/unload audio clips based on asset usage.
* Play, stop, pause, and update sound instances.
* Apply 3D spatialization based on listener and source positions.
* Provide global controls (master volume, mute, etc.).

Initial implementation can target a straightforward backend (e.g., a simple library or platform API) with:

* Mono/stereo playback.
* Basic panning and attenuation for 3D.

---

### 10.3 Audio Asset Types

Propulsion MK I treats audio files in `Content/Audio/` (and subfolders) as **Audio Clips**.

Supported formats (initial):

* `.wav` – uncompressed PCM (simple, good for effects).
* `.ogg` – compressed (good for music/longer tracks).

Each imported audio file becomes an entry in the Asset Registry:

* Type: `Audio`.
* Path: e.g., `Content/Audio/Explosion.wav`.
* Metadata (optional): channels, sample rate, length.

Later, import settings can control:

* Streaming vs fully loaded in memory.
* 3D vs 2D default behavior.

---

### 10.4 Components

Audio is attached to Objects using components, fitting naturally into the existing Object/Component model.

#### 10.4.1 AudioSourceComponent

Attached to an Object to allow it to play sounds.

Fields (initial):

* `Clip` – audio asset reference (can be null).
* `Volume` – 0.0–1.0.
* `Pitch` – playback speed/pitch multiplier (default 1.0).
* `Loop` – bool.
* `Is3D` – bool (true = positional audio, false = 2D/UI-like).
* `MinDistance` – distance at which sound is at full volume.
* `MaxDistance` – distance after which sound is inaudible (for simple attenuation).

Behavior:

* When `Is3D` is true, the source uses the Object’s world position for spatialization.
* When `Loop` is true, the clip restarts automatically when it reaches the end.

#### 10.4.2 BackgroundMusic / Global Audio (Future)

Instead of a dedicated "Background Music" component, plan for:

* A special **global audio channel** managed by AudioSystem.
* Optional `MusicPlayer` Object blueprint that uses an AudioSourceComponent flagged as "music".

This keeps the system flexible while still making background music easy to set up.

---

### 10.5 Listener & 3D Audio

To support 3D audio, the engine must define an **Audio Listener**.

* By default, the listener is tied to the **active camera** (e.g., the player camera).
* The listener has:

  * Position (from camera/Object Transform).
  * Orientation (forward/right/up vectors from camera).

AudioSystem uses listener + AudioSource positions to compute:

* Volume attenuation (based on distance).
* Stereo panning.
* (Future) Doppler effect and more advanced spatialization.

---

### 10.6 Runtime Audio Flow

Per frame, AudioSystem performs:

1. **Listener Update**

   * Read the active camera’s transform.
   * Update listener position/orientation.

2. **Source Update**

   * For each active AudioSourceComponent:

     * Sync 3D position from Object’s Transform (if `Is3D`).
     * Apply any runtime changes (volume, pitch, looping).

3. **Instance Management**

   * Start new instances when requested by logic/engine.
   * Stop or clean up finished instances.

4. **Submit to Backend**

   * Send updated state to the underlying audio library to mix and output.

---

### 10.7 Visual Scripting Integration

Audio actions are exposed to the visual logic graph via nodes.

#### 10.7.1 Action Nodes

* **Play Sound**

  * Inputs:

    * `Clip` (audio asset reference, optional if using component’s Clip).
    * `Target` (Object, default This Object).
    * `Volume` (optional override).
    * `Pitch` (optional override).
    * `Loop` (optional override).
  * Behavior:

    * If the Target has an AudioSourceComponent, use it to play the Clip.
    * If Clip is not specified, use the component’s default Clip.

* **Play Sound at Location**

  * Inputs:

    * `Clip`.
    * `Location` (Vector3).
    * `Volume`.
  * Behavior:

    * Spawns a temporary 3D sound at the given world location (no persistent Object required).

* **Stop Sound** (future)

  * Inputs:

    * `Target` (AudioSource/Object).
  * Stops the sound currently playing on the target AudioSource.

* **Fade In / Fade Out** (future)

  * Inputs:

    * `Target`.
    * `Duration`.
    * `TargetVolume` (for Fade In).
  * Gradually adjusts volume over time.

#### 10.7.2 Global Control Nodes

* **Set Global Volume**

  * Inputs:

    * `Volume` (0.0–1.0).
  * Affects the master mix.

* (Future) **Set Music Volume**, **Set SFX Volume**

  * If/when mixer groups are introduced.

---

### 10.8 Editor UX

To keep audio simple and discoverable:

* In the Object’s Details panel:

  * AudioSourceComponent shows a simple UI:

    * Audio clip picker (with small play/stop preview button).
    * Volume/Pitch sliders.
    * Toggle for Loop and Is3D.

* In the Content Browser:

  * Audio assets show waveform or speaker icon.
  * Double-clicking opens a small **Audio Inspector** tab:

    * Shows basic info (length, sample rate, channels).
    * Allows preview playback.

* Optional **Audio Mixer/Monitor** window (future):

  * Shows currently playing voices and their volumes.

---

### 10.9 Performance & Streaming (Future)

For larger projects, consider:

* Streaming longer music tracks from disk rather than loading fully into memory.
* Limiting the number of simultaneous sound instances.
* Pooling AudioSource instances to avoid allocation overhead.

These details can be abstracted away from beginners but controlled via project-level audio settings for advanced users.

---









-------------------------------------------------------------------------------
## 11. Animation
-------------------------------------------------------------------------------

This section defines how Propulsion MK I handles character and object animation: importing animation data, playing and blending animations, and exposing control to the visual logic graph.

### 11.1 Goals

* Make it straightforward to get a character idling, walking, and running with minimal setup.
* Use a state-machine-based approach that’s visual and beginner-friendly.
* Support both skeletal animation (for characters) and simple transform animation (for props/UI) over time.
* Integrate cleanly with the Object/Component system and the visual logic graph.

---

### 11.2 Animation Asset Types

Propulsion MK I will support skeletal animation assets imported alongside skinned meshes.

**Initial asset types:**

* **Skeletal Mesh**

  * Imported from formats like `.fbx` that contain bones and skinning information.
  * Asset type: `SkeletalMesh`.

* **Animation Clip**

  * A timeline of bone transforms for a given skeleton (e.g., `Idle`, `Walk`, `Run`, `Jump`, `Attack`).
  * Asset type: `Animation`.
  * Stored under `Content/Animations/` by convention.

Import process (conceptual):

* When importing a skinned mesh file:

  * User can choose to import:

    * Skeletal mesh only.
    * Skeletal mesh + one or more embedded animations.
  * Skeleton hierarchy is stored as part of the `SkeletalMesh` asset or as a separate `Skeleton` asset (future).

---

### 11.3 Components

Animation is attached to Objects using components.

#### 11.3.1 SkeletalMeshComponent

Attached to an Object to render a skinned character or creature.

Fields:

* `Mesh` – reference to `SkeletalMesh` asset.
* `DefaultMaterial(s)` – material overrides.
* `Visible` – bool.

This component itself does not define animation; it only renders the mesh given the current pose.

#### 11.3.2 AnimatorComponent

Controls which animation(s) play on a SkeletalMesh.

Fields (initial):

* `SkeletalMeshComponent` reference (implicit if on same Object).
* `AnimationController` reference – points to an **Animation State Machine** asset (see 11.4).
* `PlaybackSpeed` – global multiplier (default 1.0).
* `AutoPlay` – whether to automatically start the default state when the game begins.

Runtime state (per instance):

* Current state (e.g., `Idle`, `Walk`).
* Current time within each playing clip.
* Blend weights for crossfades.
* Parameter values (e.g., `Speed`, `IsInAir`).

---

### 11.4 Animation State Machine (Controller)

Instead of wiring animations purely via logic graphs, Propulsion MK I uses a dedicated **Animation Controller** asset (state machine).

#### 11.4.1 Concept

* An **Animation Controller** defines:

  * A set of named **States** (Idle, Walk, Run, Jump, etc.).
  * Transitions between states with conditions.
  * Parameters that drive those transitions (e.g., `Speed` float, `IsOnGround` bool).

* It is edited in a visual **State Machine Editor**, similar in spirit to Unreal’s Anim Graph but with a simpler scope and UI.

#### 11.4.2 Animation Controller Asset

* Stored as a separate asset, e.g., `.fanimctrl` (name flexible, concept is what matters).
* Contains:

  * `States[]`:

    * `Name` (e.g., `Idle`).
    * `AnimationClip` reference.
    * `Loop` flag.
    * Optional blend settings (e.g., blend in/out times).
  * `Parameters[]`:

    * `Name` (e.g., `Speed`, `IsInAir`).
    * `Type` (Number/Float, Bool).
    * `DefaultValue`.
  * `Transitions[]`:

    * `FromState` → `ToState`.
    * `Conditions` (simple expressions like `Speed > 0.1`, `IsInAir == true`).
    * `BlendTime`.

---

### 11.5 State Machine Editor

The State Machine Editor is a dedicated visual editor.

Layout (conceptual):

* **Left:** Parameter list

  * Shows all parameters (Name, Type, DefaultValue).
  * Buttons to add/remove parameters.

* **Center:** State graph view

  * Nodes represent animation states.
  * Arrows represent transitions.
  * Double-clicking a state opens its properties.

* **Right:** State/Transition details

  * For states: selects AnimationClip, Loop, blend settings.
  * For transitions: defines conditions and blend time.

Basic workflow for a walking character:

1. Create parameters:

   * `Speed` (Float).
   * `IsInAir` (Bool).
2. Create states:

   * `Idle` → `Idle.anim`.
   * `Walk` → `Walk.anim`.
   * `Run` → `Run.anim`.
3. Define transitions:

   * `Idle` → `Walk` when `Speed > 0.1`.
   * `Walk` → `Idle` when `Speed <= 0.1`.
   * `Walk` → `Run` when `Speed > 2.0`.
   * `Run` → `Walk` when `Speed <= 2.0`.

Parameters can be set from the visual logic graph (see 11.7).

---

### 11.6 Runtime Evaluation

At runtime, the AnimatorComponent and Animation Controller cooperate to determine the bone pose.

Per frame for each Object with AnimatorComponent:

1. Read current parameter values.
2. Evaluate state transitions:

   * Check conditions.
   * If a transition is valid, initiate crossfade from current state to target state.
3. Advance animation time:

   * For active state(s), advance time by `DeltaTime * PlaybackSpeed`.
   * Loop or clamp based on state settings.
4. Compute final pose:

   * Sample underlying AnimationClip(s) at the current time(s).
   * Blend poses according to crossfade/blend weights.
5. Write the resulting bone transforms to the SkeletalMeshComponent for rendering.

This runtime evaluation is typically updated in an `AnimationSystem::Update(DeltaTime)` step in the main loop.

---

### 11.7 Visual Scripting Integration

Animation is controlled from the visual logic graph primarily via **parameters** and some direct control nodes.

#### 11.7.1 Parameter Nodes

* **Set Animation Parameter (Float/Bool)**

  * Inputs:

    * `Target` (Object, default This Object).
    * `ParameterName` (dropdown populated from the bound Animation Controller).
    * `Value`.
  * Behavior:

    * Sets a parameter on the AnimatorComponent, which will drive state transitions and blending.

* **Get Animation Parameter** (advanced)

  * Returns the current parameter value.

Typical usage:

* In a character Object’s logic graph:

  * Compute `Speed` from the Rigidbody or movement logic (e.g., length of velocity vector).
  * Call `Set Animation Parameter (Float)` with `ParameterName = Speed`.
  * Set `IsInAir` based on physics `IsOnGround`.

#### 11.7.2 Direct Control Nodes

* **Play Animation (One-Shot)**

  * Inputs:

    * `Target` (Object).
    * `Clip` (Animation asset).
    * `BlendTime`.
  * Bypasses the state machine for quick one-off animations (e.g., door opening, small prop animation).

* **Set Animation State**

  * Inputs:

    * `Target`.
    * `StateName` (string/dropdown from controller).
    * `BlendTime`.
  * Forces the Animator into a specific state (useful for scripted sequences).

These nodes provide a simple mental model: parameters for continuous behavior, direct control for moments when you want to override.

---

### 11.8 Non-Skeletal / Simple Animations (Future)

Beyond skeletal animation, the engine can later support simple animations for:

* Transform (position/rotation/scale over time).
* Material parameters (e.g., fade-in/out, color pulses).
* UI animations (slide, fade, scale).

This could be achieved with a **Timeline/Curve** system:

* A `Timeline` asset or component that:

  * Defines keyframes or curves over time.
  * Can be started/stopped from the visual graph.

For now, this is a future extension, but the design should keep room for a general animation/timeline system that complements the AnimatorComponent.

---









-------------------------------------------------------------------------------
## 12. UI (In-Game)
-------------------------------------------------------------------------------

This section defines the in-game UI system (separate from the ImGui-based editor UI): how screens and HUDs are built, how they are laid out, and how they connect to the visual logic graph.

### 12.1 Goals

* Allow beginners to create simple menus and HUDs quickly (health bars, buttons, labels).
* Use a visual, WYSIWYG **UI Layout Editor** with drag-and-drop widgets.
* Make UI behavior easy to control via the visual logic graph ("When Button is Clicked → Do X").
* Keep the system resolution-agnostic via anchoring and scaling.

---

### 12.2 Core Concepts

* **UI Widget**

  * A basic element of the UI (Button, Text, Image, Slider, Panel, etc.).

* **UI Canvas / Screen**

  * A collection of widgets arranged in a hierarchy for a specific purpose (e.g., Main Menu, HUD, Pause Menu).
  * Saved as a `.pui` asset (concept name; extension can be adjusted later).

* **UI Instance**

  * A specific instance of a UI Canvas active at runtime (e.g., the current HUD for Player 1).

* **UI System**

  * Manages active UI instances, input routing (mouse/keyboard/gamepad), and rendering of UI.

---

### 12.3 UI Widgets

Initial widget types:

* **Panel / Container**

  * Basic container for grouping other widgets.
  * Provides background color/image and padding.

* **Text**

  * Displays simple text.
  * Properties: Text string, Font, Size, Color, Alignment.

* **Image**

  * Displays a texture/sprite.
  * Properties: Texture, Color (tint), Stretch mode.

* **Button**

  * Clickable area with visual states (normal/hover/pressed/disabled).
  * Contains a child Text for label by default.

* **Slider**

  * Allows selecting a value in a range.
  * Properties: Min, Max, Value.

Later additions might include checkboxes, progress bars, dropdowns, etc., but the initial goal is a minimal, useful set.

---

### 12.4 UI Layout Editor

The UI Layout Editor allows users to visually build `.pui` screens.

#### 12.4.1 Opening & Layout

* UI Canvas assets (`.pui`) are created from the Content Browser via `Create → UI Canvas`.
* Double-clicking a `.pui` opens the **UI Layout Editor** in a new tab.

Editor layout (conceptual):

* **Left:** Hierarchy panel

  * Shows the tree of widgets (root canvas → panels → buttons, text, etc.).
  * Supports drag-and-drop reordering.

* **Center:** UI Canvas viewport

  * WYSIWYG preview of the UI.
  * Uses a reference resolution (e.g., 1920×1080) with optional preview for other aspect ratios.

* **Right:** Details panel

  * Shows properties for the selected widget.
  * Common sections: Transform (position/size/anchors), Appearance (color, font), Behavior (interactions).

* **Top:** Toolbar

  * Tools for selecting, moving, resizing widgets.
  * Zoom controls, reference resolution dropdown.

#### 12.4.2 Anchors & Layout

To support different resolutions and aspect ratios:

* Each widget has **Anchors** relative to its parent (e.g., top-left, center, bottom-right, stretch modes).
* Each widget has **Position** and **Size** values in either:

  * Pixels (absolute) or
  * Relative terms (percent of parent, future).

Beginner-friendly defaults:

* New widgets default to reasonable anchors depending on where they are dropped (e.g., dropping near top-left anchors it there).

---

### 12.5 UI Rendering

The UI System handles rendering widgets after the 3D scene is drawn.

High-level flow per frame:

1. Render 3D Scene.
2. Render active UI instances on top (in screen space).

Rendering specifics:

* UI uses a **2D orthographic projection** aligned with screen coordinates.
* All UI rendering is batched where possible to minimize draw calls.
* Basic support for:

  * Text rendering.
  * Rectangles and textured quads.
  * Simple transparency and layering using a Z-order or explicit draw order in the hierarchy.

---

### 12.6 UI & Input

The UI System must integrate with the Input System to handle mouse/keyboard events.

#### 12.6.1 Input Routing

* Mouse position is mapped to UI canvas coordinates.
* The UI System performs hit testing to determine which widget (if any) is under the cursor.
* For each frame:

  * Handle hover states (Mouse Over/Out).
  * Handle click events (Mouse Down/Up).

Keyboard navigation (future):

* Support focus-based navigation (e.g., arrow keys, Tab) for gamepad/keyboard-only use.

#### 12.6.2 Button Interaction

Buttons expose standard events:

* `On Click` – when the button is pressed and released over the same widget.
* (Future) `On Hover`, `On Pressed`, `On Released`.

These events connect to the visual logic graph as described below.

---

### 12.7 Visual Scripting Integration

UI events and properties are exposed to the visual logic graph.

#### 12.7.1 Event Nodes

* **When Button is Clicked**

  * Inputs:

    * `Widget` reference (specific Button in a `.pui`).
  * Exec output: `On Click`.
  * Typical usage: hooking up UI buttons to actions like "Start Game", "Quit", "Resume".

* (Future) **When Widget is Shown/Hidden**, **When Slider Value Changes**, etc.

#### 12.7.2 Action Nodes

* **Show Widget**

  * Inputs:

    * `Widget` reference.
  * Makes the widget visible.

* **Hide Widget**

  * Inputs:

    * `Widget` reference.
  * Makes the widget invisible (but it may still occupy layout space if desired).

* **Set Text**

  * Inputs:

    * `Widget` reference (Text widget or Text child of a Button).
    * `Text` (string).
  * Updates the visible text.

* (Future) **Set Image**, **Set Slider Value**, etc.

#### 12.7.3 Connecting UI to Objects

Common pattern:

* UI button event triggers logic on a specific Object or system:

  * Example: `When Button is Clicked (StartButton)` → `Load Scene Level01`.
  * Example: `When Button is Clicked (QuitButton)` → `Exit Game`.

The editor should make it easy to:

* Pick a target Object from the Scene.
* Call a simple node that performs the desired action (e.g., change a variable, call a function node, load a scene).

---

### 12.8 UI Instances & Screens

To manage menus and HUDs:

* **UI Canvas (`.pui`)** defines layout and widgets.
* At runtime, the UI System can:

  * Create an instance of a UI Canvas.
  * Show/Hide or Destroy that instance.

Example usage:

* At game start:

  * Create `MainMenu.pui` instance and show it.
* When starting the game:

  * Hide/destroy `MainMenu` instance.
  * Create `HUD.pui` instance and show it.

In the visual graph, this could be driven by nodes like:

* `Show UI Canvas` / `Hide UI Canvas`.

---

### 12.9 Editor UX Considerations

To keep UI approachable:

* Provide a simple **UI Template** system:

  * New UI Canvas from templates like "Blank", "Main Menu", "HUD".

* In templates, include example logic:

  * A "Play" button already wired to a stub node in the graph (with instructions in a tooltip).

* Add hover tooltips in the UI Layout Editor explaining:

  * Anchoring concepts.
  * How to connect UI events to logic.

---









-------------------------------------------------------------------------------
## 13. Play & Build Pipeline
-------------------------------------------------------------------------------

This section defines how Propulsion MK I runs games inside the editor, how it produces standalone builds, and how logging/profiling are surfaced to users.

### 13.1 Goals

* Make it easy to hit **Play** and see the game run in the editor viewport.
* Keep a clear separation between **editing** and **playing** states to avoid confusion.
* Provide a simple, reliable way to create standalone builds with minimal configuration.
* Expose basic logging and performance data (FPS, frame time) in a beginner-friendly way.

---

### 13.2 Game Modes: Editor vs Runtime

Propulsion MK I distinguishes between two main modes:

* **Editor Mode**

  * User can move objects, change properties, edit graphs, etc.
  * Scene is not fully simulated (no gameplay logic or physics unless explicitly previewed).

* **Play Mode (In-Editor)**

  * The game runs using the current Scene(s) as if in a standalone build.
  * Logic graphs, physics, audio, and animation systems are active.
  * Editor UI remains visible; the game is embedded in the editor viewport.

* **Standalone Build**

  * Exported executable that runs without the editor.
  * Uses the same engine core and content but without ImGui/editor overhead.

---

### 13.3 In-Editor Play Controls

Editor toolbar exposes basic runtime controls:

* **Play**

  * Starts simulation from the current Scene’s **Start Scene** (or selected Scene if configured).
  * Disables direct object editing in the main viewport while running.

* **Pause**

  * Freezes time progression (DeltaTime = 0 for Logic/Physics).
  * Allows the user to inspect objects and variables mid-simulation.

* **Step** (future)

  * Advances the simulation by a single frame or fixed time step while paused.

* **Stop**

  * Ends simulation and restores Scene to its **pre-play state**.

#### 13.3.1 Play Session Lifecycle

When the user hits **Play**:

1. Save any unsaved changes in the Scene (optional prompt or autosave snapshot).
2. Create a **Runtime World** from the current Scene data:

   * Instantiate objects and components.
   * Initialize systems (Logic, Physics, Audio, Animation, UI, etc.).
3. Set **GameIsPlaying = true** and start ticking runtime systems.

When the user hits **Stop**:

1. Shutdown runtime systems (stop audio, clear physics, destroy runtime objects).
2. Discard runtime-only state; do **not** overwrite the authoring Scene unless explicitly requested.
3. Restore editor mode with Scene state as it was before Play.

Design note:

* Later, an option can allow selectively keeping certain runtime changes (e.g., object transforms) if the user wants to "Apply Changes" from Play Mode.

---

### 13.4 Play-In-Viewport vs Play-In-Window (Future)

Initial implementation:

* **Play In Viewport**

  * Game runs inside the main editor viewport.
  * Editor UI remains accessible around it.

Future options:

* **Play In New Window**

  * Launches a separate game window while editor remains in control.

* **Maximize On Play**

  * Temporarily maximizes the game viewport for an immersive preview.

These modes are configured in Project or Editor settings.

---

### 13.5 Build Configurations & Targets

Propulsion MK I supports multiple build configurations for different stages of development.

Initial support:

* **Targets**

  * Windows desktop (x64).

* **Configurations**

  * `Debug` – full logging, asserts enabled, minimal optimization.
  * `Release` – optimized, reduced logging.
  * (Future) `Development` – a middle ground used for testing.

Build settings (stored in `Project.pproj` or a related asset):

* Target platform(s).
* Output directory.
* Executable name.
* Icon and splash screen (future).
* Default start Scene for standalone builds.

---

### 13.6 Build Pipeline (Concept)

When the user chooses `Build Game` from the menu, Propulsion MK I performs:

1. **Validation**

   * Ensure required settings are defined (start Scene, output path).
   * Check for missing assets referenced by Scenes/Objects.

2. **Asset Collection**

   * Traverse Scenes and Objects to determine which assets are referenced (meshes, materials, textures, audio, animations, UI, etc.).
   * Optionally include all assets in the project for simplicity (for early versions).

3. **Asset Cooking (Future)**

   * Convert source assets to runtime-friendly formats (compressed textures, preprocessed meshes, etc.).
   * Generate a manifest describing all assets and their runtime paths/IDs.

4. **Shader/Material Compilation**

   * Compile or pre-link shaders required by materials.

5. **Packaging**

   * Copy engine binaries, runtime data, and cooked assets to the build output directory.
   * Generate a minimal configuration file for the runtime (e.g., start Scene, resolution defaults).

6. **Post-Build**

   * Report success or errors in the Build Log.
   * Optionally offer a "Run Build" button to launch the game.

For early-stage development, some steps can be simplified (e.g., using source assets directly), but the pipeline should be designed with future cooking in mind.

---

### 13.7 Logging & Console

Propulsion MK I provides a simple logging system and in-editor console to help users understand what’s happening.

#### 13.7.1 Logging System

* Logging categories (e.g., `Core`, `Renderer`, `Physics`, `Logic`, `Audio`, `User`).
* Levels (e.g., `Info`, `Warning`, `Error`).
* API used by engine and optionally exposed to user code/graphs.

Logs can be output to:

* Editor console window.
* Log file on disk (for debugging standalone builds).

Visual scripting integration:

* **Print Debug Message** node (already planned in Logic Graph section) writes to:

  * On-screen overlay (short-lived).
  * Console window.

#### 13.7.2 Console Window

* Editor panel: **Console**.
* Shows:

  * Timestamp.
  * Category.
  * Message.
* Features:

  * Filter by category and level.
  * Clear log.
  * Pause scrolling.

This gives beginners a simple place to see errors (e.g., missing assets, invalid connections) and debug messages.

---

### 13.8 Basic Profiling & Stats

To help users gauge performance, Propulsion MK I exposes some basic statistics.

Initial features:

* **FPS Counter**

  * Displayed in the viewport corner when enabled.

* **Frame Time**

  * Average or instantaneous frame time in ms.

* **Draw Call / Triangle Count** (future)

  * Simple counts to give users a feel for scene complexity.

Access:

* Via a small **Stats** dropdown in the viewport toolbar.
* Or a dedicated **Performance** panel for more detailed breakdown in future.

---









-------------------------------------------------------------------------------
## 14. Extensibility & Advanced Use
-------------------------------------------------------------------------------

This section defines how Propulsion MK I can be extended beyond the built-in tools: via plugins, a C++ API, and (later) higher-level scripting options. The goal is to keep the **beginner path** simple while giving **advanced users** room to bend the engine to their will.

### 14.1 Goals

* Allow advanced users to add new functionality without modifying the engine core.
* Keep the built-in experience clean – no need to learn plugins/C++ to make a game.
* Provide a stable API surface that can evolve without constantly breaking user code.
* Support sharing/distribution of extensions (e.g., plugins) between projects.

---

### 14.2 Plugin System (Planned)

**Concept:**

* A **plugin** is a package of code and assets that extends Propulsion MK I.
* Plugins can be enabled/disabled per project.
* Typical use cases:

  * New components (e.g., advanced character controller).
  * New systems (e.g., custom AI, networking).
  * Editor tools (importers, custom panels).

#### 14.2.1 Plugin Structure

Each plugin lives in a folder, e.g., `Plugins/PluginName/`, and may contain:

* `Plugin.pplugin` – metadata file (name, version, author, description, dependencies).
* `Binaries/` – compiled code for supported platforms.
* `Content/` – assets (materials, objects, UI, etc.).
* `Config/` – optional configuration files.

#### 14.2.2 Plugin Discovery & Loading

* On project load, Propulsion MK I:

  * Scans `Plugins/` directory.
  * Reads `Plugin.pplugin` metadata.
  * Loads enabled plugins.

* Plugins can register:

  * New component types.
  * New systems (update steps in the main loop).
  * New editor panels and tools.
  * New visual scripting nodes.

#### 14.2.3 Plugin Management UI

* Accessible via `Edit → Plugins`.
* Shows a list of available plugins with:

  * Name, version, author.
  * Description.
  * Enabled/disabled toggle.

Future features:

* Dependency handling (plugin A depends on plugin B).
* Warnings for incompatible engine versions.

---

### 14.3 C++ API (Planned)

For users comfortable with C++, Propulsion MK I exposes an API to extend engine behavior.

#### 14.3.1 Use Cases

* Implement custom components (e.g., `WaterBuoyancyComponent`).
* Implement custom systems (e.g., `AISystem`, `WeatherSystem`).
* Integrate third-party libraries (e.g., pathfinding, networking).
* Add custom visual scripting nodes backed by C++.

#### 14.3.2 Extension Points

The C++ API will provide registration mechanisms for:

* **Components**

  * Register new component types with metadata (name, fields, default values).
  * Define how they serialize/deserialize.
  * Provide an editor Details UI via reflection or simple descriptors.

* **Systems**

  * Register systems that implement an `Update(DeltaTime)` function.
  * Configure their place in the update order (e.g., before/after physics).

* **Graph Nodes**

  * Register new node types with:

    * Node name and category.
    * Pins (exec/data in/out, types).
    * C++ function callbacks for execution.

* **Editor Extensions (Future)**

  * Add custom panels/windows and menu items.

#### 14.3.3 Build & Integration Model

Possible approaches:

* **Static linking** in early versions:

  * Custom C++ code is compiled together with the engine/editor.
  * Simpler to implement but less dynamic.

* **Dynamic libraries (DLLs/shared objects)** in later versions:

  * Plugins compiled into separate binaries.
  * Engine loads them at runtime.

The design of the C++ API should favor **clear, documented headers** and minimal macro magic, keeping it approachable for indie developers.

---

### 14.4 Scripting Layer (Future)

In addition to visual scripting and C++, Propulsion MK I may support a higher-level scripting language (e.g., Lua, Python, or a custom language) in the future.

**Goals for scripting:**

* Provide a quicker iteration loop than C++ (no full rebuilds).
* Allow logic that is more complex than what’s comfortable in visual graphs.
* Keep the visual logic graph and scripting layer interoperable.

#### 14.4.1 Possible Integration Model

* Each Object could:

  * Use a visual graph.
  * Use a script.
  * Or combine both (graph calling into script functions).

* Script bindings would expose:

  * Engine types (Vector, Transform, Object references, etc.).
  * Common APIs (spawn, destroy, play sound, load scene, etc.).

Initial scope (when/if added):

* Simple per-object scripts for gameplay logic.
* Utility scripts for tools (import automation, batch tasks).

---

### 14.5 Advanced Workflows & Power-User Features (Future)

As the engine matures, consider features aimed at advanced users:

* **Custom Build Steps**

  * Pre/post-build hooks (e.g., run a script before packaging).

* **Automation & CLI Tools**

  * Command-line tools for building, testing, or running headless simulations.

* **Data-Driven Config**

  * External config files or data tables that games can read for balancing, localization, etc.

These features should remain opt-in and not clutter the beginner experience, but the engine architecture should leave room for them to be added cleanly.









-------------------------------------------------------------------------------
## 15. Quality-of-Life & Polish
-------------------------------------------------------------------------------

This section defines small-but-important features that make Propulsion MK I feel smooth, forgiving, and pleasant to use day-to-day.

### 15.1 Goals

* Reduce frustration by making mistakes easy to undo.
* Protect users from data loss.
* Make it fast to find things (objects, assets, settings).
* Help beginners understand features without needing external docs.

---

### 15.2 Undo/Redo System

A global **Undo/Redo** stack tracks user actions across the editor.

#### 15.2.1 Scope

Undoable actions include (non-exhaustive):

* Transform changes (move/rotate/scale objects).
* Property edits in Details panels (numbers, colors, asset references).
* Adding/removing components.
* Adding/removing/moving objects in a Scene.
* Graph edits (adding/removing nodes, connecting pins, moving nodes).
* Material graph edits.
* UI layout edits.

Some actions may be non-undoable (e.g., certain project-level settings), but these should be rare and clearly indicated.

#### 15.2.2 UI & Shortcuts

* Standard shortcuts:

  * `Ctrl+Z` – Undo.
  * `Ctrl+Y` or `Ctrl+Shift+Z` – Redo.
* Menu entries under `Edit`:

  * `Undo [Last Action Description]`.
  * `Redo [Last Action Description]`.

Each undo step stores a short description (e.g., "Move Object", "Change Base Color", "Add Component: Light").

---

### 15.3 Auto-Save & Recovery

To protect against crashes or accidental closure, Propulsion MK I supports auto-saving.

#### 15.3.1 Auto-Save Behavior

* Auto-save interval configurable in Editor Settings (e.g., every 5, 10, 15 minutes).
* Auto-saves do **not** overwrite primary Scene/asset files.
* Auto-saves are stored in a separate folder (e.g., `Propulsion/AutoSaves/`).

On startup after an unexpected shutdown:

* Engine detects existing auto-save snapshots.
* Offers to:

  * Restore from the most recent auto-save, or
  * Discard and continue with the last manual save.

#### 15.3.2 Manual Save Reminders (Optional)

* If the user has unsaved changes for a long period, show a subtle reminder: "You have unsaved changes" with a `Save Now` button.

---

### 15.4 Search & Navigation

To keep projects manageable as they grow, Propulsion MK I provides search tools.

#### 15.4.1 Asset Search

* Search bar in the **Content Browser**:

  * Filter by name.
  * Optional filters by type (Material, Object, Scene, Audio, etc.).

#### 15.4.2 Scene Object Search

* Search field at the top of the **Scene Objects** panel:

  * Filters objects by name as you type.
  * Optionally supports fuzzy matches (e.g., partial names).

#### 15.4.3 Quick Open (Future)

* A shortcut (e.g., `Ctrl+P`) to open a "Quick Open" dialog:

  * Type to search assets by name.
  * Press Enter to open the selected asset in a new tab.

---

### 15.5 Tooltips, Help & Onboarding

Built-in guidance helps users understand controls without leaving the editor.

#### 15.5.1 Contextual Tooltips

* Hovering over buttons, fields, and nodes shows short explanations.
* For complex features, tooltips may include:

  * One-sentence description.
  * A tiny diagram or example.

Example:

* Hover over `Anchors` in the UI editor:

  * Tooltip: "Anchors control how this widget sticks to the screen when the resolution changes." + small visual hint.

#### 15.5.2 First-Time Hints

* On first use of certain features (e.g., opening the Logic Graph, Material Editor, UI Editor):

  * Show a small hint panel with "What you can do here" and a `Got it` button.

#### 15.5.3 Help Links (Future)

* Optional links to online docs or local help pages for deeper explanations.

---

### 15.6 Editor Preferences

Users can customize some aspects of the editor to fit their workflow.

Initial preferences (in `Edit → Editor Settings`):

* Theme (Light/Dark, if supported by ImGui/the editor skin).
* Grid settings (size, color, snapping on/off).
* Gizmo settings (handle size, colors).
* Auto-save interval.
* In-Editor Play behavior (e.g., maximize viewport on Play).

---

### 15.7 Error Handling & User-Friendly Messages

When something goes wrong, Propulsion MK I should explain it clearly.

* Missing asset references:

  * Show placeholders in the viewport/graphs.
  * Console message: "Material 'Wood.pmat' missing – using default material.".

* Invalid graph connections:

  * Highlight problematic nodes/pins.
  * Show plain-language explanation in tooltip or a small warning panel.

* Build errors:

  * Summarize issues in a **Build Log** panel.
  * Provide clear messages (e.g., "Start Scene not set in Project Settings").

> This is a living document. As Propulsion MK I evolves, new sections and details will be added or refined.
