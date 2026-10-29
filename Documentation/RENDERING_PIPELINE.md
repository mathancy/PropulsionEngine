# Rendering Pipeline: Ray Tracing, Path Tracing and DLSS

Status: **phase 1 (lights and HDRI in the raster renderer) is built, with raster shadows and ambient occlusion added after it; phases 2-5 are design only.** Written 2026-10-07; phase 1 notes in section 11, shadows and AO in section 12.

Goal: a rendering pipeline built on the existing Vulkan architecture that lets the editor viewport (and Play) render
in three modes, with real light objects and HDRI environments, and DLSS 4.5 on top where the GPU supports it.

| Mode | What it is | Needs RT hardware |
|---|---|---|
| **Raster** | The current forward renderer, driven by real lights and the HDRI (image-based lighting). Also the fallback. | No |
| **Hybrid RT** | Raster G-buffer plus ray-traced shadows, reflections and AO (optional one-bounce GI). | Yes |
| **Path Tracer** | Ray-gen shader with next-event estimation, HDRI importance sampling, MIS and GGX. Progressive accumulation in the editor, low spp plus a denoiser in Play. | Yes |

## 1. Where the engine stands (verified in the code)

| Area | Today | Consequence |
|---|---|---|
| Vulkan | Instance `apiVersion` 1.3 (`VulkanContext::createInstance`). Vulkan SDK 1.4.328, `glslc` present. | No new SDK needed. |
| Device | `createLogicalDevice` enables only `samplerAnisotropy`, `sampleRateShading`, `fillModeNonSolid`. Extensions: swapchain only. | Needs RT extensions, buffer device address, descriptor indexing, scalar block layout. |
| GPU choice | `pickPhysicalDevice` takes the first suitable device. The log shows the RTX 4070 Ti, but the machine also has an AMD iGPU. | Make the choice explicit: prefer an RT-capable discrete GPU. |
| Frame | Render passes (not dynamic rendering). Scene goes to a fixed 2048x2048 offscreen image (`Engine::render`, STEP 1), then selection mask, dilation, composite, gizmo blend (STEP 2.x), then ImGui. | The new pipeline replaces STEP 1 only. Outline and gizmos keep working in every mode. |
| Lighting | Hardcoded in `shaders/basic.frag`: two fixed directional lights and a constant `0.03` ambient. Tone map is a Reinhard approximation, not ACES. | Replace with a light buffer and an environment. |
| Materials | One dynamic UBO per object. Textures are bound **for the first visible object only** (`Engine::render`, "Only bind textures from first visible object that has them"). | Ray tracing needs every object's textures at once: a bindless material table. |
| Meshes | `Mesh` owns a plain vertex and index buffer (`Vertex` = position, normal, uv). No device-address or acceleration-structure usage flags. | Buffers need `SHADER_DEVICE_ADDRESS` and `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY` usage. Vertex data is also kept CPU-side (`getVertices`), which helps. |
| Memory | Raw `vkAllocateMemory` throughout, no allocator library. | New code uses one small allocation helper. VMA is optional, not required. |
| Components | Components live on Object assets (`.pobj`, version 10). The Camera is the model: `CameraSettings`, `ObjectFile`, `drawObjectCameraProperty`, `cameraOf`. | A Light component follows the same pattern (`.pobj` v11). |
| Image loading | `stb_image` is vendored, and `stbi_loadf` reads Radiance `.hdr`. | Polyhaven `.hdr` works with no new dependency. `.exr` would need `tinyexr` (later). |
| Coordinates | Z up, forward +Y, right +X. | A directional light shines along its object's forward (+Y). The HDRI sky mapping must be Z-up too. |

## 2. Architecture

```
Engine::render()
  └─ RenderPipeline::renderScene(frame, SceneView)          <- replaces STEP 1
       ├─ SceneExtractor      scene objects -> SceneRenderData (instances, lights, materials, environment)
       ├─ GpuScene            mesh/material/light tables + BLAS per mesh + TLAS per frame
       ├─ Environment         HDRI texture, importance-sampling CDFs, prefiltered IBL
       ├─ IRenderPath
       │    ├─ RasterPath     forward PBR (today's shader, real lights + IBL)
       │    ├─ HybridRtPath   G-buffer + RT shadows / reflections / AO + denoise
       │    └─ PathTracePath  rgen/miss/chit + accumulation
       └─ Upscaler (optional) DLSS Super Resolution / Ray Reconstruction via NGX
  output: the existing offscreen colour image, so mask / dilation / composite / gizmo are untouched
```

