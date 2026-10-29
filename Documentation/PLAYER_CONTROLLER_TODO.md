# Player controller to-do

Goal: a basic player controller written as an Object script (graph or `.prop`) that works in Play mode.
Each step is finished only when its test passes. Do them in order.

Status key: [ ] not started, [~] in progress, [x] done and tested

## 0. Play mode shell
Play already runs scripts on a snapshot of the scene (`Engine::startPlay` / `stopPlay`, Play menu item,
Escape to stop). What it lacks to feel like a game:
- [x] A visible Play/Stop button (centered in the menu bar, green/red), and a "playing" state in the viewport (green border)
- [x] FPS counter at the viewport's top-right, always on while playing (toggle comes later)
- [x] While playing, the editor gizmos, selection outline, tool buttons, drag-drop and fly camera are off
- [x] Builds and launches (Debug)
- [x] Hand test (needs a person): Play, an object with a script moves with WASD, border and FPS show, Stop restores the scene exactly
- (Mouse capture moved to step 2, where something uses it)

## 1. Camera component (Unreal-style: a component on an Object)
Design: an Object asset can have a Camera component. Settings: field of view (horizontal degrees, default 90),
near/far clip, "auto activate for player", and a relative location/rotation from the Object (so a third-person
camera is just an offset). Because the camera lives on the Object, a scripted player carries it along.
Engine conventions (settled 2026-10-07): the world is **Z up, forward is +Y, right is +X** (right-handed,
Blender-style). Rotation is Euler degrees: pitch about X, roll about Y, yaw about Z. This matches the editor
(grid, fly camera, gizmos) and now also TransformComponent, the PropulsionScript interpreter and C++ generator, and
the camera component. camera.fbx is modeled Y-up looking down -Z, so the editor draws it turned a quarter about X
(`CameraSettings::meshCorrection`).
- [x] 1a. `CameraSettings` data, saved in the .pobj file (v4); older files still load. Test: `ObjectFileTest` passes
- [~] 1b. Object editor: Add/Remove Camera component and edit its settings. Compiles; waiting on a hand test
      (close the editor first so the exe can be relinked)
- [x] 1c. (hand-tested: model looks right) Editor-only camera mesh (`tests/models/camera.fbx` copied to `resources/default_meshes/Camera.fbx`) drawn
      for instances whose Object has a camera; hidden during Play. Test: hand
- [~] 1d. Frustum outline when such an instance is selected (editor only). Math tested (`CameraFrustumTest`
      passes); built, waiting on a hand test
- [~] 1e. Play looks through the camera (auto-activate); editor camera comes back on Stop. Built; the pose, view
      and field-of-view math is tested (`CameraFrustumTest`). Waiting on a hand test: walker + camera
      (the first Object, in scene order, whose camera has Auto Activate on is the one used)
- [ ] Later: aspect-ratio constraint / letterbox, orthographic, several cameras with a blend between them

Found along the way (not part of this item): imported meshes are never loaded for rendering. Only Cube, Sphere and
Cylinder are drawn; anything else falls back to a cube. The glTF import also isn't supported by the loader (ufbx).

## 2. Mouse look
Play captures the mouse (cursor hidden, raw movement; Esc or Stop gives it back, and so does alt-tabbing away).
`lookX` is positive when the mouse moves right and `lookY` when it moves up. They are a *rate*, 1.0 = 1000 pixels per
second, so a script scales them by a speed and `per second` and the turn doesn't depend on the frame rate
(`include/scripting/MouseLook.h`). Turning right is a negative yaw, and pitch is about X (looking up is positive).
- [x] `lookX` / `lookY` axes return mouse movement (`MouseLook`, used by `EngineHost::axis` in PlayMode.cpp);
      capture on Play, release on Stop. Test: `MouseLookTest` (directions, frame-rate independence, no jump on capture)
- [x] `player.prop`: yaw from lookX, pitch from lookY (clamped to +/-89), and walking stays level when looking up
      or down. Tests: `behaviors.rs` and the compiled C++ comparison in `differential.cpp`
