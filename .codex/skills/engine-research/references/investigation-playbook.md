# Investigation Playbook

Per-domain entry points for VertexForge research. Each recipe includes the layer path, key files to verify before relying on them, and a focused research prompt. File-location facts are sourced from `CLAUDE.md` and prior notes when present; re-verify, do not assume.

The spine of the engine is a strict layering (from `CLAUDE.md`):

```
Editor   --> Services, Import, Core, Plugin, ProceduralGen, ImageProcessing, GameExport, ECSRegistry
Runtime  --> Services, Core
Services --> Utilities, Terrain, Serialization, World, Window
Core     --> Graphics, Audio, Physics, Animation, mType, jolt, recast
Graphics --> Window, VFX, imgui
Plugin   --> Services, Utilities
```

Editor/Runtime never include Core or Graphics directly - they go through the **Services** layer (CQRS events) and the **provider/adapter** seam.

---

## Layer / architecture flow

How a feature reaches the renderer: `Editor window -> EventDispatcher (Command/Query) -> ServiceImpl -> Provider interface -> Adapter -> Controller -> Core/Graphics`.

- Services: `services/interfaces/I*Service.hpp`, `services/impl/`, `services/events/` (`EventDispatcher.hpp`, `EventTypes.hpp`).
- Provider seam: `services/providers/I*Provider.hpp` implemented by `core/adapters/*Adapter.{hpp,cpp}`.
- Startup wiring: `core/bootstrap/{Editor,Runtime}Bootstrap.*` creates Core/Graphics + adapters and hands providers to services.

**Research prompt:** "Trace how `<feature>` flows from the editor UI through the Services CQRS event to the Core adapter and controller. Return the event type, the ServiceImpl handler, the provider interface, and the adapter method with file:line for each hop."

## Rendering / Vulkan

- Renderer core: `graphics/`, GPU-driven passes in `graphics/render/gpudriven/` (`GPUDrivenRenderer*.cpp` - Scene, Terrain, Water, Vegetation, GI, DepthPrepass, Draw, Query, Compute).
- Preview controllers: `graphics/controllers/*PreviewController`.
- Shaders: source in `resources/shaders/`; **runtime gotcha** - the Editor reads from `bin/Editor/resources/shaders/`, so GLSL edits must be copied there to take effect.
- For Vulkan API semantics, pair with [external-research.md](external-research.md).

**Research prompt:** "Find where the `<pass>` render pass is recorded in graphics/render/gpudriven, which descriptor sets it binds, and which shader files it uses. Return file:line for the record site and the shader paths."

## Terrain

Layer path: `services/impl/scene/TerrainService.cpp` (CPU, edge sync) -> `graphics/render/gpudriven/TerrainGPUAdapter.cpp` / `TerrainStreamManager.cpp` (GPU upload) -> `task_terrain.glsl` / `mesh_terrain.glsl` (per-tile LOD).

- Types/config: `utilities/terrain/TerrainTypes.hpp`, tile data `utilities/terrain/TerrainTile.hpp`.
- Geometry: `utilities/terrain/TerrainTileGenerator.cpp`; grid: `utilities/terrain/TerrainGrid.cpp`.
- Key concepts: duplicated boundary vertices per tile, `syncTileEdges()`, `edgeStitchInfo`, ghost-triangle normals, GPU task shader picks LOD independently of CPU `currentLOD`.

**Research prompt:** "In utilities/terrain and TerrainService.cpp, explain how tile boundary heights stay crack-free across LOD transitions. Return the functions (syncTileEdges, getStitchedHeight, calculateNormals) with file:line."

## ECS / components

- Components: `utilities/components/Components.hpp` (add struct, use via `Entity::addComponent<T>()`/`getComponent<T>()`).
- Registry singleton: the EnTT registry lives in `ECSRegistry` (extracted from `utilities/scene/EntityRegistry.*`). **Rule:** any DLL touching `EntityRegistry` must `links { "ECSRegistry" }` + postbuild-copy so the singleton resolves once per process.

**Research prompt:** "Where is component `<T>` defined and which systems read/write it? Return the struct in Components.hpp and the call sites with file:line."

## Services / CQRS events

Three channels on `EventDispatcher::instance()`: `execute(Command)` (actions), `query(Query)` (reads), `publish(Notification)` + `subscribe/unsubscribe` (pub/sub). Event types grouped by domain in `services/events/*Events.hpp`.

**Research prompt:** "List the Commands/Queries/Notifications for `<domain>` in services/events and their handlers registered in registerEventHandlers(). Return file:line."

## Plugins / mType natives

- Engine-side SDK + loader: `plugin/api/` (`IPlugin.hpp`, `PluginContext.hpp`, `PluginExport.hpp`, `PluginVersion.hpp`, `PluginDescriptor.hpp`), loader in `plugin/core/`.
- Plugins live out-of-tree under `plugins/<Name>/`, compile against exported `sdk/` headers (NOT engine source). **Re-run `premake5 vs2022` after editing engine headers** so `sdk/` refreshes; plugin API version must match (ABI).
- mType script natives: static wrapper classes (`Entity::`, `Physics::`, `UI::`, ...). Language source at `C:\matan\mType`. See the `game-developer` skill for the script side.

**Research prompt:** "Find the plugin API version and which capabilities `PluginContext` exposes (custom pipeline, textures, script natives, editor windows). Return the methods with file:line and the current apiVersion."

## Subsystem DLLs

Large modules are split into SharedLib/StaticLib subsystems via `removefiles` in the parent plus a dedicated project (see the extraction table in `CLAUDE.md` if present: Audio, Animation, Terrain, Serialization, World, ProceduralGen, ImageProcessing, GameExport, ECSRegistry, ...). Each DLL has a `VF_<NAME>_BUILD_DLL` define and `VF_<NAME>_API` export macro. Build config: `premake5.lua`.

**Research prompt:** "Show how the `<Subsystem>` DLL is configured in premake5.lua - its sources (removefiles in parent), its build-DLL define, and which projects link it."

## Build / test

- Generate + build: `premake5 vs2022` then `msbuild VFEngine/VertexForge.sln /p:Configuration=Development /p:Platform=x64`. Single target: `/t:Editor`. Requires `VULKAN_SDK`. (See memory: full MSBuild path, `/t:"Engine\Editor"` solution-folder prefix, transient `/m` lib races.)
- Tests: doctest runner `bin/Tests/<Config>/x64/Tests.exe`; filter `--test-case=`, `--source-file=`, `--subcase=`. Add `test_<feature>.cpp` to `VFEngine/tests/` (auto-globbed).
- User builds themselves - research/plan output should hand off *what to rebuild*, not auto-run msbuild.

**Research prompt:** "Find the existing tests covering `<feature>` in VFEngine/tests and what they assert. Return the TEST_CASE names and file:line."