Rules:
- **Capability detection, not requirements.** `RtCapabilities` is filled at device creation. If the GPU lacks ray tracing,
  Hybrid RT and Path Tracer are greyed out in the UI and Raster is used. The thumbnail generator and Object-editor
  preview stay on Raster.
- **One scene description for all paths.** Light, HDRI and material data are extracted once per frame; each path reads
  the same structures.
- **Render size is separate from output size.** The fixed 2048x2048 offscreen image is a limitation DLSS will hit
  (it needs a render resolution smaller than the output). The pipeline works at an explicit render extent and
  upscales into the output extent.
- **Shaders** stay GLSL compiled by `glslc` (`GL_EXT_ray_tracing`, `GL_EXT_nonuniform_qualifier`,
  `GL_EXT_buffer_reference`). `compile_shaders.ps1` grows entries for the new stages.

## 3. Light component

New file `include/core/LightSettings.h`, modelled on `CameraSettings.h`:

```cpp
struct LightSettings {
    enum class Type { Directional, Point };
    bool      enabled   = false;
    Type      type      = Type::Point;
    glm::vec3 color     = {1, 1, 1};
    float     intensity = 1.0f;     // directional: lux-like scalar; point: candela-like scalar
    float     range     = 20.0f;    // point: raster cutoff only (ray-traced modes use physical falloff)
    float     radius    = 0.0f;     // point: sphere radius for soft shadows; directional: use angularDiameter
    float     angularDiameterDeg = 0.53f;  // directional: sun size, soft shadows
    bool      castShadows = true;
    void sanitize();
};
```

- `.pobj` **v11**: after the existing data, append the light block. Older files load as "no light", as with every
  earlier version bump. `ObjectFile::Data` gains `LightSettings light`.
- `ContentItem` gains `light`; `Engine::lightOf(const SceneObject&, LightSettings&)` mirrors `cameraOf`.
- UI: an entry in `drawAddComponentButton`, a `drawObjectLightProperty` panel, and an editor-only icon/gizmo in the
  viewport (hidden while playing, like the camera mesh).
- Spot lights are a natural third type later; the struct leaves room (`Type`).

## 4. HDRI environment