- [x] Spring Arm component (Unreal's SpringArm; replaces the short-lived "Pitch Tilts Camera Only" switch and Look Pivot).
      "+ Add Component > Spring Arm": Target Arm Length, Target Offset (where the arm is fixed, e.g. head height), Socket
      Offset, Use Pawn Control Rotation. The Camera hangs from the end of it (a child of it in the Graph's component
      list). With Use Pawn Control Rotation on, a scripted Object's X rotation (look up/down) swings only the arm, so the
      Object stays upright (`Engine::applyLookPitch`, `cameraWorldMatrix(..., lookPitch)`). Arm length 0 = first person.
      File v7; v5/v6 files load as an equivalent arm. Tests: `CameraFrustumTest`, `ObjectFileTest`
- [x] Arm collision and arm lag (Spring Arm settings, in Play only; `include/core/SpringArm.h`). Do Collision Test (on by
      default): a ball of Probe Size (0.2) is slid from the arm's fixed point to where the camera wants to be, socket
      offset included, and the camera stops where it meets a solid object, then goes back out the moment the way is
      clear. The Object itself, characters and non-solid objects don't block it (like Unreal's camera channel); a
      first-person arm (length 0, no socket) isn't tested. Enable Camera Lag (Speed 10, Max Distance 0 = none) eases the
      arm's fixed point toward the Object; Enable Camera Rotation Lag (Speed 10) eases the arm's direction toward where
      the player is looking. Both off by default, speed 0 means no lag, and a jump over 20 units snaps (a teleport). The
      lag is frame-rate independent (1 - e^(-speed*dt)). File v10 (the settings are a block at the end; older files load
      with collision on and no lag). Not done: easing the camera back out after a pull-in (it snaps out, as Unreal's
      does). Tests: `SpringArmTest`, `ObjectFileTest`
- [~] Hand test: Play, move the mouse and the view turns the right way (right = right, up = up), the camera follows,
      W walks along the ground while looking up, Esc brings the cursor back

## 3. Collision and gravity
Design: the engine does what Unreal's CharacterMovement does. The script says where to walk (`move Self forward ...`);
after the scripts run each frame, the engine carries that move out against the solid objects, adds gravity and a jump,
and gives the object the position it really ended up at (the script starts from there next frame).
- Every scene object with a mesh is a solid shape: its mesh's bounds, turned and scaled with it. Box for most meshes
  (a cube is exact), a ball for the Sphere mesh, a cylinder for the Cylinder mesh (its length runs along the mesh's own Y
  axis, as drawn). Solid by default; an Object's "Solid" checkbox turns that off (pickups), and a script's
  `make Collision.Enabled false` does it while playing. An Object with no mesh has no shape.
- A character is an upright capsule centered on the Object's origin, from the "Character Movement" component
  (radius, half height, gravity, jump speed, max slope, and a "Jump Action": the Input Action that makes it jump). Adding the component fits the capsule to
  the mesh, so the mesh stands exactly on the ground ("Fit to Mesh" does it again). It scales with the object.
- Characters slide along walls, stand on slopes up to the max slope (and slide down steeper ones), bump their heads on
  ceilings and fall off edges. A script move of 20+ units in one frame is a teleport, not a walk.
- [x] Shapes for every scene object, and the Object's "Solid" setting. Code: `include/physics/Collision.h`. Test: `CollisionTest`
- [x] Character Movement component: gravity, ground check, jump (Space, from the ground only), walls, ceilings, slopes,
      edges, no tunnelling through thin walls at speed. Code: `Collision.h` (`moveCharacter`), `PlayPhysics.h` (how the
      scene's objects are stepped), `Engine::stepPlayPhysics`. Tests: `CollisionTest` (the mover and the scene stepping),
      `ObjectFileTest` (settings saved, file v8; older files load as solid with no character movement)
- [x] Hand test (done): build a floor and a wall from cubes, give the player Object a Character Movement component,
      Play: it stands on the floor, stops at the wall, slides along it, falls off the edge, jumps with Space
- [x] Traces (`trace from A to B`, `sweep`) are answered from these shapes: every solid object in the scene, with or
      without a script (`Physics::traceShapes`, the host's `trace` callback in `propulsion_ffi.h`, `Host::Trace` in the C++
      runtime). `Entity` is only the scripted object it hit. Tests: `CollisionTest`, `nodelist_types.rs`, `ffi.rs`,
      `differential_kits.cpp`, `PlayModeTest`
- [x] `when Self touches` / `stops touching` / `hits` between scripted objects: touching is overlapping or resting
      against (any shapes: `shapesOverlap`, GJK); a hit is a character blocked by a solid object, once per contact.
      Tests: `CollisionTest` (the tracking) and `PlayModeTest` (real scripts through the real interpreter)
- [~] View > Collision Shapes (F4): draws every object's physics shape as a wireframe over the editor viewport (a box's
      edges, a sphere's three circles, a cylinder, and a character's capsule). Hidden in Play, which shows just the game.
      Colors: orange = solid, green = a character's capsule, dim blue = doesn't block (non-solid), yellow = the
      selected object. It is drawn over the scene, not hidden by meshes (like the camera frustum), and uses the same
      `Shape` the physics uses, so what you see is what blocks. Code: `include/physics/CollisionOutline.h`,
      `Engine::drawCollisionShapes`. Test: `CollisionOutlineTest` (every drawn point lies on the physics shape). Built;
      waiting on a hand test
- [ ] Later: stepping up small ledges, being carried by a moving platform, jumping or launching from a script (needs
      system calls to know which object made them), hiding wireframes behind meshes, and showing the shapes in the
      Object editor preview