- New asset type **HDRI** in the content browser. Import `.hdr` (Polyhaven's default 1k-8k download). `.exr` is a later
  addition via `tinyexr`.
- Stored as RGBA16F (or RGB32F for sources under 4k) equirectangular image. Mapping is Z-up: `u = atan2(y, x)`,
  `v = acos(z)`, with a yaw offset setting.
- Per-scene environment settings: HDRI asset, intensity, yaw rotation, "show as background" toggle, and a flat colour
  fallback when no HDRI is set.
- Derived data, built on a background step when the HDRI changes:
  - **Raster:** diffuse irradiance (9-coefficient SH or a small cubemap), prefiltered specular cubemap with mips, and
    a BRDF LUT.
  - **Ray tracing:** marginal and conditional CDF buffers for importance sampling the sun and sky, plus the equirect
    image for miss shaders.
- Scene environment settings are saved in the project file (`ProjectSerializer`), not on an Object.

## 5. Ray tracing foundation

Device additions (enabled only if supported):

- `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_deferred_host_operations`
- `VK_KHR_ray_query` (used by Hybrid RT from fragment/compute shaders, so the G-buffer path needs no RT pipeline)
- Vulkan 1.2 features: `bufferDeviceAddress`, `descriptorIndexing` set (`runtimeDescriptorArray`,
  `shaderSampledImageArrayNonUniformIndexing`, `descriptorBindingPartiallyBound`, `descriptorBindingVariableDescriptorCount`,
  `descriptorBindingUpdateAfterBind`), `scalarBlockLayout`, `shaderFloat16`/`storageBuffer16BitAccess` if wanted.
- Vulkan 1.3: `synchronization2` (simpler barriers for the new passes). The existing render passes stay as they are;
  `dynamicRendering` is optional and only worth enabling if the new raster G-buffer pass benefits from it.

Resources:

- **BLAS per mesh**, built once when the mesh is created, compacted afterwards. `Mesh` exposes its buffers' device
  addresses. Rebuilt only if mesh data changes (not expected at runtime).
- **TLAS per frame in flight**, rebuilt from visible `SceneObject` transforms. Instance custom index = index into
  the instance table (mesh buffers, material id, normal matrix). Rebuild every frame at first; skip when nothing
  moved.
- **Instance table, material table, light table** as storage buffers, addressed by custom index. Materials carry
  indices into a **bindless texture array** (albedo, metallic, roughness, normal, AO, emissive, height). This also
  fixes the "first object's textures only" limit in Raster.
- Texture cache (`getOrLoadTexture`) hands out array slots instead of binding per draw.

## 6. Hybrid RT mode

1. Raster pass writes a G-buffer: depth, world normal + roughness, albedo + metallic, motion vectors, emissive.
2. Compute passes with `ray_query` (no separate RT pipeline needed):
   - **Shadows**: one ray per light per pixel; directional uses cone-sampled sun disc, point uses sphere sampling.
   - **Reflections**: GGX-sampled ray for rough-to-mirror surfaces, hit shaded with the same BRDF, miss samples the HDRI.
   - **AO / GI**: short-range cosine rays (AO), optional one diffuse bounce with HDRI at miss.
3. Temporal accumulation plus an à-trous / SVGF-style spatial filter on the noisy signals, then lighting composite.
4. Replaced by DLSS Ray Reconstruction when available (see section 8).

## 7. Path tracer mode

- Ray-gen / closest-hit / miss pipeline (`VK_KHR_ray_tracing_pipeline`) with a shader binding table.
- Per bounce: material fetch (bindless), GGX specular + Lambert diffuse lobe selection, **next-event estimation** to
  directional and point lights, **HDRI importance sampling**, **MIS** between light and BSDF samples, Russian
  roulette, emissive hits.
- **Editor behaviour**: progressive accumulation into an RGBA32F buffer; accumulation resets when the camera, an
  object transform, a light, a material or the environment changes. UI: samples per pixel target, max bounces,
  current sample count.
- **Play behaviour**: 1-2 spp per frame, then the denoiser (DLSS-RR, or the Hybrid filter as the non-NVIDIA fallback).
- Outputs the auxiliary buffers the denoiser needs: diffuse albedo, specular albedo, normal + roughness, motion
  vectors, linear depth, specular hit distance.

## 8. DLSS 4.5 (NGX directly)

Decision: integrate through **NGX using NVIDIA's public DLSS SDK** (github.com/NVIDIA/DLSS), not Streamline. This keeps
Vulkan device creation under our control (no interposer). NGX tells us which instance and device extensions it needs
(`NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements` / `...DeviceExtensionRequirements`), and `VulkanContext`
adds them.

- Everything sits behind an `Upscaler` interface: `None`, `DLSS_SR`, `DLSS_RR`. Nothing outside the DLSS backend
  includes NVIDIA headers.
- **Super Resolution**: needs depth, motion vectors, exposure, sub-pixel jitter (Halton) applied in the projection
  matrix, and a render extent smaller than the output extent. Used with Raster and Hybrid RT.
- **Ray Reconstruction**: replaces the denoiser for Path Tracer and Hybrid RT. Needs the auxiliary buffers from
  section 7. This is the DLSS 4.5 headline feature (second-generation transformer model, "Preset F").
- Redistribution: `nvngx_dlss.dll` / `nvngx_dlssd.dll` ship next to the exe; the SDK licence must be accepted before
  any binaries are committed or distributed. The SDK itself is fetched by `setup.ps1`, not committed.

**Risks to resolve early (not assumed away):**
- Ray Reconstruction on Vulkan is described by NVIDIA as beta. A Streamline issue
  ([#119](https://github.com/NVIDIA-RTX/Streamline/issues/119)) reports a `VK_ERROR_DEVICE_LOST` on the first RR
  evaluation on an RTX 5090 with drivers 610.88/616.56. It is a different path (Streamline, RTX 50 series) but shows
  the area is fragile. A one-day spike (a standalone RR evaluate on this machine's RTX 4070 Ti) comes **before**
  phase 5 is committed to.
- I have not yet read the NGX Vulkan headers in the current SDK; the extension and parameter names above come from
  NVIDIA's public documentation and need checking against the version we download.
- The NVIDIA driver on this machine reports Windows version 32.0.16.1714 (NVIDIA 617.14). Check the SDK's minimum
  driver for the DLSS 4.5 models before testing.

## 9. Phases

Each phase builds, runs and has tests where the logic can run headless (the repo already has `tests/` with
headless C++ tests, e.g. `asset_id_test.cpp`, `object_file_test.cpp`).

1. **Lights + HDRI in Raster.**
   `LightSettings`, `.pobj` v11 + round-trip test, `lightOf`, Light panel, light icon; HDRI asset import and
   viewer; environment settings in the project file; light and environment UBO in `basic.frag`; sky background;
   diffuse + specular IBL; tone map fixed. Done when: a point and a directional light light a scene, an HDRI
   lights and reflects, and a v10 file still opens.
2. **RT foundation.**
   Capability detection and explicit GPU choice; device features/extensions; buffer-device-address mesh buffers;
   BLAS/TLAS; instance/material/light tables; bindless textures (Raster uses them too). Done when: a debug
   ray-query pass can draw primary-ray hit normals, matching the raster silhouette, with validation clean.
3. **Hybrid RT.** Shadows, then reflections, AO, denoise. Mode switch in the viewport toolbar.
4. **Path tracer.** NEE + HDRI sampling + MIS; accumulation and its reset rules; UI.
5. **DLSS.** RR/SR spike first, then the `Upscaler` interface, jitter + motion vectors, render/output extent split.

## 10. Open items

- Render extent: replace the fixed 2048x2048 offscreen image with one sized to the viewport (needed by phase 5, and
  helpful earlier). This touches `VulkanContext` offscreen, selection mask, composite and gizmo images, which all
  assume one extent today.
- Transparency / alpha materials and the `discard` in the parallax path are not handled by ray tracing at first:
  ray-traced modes treat all surfaces as opaque, and height-map parallax is raster-only.
- Skinned or animated meshes: none exist today, so BLAS rebuild/refit is out of scope.
- `.exr` HDRIs, spot lights and area lights are follow-ups.

## 11. Phase 1 as built (lights and HDRI in the raster renderer)

What exists now, and where:

| Piece | Where |
|---|---|
| Light component (directional, point) | `core/LightSettings.h`; stored in the `.pobj` (version 11, see `project/ObjectFile.h`); edited in the Object editor and the Script Graph's component list (`ui/ObjectProperties.cpp`, `ui/ScriptGraphTab.cpp`) |
| Making lights | Content Browser right-click: **Create Directional Light** / **Create Point Light** (an Object with no mesh and the component on). Dropping one into the level puts a bulb 3 up and the sun 6 up, aimed down and across. |
| Editor pictures of lights | A glowing sphere for a bulb; a sphere with a cone along the shine direction for the sun. Shown in the level and in the Object editor's preview, never while playing. |
| Environment settings | `core/EnvironmentSettings.h`; saved in the project file (`.pproj` version 5): HDRI path (relative to the project), intensity, rotation about Z, sky as background |
| Environment UI | The **Environment** section at the top of the Objects panel (`ui/EnvironmentPanel.cpp`) |
| HDRI asset type | `.hdr` files import as type **HDRI** (into `Assets/HDRI`); double-click or right-click > Use as Environment |
| HDRI math (no GPU, tested) | `rendering/EnvironmentMap.h`: panorama mapping, diffuse spherical harmonics, GGX-filtered reflection chain, half floats |
| Light block packing (tested) | `rendering/LightingData.h` |
| GPU side | `rendering/SceneLighting.*` (descriptor set 1, the environment image and its mips, the sky pass); `shaders/lighting.glsl` (shared by `basic.frag` and `sky.frag`), `shaders/sky.vert/.frag` |
| Tests | `LightingTest` (new), `ObjectFileTest` and `ProjectFileTest` (extended) |

Behaviour worth knowing:

- **No light objects in the scene: the old two lights are used** (a warm key and a cool, diffuse-only fill), so existing
  projects still look lit. As soon as the scene has any light object (even one turned down to 0), only the scene's lights
  are used. At most 16 lights are used; the rest are ignored with a console message.
- **The sky replaces the old constant ambient.** It lights diffuse surfaces (9 spherical harmonic coefficients) and is
  reflected by shiny surfaces (a mip chain of GGX-filtered copies, `roughness * maxLod`), with Karis' analytic BRDF fit
  instead of a lookup image. With no HDRI the sky is a blue/pale/brown gradient. Existing scenes therefore look brighter
  and a little bluer than before; **Intensity** in the Environment section turns it down.
- **The Object and Material previews** are lit by the default two lights in the same sky, not by the level's lights.
- **HDRIs are read and prepared on another thread** and swapped in between frames. Measured in a Debug build: 1k, instant;
  4096x2048 about 4 s; 8192x4096 about 9 s (a Release build is several times faster). Larger than 8192x4096 is refused.
  The GPU copy is half floats: about 87 MB for 4k.
- **HDRI orientation:** the middle of the panorama looks along +Y; turning toward +X moves right across the picture; the
  top row is up (+Z). Verified with a test sky that has a sun and green (+Y) / red (+X) patches. Rotation is
  counter-clockwise seen from above.
- **Shadows and ambient occlusion** were added right after this phase, in the raster renderer: see section 12.
- **Reflection filtering above 512 pixels wide is a box average,** not GGX (the mips for roughness below about 0.3).
  They look right; a GPU filter in phase 2 would make them exact.

Follow-ups noticed while building this (none block phase 2):

- Auto-exposure still looks only at the objects' base colors, not at the lights or the sky.
- `.exr` HDRIs (needs `tinyexr`), spot lights and area lights.
- The thumbnail of an HDRI is a clamped LDR conversion (fine for a picture, not for judging brightness).

## 12. Raster shadows and ambient occlusion (added after phase 1)

The normal render mode now has soft shadows and screen-space ambient occlusion, so it is a decent fallback on GPUs without
ray tracing, and phase 3's ray-traced shadows and AO have something to be compared against.

| Piece | Where |
|---|---|
| Sun shadow map, point light shadow maps | `rendering/ShadowRenderer.*` (depth-only pass, `shaders/shadow.vert`) |
| Shadow geometry (tested) | `rendering/ShadowMath.h`; slot assignment and the matrices in the light block: `rendering/LightingData.h` |
| Soft filtering | `shaders/lighting.glsl` (`sunShadow`, `pointLightShadow`), used by `shaders/basic.frag` |
| Ambient occlusion | `rendering/AmbientOcclusion.*`: normals and depth pre-pass (`prepass.vert/.frag`), occlusion (`ssao.frag`), depth-aware blur (`ssao_blur.frag`) |
| Controls | Environment section of the Objects panel; saved in the project file (version 6) |
| Tests | `ShadowTest` (new); `ProjectFileTest` extended |

How it works:

- **Sun:** four cascaded orthographic maps of 2048x2048 (the same memory as the single 4096x4096 map they replaced). Together they
  cover the camera's view out to the **Distance** setting (default 100 units); the first covers only the stretch just in front of
  the camera, each next one a deeper and wider stretch. The reaches grow by a constant factor from about 1 unit out to Distance
  (`cascadeSplits` in `ShadowMath.h`; 3.2, 10, 32, 100 for 100), so a shadow's texels stay about as big as the screen's pixels:
  shadows are sharp close to the camera, coarser far away, and moving toward one sharpens it. This holds at any Distance. A pixel
  is looked up in the nearest map that reaches it (by its depth from the camera); over the last 15% of a map's reach it fades
  into the next, and shadows fade out over the last 10% of Distance. Each map is a sphere around its slice of the view, the same
  size however the camera turns, and its position moves in whole texels, so edges don't shimmer. Each reaches back toward the sun to include
  casters outside the view, and a caster is only drawn into the maps its bounding sphere overlaps. Only the first sun that has
  Cast Shadows on gets maps (the default rig's key light does; its fill doesn't).
- **Point lights:** the first 4 with Cast Shadows on get six 1024x1024 faces each (a 90 degree view along each axis). The shader
  chooses a face by the dominant axis, the same rule as `pointFaceOf` in C++; a light only draws casters within its Range.
- **Softness** is percentage-closer soft shadows: find what blocks the light, work out the penumbra from the light's size
  (Angular Diameter for the sun, Radius for a bulb) and the blocker's distance, and average the map over that width. Shadows are
  sharp where something touches the ground and soften with distance; a bigger light makes them softer. The sample pattern is
  rotated per pixel, so the filtering shows as fine noise instead of banding. Bias is a slope-scaled depth bias, an offset
  along the surface normal, and extra for wide filters.
- **Ambient occlusion** affects only the sky's light (diffuse and reflections), never direct lights (they have shadows).
  16 samples per pixel in a hemisphere of the Radius setting, turned per pixel, then a 5x5 blur that doesn't cross depth edges.
  It runs at the scene target's size (2048x2048).
- The scene shader reads the previews' occlusion as white and has no shadows in the Object and Material previews.
- The scene's draws are now built into a list first (their uniform blocks written once), then used by the shadow passes, the
  pre-pass and the scene pass.

Costs and limits:

- Memory: about 64 MB for the sun's maps, 96 MB for the point maps, about 40 MB for occlusion.
- Every frame redraws every caster into the sun's maps that it overlaps (up to four), and into each face of each shadowed bulb
  that reaches it. That is fine for editor scenes; static shadow caching is a follow-up.
- Not casting: the editor's light and camera pictures, and the world grid. Everything with a mesh casts and receives.
- The sun's maps cover the view out to **Distance**; beyond it, no shadows. Bringing Distance down sharpens the far maps, not
  the nearest. A softer penumbra than about 64 texels of the map it is read from is clipped (a very large Angular Diameter up
  close shows a sharper edge than a real sun would).
- **Why a shadow is as dark as it is:** a shadow only removes that one light's direct light. What is left is the sky (with
  ambient occlusion) and every other light, with no light bounced from other surfaces, so a shadow from the scene's strongest
  light (usually the sun) removes more of what a surface receives than one from a bulb whose light has faded with distance. Measured
  in single-light test scenes, a floor in the sun's shadow was about 42% as bright as the lit floor, and in a bulb's shadow about
  43%: shadows from the two kinds of light look alike when each is the only light. Bounced light, and sky light reaching into
  shadows, come with the ray-traced modes.

## 13. Ray tracing foundation and Hybrid RT (phase 2, and the first part of phase 3)

Built, compiled and covered by headless tests; **not yet looked at on screen by anyone but me for the debug view** (the user runs the
visual checks, see the list at the end).

What exists:

- **Capability detection** (`VulkanContext`, `RayTracingSupport.h`): every GPU is described (usable, discrete, ray queries, memory).
  The best usable one is chosen: ray queries first, then discrete, then memory. `PROPULSION_GPU=<number or part of the name>` forces one;
  `PROPULSION_DISABLE_RT=1` starts without ray tracing (to test the fallback). Extensions and features are enabled only if the GPU has
  them: acceleration structures, ray query, deferred host operations, ray tracing pipeline (if present), buffer device address,
  descriptor indexing (runtime arrays, partially bound, update after bind), scalar block layout, 64-bit integers. The log says what
  was found ("Ray tracing: ray queries and ray tracing pipelines").
- **Mesh buffers** are created with device addresses (only on such GPUs).
- **`RayTracing`**: one BLAS per mesh (built the first time it is traced, waiting for the GPU once; stored on the `Mesh`), a TLAS per
  frame in flight rebuilt every frame from the visible objects, and an instance table (vertex/index addresses, normal matrix,
  material index) the shaders read with buffer references (`shaders/raytracing.glsl`).
- **Debug view** (Environment panel, Ray Tracing, Debug View; or `PROPULSION_RT_DEBUG=1|2`): a compute pass shoots one primary ray per
  pixel and draws the hit normal or a colour per object. It is the phase's "done when" check: silhouettes should match the normal view.
- **Render mode** (`RenderMode.h`, project file version 7, picker over the viewport's top-right corner): Raster or Hybrid RT. A project
  that asks for Hybrid RT on a GPU without ray queries draws as Raster. The ray count per light and per occlusion is stored too (16 each;
  no UI yet).
- **Hybrid RT** = the normal forward shader with ray queries in it (`basic_rt_frag.spv`, built from `basic.frag` with `HYBRID_RT`; the
  scene shader gets a second pipeline, `Shader::addFragmentVariant`). Shadows: rays from the surface to points spread over the light's
  disc or sphere, so penumbrae come from the light's real size. Every light with Cast Shadows on casts (no 1 sun / 4 bulbs limit, no
  shadow maps drawn, no Distance). Ambient occlusion: cosine-weighted rays within the AO Radius, no depth pre-pass. The noise is a fixed
  per-pixel pattern (no temporal filter yet), so a low ray count looks grainy.

Decisions that differ from the plan above:

- The path tracer uses the ray tracing pipeline (ray generation, closest-hit, miss, any-hit, with a shader binding table), as section 7 says, not
  ray queries from a compute shader. Image quality is the same either way (it comes from the sampling, not the dispatch); the pipeline
  is faster with varied materials and several bounces (shader execution reordering on Ada, separate shaders per stage) and has any-hit and
  callable shaders. Hybrid RT stays on ray queries in the forward fragment shader.
- BLAS compaction is not done yet (the scenes are small). The bindless texture array and material table are not built yet: they are
  built when the first thing needs them (ray-traced reflections and the path tracer), and the instance table already has the field for
  a material index.
- Hybrid reflections and a denoiser are still to do. GPU names with ray queries are checked at start; a failure to create the ray
  tracing objects logs an error and the editor carries on as Raster.

Known issue noticed on the way (not changed): `basic.frag` applies `pow(x, 2.2)` to texture colours that are already decoded from sRGB by
the `R8G8B8A8_SRGB` texture format, and reads metallic, roughness and occlusion maps through the same sRGB format. Textured materials are
therefore darker than intended, and data maps are curved. Worth fixing before comparing ray-traced shading with Raster.

## 14. Transparent materials and the texture table (first part)

Requested after phase 2: alpha (foliage) and translucency (glass), in every render mode. Built so far: the material side, and the Raster
scene pass. Not built yet: transparency in shadow maps, in the ambient occlusion pre-pass, in Hybrid RT's rays, and in the Path Tracer.

- **Material settings** (`core/MaterialSettings.h`; Material editor, above the node graph): Blend Mode (Opaque, Masked, Translucent),
  Opacity, Cutoff, Two Sided, Refraction (index of refraction), Thin Walled, Tint Distance. They are kept in the `.pmat` file as a tagged
  block after the splitter ratio (`project/MaterialSettingsFile.h`), so older files read as Opaque and older engines ignore it.
  The Opacity and Opacity Mask pins of the Final Material node now take an Image node (its red channel is the alpha); with neither
  connected, a Masked or Translucent material uses the Base Color texture's alpha channel.
- **Texture table** (`rendering/TextureTable.h`): every material texture in one array the shader indexes (set 2, partially bound), with a
  set per frame in flight, brought up to date once per frame before recording. Each object's uniform block holds the slots it uses
  (`textureSlots`, `textureSlots2`) and its `surfaceParams`. This replaces binding the first textured object's textures: every object has
  its own now. The GPU must support runtime descriptor arrays with non-uniform indexing (all current desktop GPUs); it is required at start.
- **Colour spaces fixed:** each picture has two views, one decoded from sRGB (base colour, emissive) and one read as stored (metallic,
  roughness, normals, occlusion, height, opacity). The scene shader no longer applies `pow(x, 2.2)` on top of the decoding, so textured
  materials are brighter than before (they were too dark), and normal and data maps are no longer curved. Pictures now have mip chains.
- **Raster:** Masked discards below the cutoff; Two Sided draws both faces (cull mode is dynamic per draw, normal flipped on back faces).
  Translucent surfaces are drawn last, farthest first (sorted by object centre), blended with premultiplied alpha (diffuse and emissive scaled by
  opacity, reflections not), without writing depth, so glass keeps its highlights. They don't darken the sky light and don't block Hybrid RT's rays yet.
- **Known limits for now:** Masked surfaces cast solid shadows and darken occlusion as if solid; Hybrid RT treats them as solid too; glass
  casts no shadow. The Path Tracer will do all of it properly (alpha-tested any-hit shaders, refraction with Fresnel, rough transmission, absorption).