## 4. Input mapping (modeled on Unreal's Enhanced Input)
Design (agreed 2026-10-07): input is data, not a table in the code.
- **Input Action** assets (`.pinputaction`, one file per action, in `Assets/Input/`). The file name is the name
  scripts use (`jump`, `moveForward`; it must be a valid PropulsionScript name). Value type: **Button** (pressed or not:
  `Input is pressing jump`, `starts pressing`, `stops pressing`) or **Axis** (a decimal: `axis moveForward of Input`).
- **Input Mapping** asset (`.pinputmap`): one table of rows `action <- key`, each with modifiers **Negate** and
  **Scale**. Keys: keyboard, mouse buttons, and mouse movement (`Mouse X`, `Mouse Y`; the rate described in step 2).
  An axis action adds up its rows (W = +1 and S = Negate on `moveForward`); a button is pressed if any of its keys is.
- The player Object picks **one Input Mapping** (like adding a mapping context to the player in Unreal). Each script
  reads input through the mapping of the Object it runs on, so two characters can be bound differently, and an Object
  with no mapping gets no input.
- Scripts are checked against the project's Input Actions: a misspelled action is a compile error, as before.
- New and old projects: if a project has no Input Actions, the defaults (`moveForward`, `moveRight`, `lookX`, `lookY`,
  `jump`, `crouch`, `interact`) and a "DefaultInput" mapping that reproduces today's keys are created, and given to every
  Object that has a script.
Steps:
- [x] 4a. PropulsionScript: the input names come from the project (`propulsion_set_input_actions`, and `Scripts/inputs.propapi` for
      `propc` and the language server), and each read of an input says which entity is asking, so each Object's
      mapping can answer for itself. Tests: `inputs.rs`, `ffi.rs`, `differential.cpp` / `differential_examples.cpp`
- [x] 4b. Files and the resolver: `.pinputaction` / `.pinputmap` (`include/project/InputFiles.h`), key names
      (`InputKeys.h`), action names, and turning key states into action values per mapping (`InputMap.h`), plus the default
      input (`DefaultInput.h`). Test: `InputMapTest`
- [x] 4c. PlayMode reads input through each Object's mapping; the Object asset holds its mapping (file v9); a character's
      jump is an Input Action name (Character Movement's "Jump Action", replacing "Jump on Jump Input"). Tests:
      `PlayModeTest` (three players with different mappings and one with none, from injected key presses; rebinding),
      `CollisionTest`, `ObjectFileTest` (v8 files load, with `jump` for the old yes/no)
- [x] 4d. Editor (built; the tables and pickers need a hand test, see 4e): "Create Input Action" / "Create Input Mapping"
      in the Content Browser (they live in `Assets/Input/`); an Input Action's editor (Button or Axis, a note, how a
      script uses it, which mappings use it); an Input Mapping's editor (a table: Action, Key with a "Listen" button,
      Negate, Scale; "Not bound" shortcuts); the Object's "Input Mapping" picker (Object editor and Graph view). A project
      with no Input Actions gets the defaults and its scripted Objects the default mapping: checked by opening a project
      saved before this existed (it gained `Assets/Input/*` and the Player Object became v9 with the mapping).
- [~] 4e. Hand test: open your project. The Content Browser has an "Input" folder with `DefaultInput` and the seven
      actions, and your player Object's "Input Mapping" says DefaultInput. Play: it still walks, looks and jumps. Then open
      DefaultInput, change the `jump` row's key (Listen, press `F`), Ctrl+S, Play: the player jumps with F and not Space.
      Make a new Input Action (say `dash`), add a row for it, use `Input is pressing dash` in a script: it compiles.
- [ ] Later: gamepad, mouse wheel, dead zones, triggers (hold, tap), several mappings at once with priorities, a
      2D axis action (one `move` instead of `moveForward` + `moveRight`)

## 5. Finish
- [x] A finished `player.prop` (`PropulsionScript/behaviors/player.prop`): walks and strafes, turns and looks, counts and
      announces a jump press, keeps health. Its placeholder jump (`add 2 to Transform.Loc.Z`) is gone: the engine's Character
      Movement does the jump, and the floor, gravity and walls. Tests: `behaviors.rs`, `differential.cpp` (the script alone),
      and `PlayModeTest` (the real script on a Character Movement body with the default input: stands, walks level, jumps
      1.6 high from the floor and lands, announces once)
- [x] `PropulsionScript/cpp/README.md` and SPEC updated (input as data, traces, touches, per-entity input)

To set up a player Object by hand: a mesh, **Character Movement** (Fit to Mesh), **Spring Arm** with a **Camera** (arm length 0 =
first person), the player script, **Input Mapping** = DefaultInput, Jump Action = `jump`.

Known gap: the script announces a jump press even if the character is in the air (the engine ignores it). Fix when the
engine can tell scripts (`when Self jumps` / `lands`, or a grounded read).
