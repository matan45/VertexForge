# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working Precision (make no mistakes)

Default to maximum precision and self-verification on every task:
- Verify facts, file contents, symbols, and API signatures against the source before acting — don't rely on memory or assumption.
- Mentally test code and re-derive any calculation before committing to it.
- State uncertainty explicitly rather than guessing; prefer accuracy over speed.
- Preserve behavior exactly when refactoring unless a change is the explicit goal.

## Build Commands

```bash
# Generate Visual Studio 2022 solution
premake5 vs2022

# Build from command line (after generating solution)
msbuild VFEngine/VertexForge.sln /p:Configuration=Development /p:Platform=x64
msbuild VFEngine/VertexForge.sln /p:Configuration=Debug /p:Platform=x64
msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64

# Build a single project (faster iteration)
msbuild VFEngine/VertexForge.sln /t:Editor /p:Configuration=Development /p:Platform=x64
msbuild VFEngine/VertexForge.sln /t:Tests  /p:Configuration=Debug /p:Platform=x64
```

**Configurations** (`vfStandardConfigs` in `premake5.lua`):
- **Debug** — `DEBUG`, symbols, no optimization. Adds `JPH_ENABLE_ASSERTS` (Jolt) and `VF_ENABLE_VALIDATION` (Vulkan validation layers).
- **Development** — the day-to-day play-test build. Optimized (`/O2`, `NDEBUG`) **with** debug symbols, plus `VF_DEVELOPMENT` for editor-only niceties. Vulkan validation stays ON (user preference; see the `Development` filter on Graphics) but Jolt asserts are OFF.
- **Release** — `NDEBUG`, optimized, no validation, no asserts.

Note the Editor stays a `ConsoleApp` in all three configs (console logs always visible — user preference). Shipped games are built by GameExport from Runtime, not by switching the Editor config.

**Prerequisites**: Vulkan SDK installed with `VULKAN_SDK` environment variable set.

**Toolchain**: C++20, MSVC toolset `v145` (VS2026), latest Windows SDK. Global flags `/utf-8 /MP /bigobj` (`/bigobj` because some TUs — e.g. `HierarchyService.cpp`, Tests — exceed the default COFF section limit instantiating templates over the full component inventory). Engine-wide defines: `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1`, `GLM_FORCE_DEPTH_ZERO_TO_ONE` (Vulkan [0,1] depth range).

**Output**: Executables in `bin/Editor/<Config>/x64/`, `bin/Runtime/<Config>/x64/`, `bin/Tests/<Config>/x64/`. Each SharedLib subsystem also lands in `bin/<Subsystem>/<Config>/x64/` and is copied into Editor/Runtime/Tests output dirs via `postbuildcommands`.

## Tests

The `Tests` project (`VFEngine/tests/`) is a doctest-based CPU-only unit test runner — no rendering dependencies. Each `test_*.cpp` registers its own `TEST_CASE`s.

```bash
# Run the full suite
bin/Tests/Debug/x64/Tests.exe

# List / filter (doctest CLI)
bin/Tests/Debug/x64/Tests.exe --list-test-cases
bin/Tests/Debug/x64/Tests.exe --test-case="*terrain*"
bin/Tests/Debug/x64/Tests.exe --source-file="*test_terrain*"
bin/Tests/Debug/x64/Tests.exe --subcase="boundary stitching"
```

Add new tests by dropping a `test_<feature>.cpp` into `VFEngine/tests/` — premake globs it automatically. `main.cpp` is the doctest entry point; do not add a second.

## Architecture

### Module Dependency Graph

```
Editor   ──> Services, Import, Core, Plugin, Mcp, ProceduralGen, ImageProcessing, GameExport, ECSRegistry (ONLY)
Import   ──> Utilities (SharedLib/DLL, requires CMake-built libs: assimp, freetype, libogg, libvorbis)
Runtime  ──> Services, Core (ONLY)
Services ──> Utilities, Terrain, Serialization, World, Window
Core     ──> Graphics, Audio, Physics, Animation, mType, jolt, recast
Graphics ──> Window, VFX, imgui
Plugin   ──> Services, Utilities (engine-side plugin SDK + loader)
Mcp      ──> Services, Import, Utilities (MCP server for AI agents; NO Core/Graphics/ImGui)
```

**Premake groups**: `Engine` (Editor, Runtime, Core, Graphics, Window, Import, Services, Plugin, Mcp, Utilities), `Subsystems` (extracted modules — see table below), `libs` (vendored third-party), `Plugins` (sample/external plugin DLLs under `plugins/`).

Module kinds: Editor/Runtime/Tests are `ConsoleApp`. Most modules are `StaticLib`. `SharedLib/DLL`: Import, Audio, Animation, Terrain, World, Serialization, ProceduralGen, ImageProcessing, GameExport, ECSRegistry, jolt, meshoptimizer, and external plugins.

### Subsystem Extraction

Large modules are split into separately compiled subsystem projects using `removefiles` in the parent and dedicated projects for each subsystem. This reduces compile times and clarifies boundaries.

| Subsystem | Extracted From | Compiles | Kind |
|-----------|---------------|----------|------|
| Audio | Core | `core/audio/**` | SharedLib (`VF_AUDIO_BUILD_DLL`) |
| Physics | Core | `core/physics/**` | StaticLib |
| Animation | Graphics + Utilities | `graphics/animation/**` (except `AnimationComputePipeline`, kept in Graphics) + `utilities/animator/**` | SharedLib (`VF_ANIMATION_BUILD_DLL`) |
| VFX | Graphics + Utilities | `graphics/render/vfx/**` + `utilities/vfx/**` | StaticLib |
| Terrain | Utilities | `utilities/terrain/**` | SharedLib (`VF_TERRAIN_BUILD_DLL`) |
| Serialization | Utilities | `utilities/serialization/**` + `utilities/world/{WorldDefinition,WorldSector,HLOD}Serialization.*` | SharedLib (`VF_SERIALIZATION_BUILD_DLL`) |
| World | Utilities | `utilities/world/**` (excluding serialization files) | SharedLib (`VF_WORLD_BUILD_DLL`) |
| Memory | Utilities | `utilities/memory/**` | StaticLib |
| Weather | Utilities | `utilities/weather/**` | StaticLib |
| Destruction | Utilities | `utilities/destruction/**` (uses v-hacd) | StaticLib |
| DataTypes | Services | `services/data/**` (header-only, `kind "None"`) | None |
| ProceduralGen | Utilities | `utilities/procedural/**` | SharedLib (`VF_PROCEDURAL_BUILD_DLL`) |
| ImageProcessing | Utilities | `utilities/imageprocessing/**` | SharedLib |
| GameExport | Utilities | `utilities/export/**` + `utilities/archive/VFPakWriter.*` (shader pre-compilation, `.vfpak`) | SharedLib (`VF_GAMEEXPORT_BUILD_DLL`) |
| ECSRegistry | Utilities | `utilities/scene/EntityRegistry.*` (the EnTT registry singleton) | SharedLib (`VF_ECSREGISTRY_BUILD_DLL`) |

**ECSRegistry rule**: any DLL that touches `EntityRegistry` (Audio, Serialization, World, Terrain, Animation, Tests, etc.) must link `ECSRegistry` so the singleton resolves to one instance across the process. Adding a new SharedLib that uses entities? Add `links { "ECSRegistry" }` and a postbuild copy.

**Key constraints**:
- Editor accesses rendering/scene/input through Services layer APIs (never directly include Core or Graphics)
- Editor calls Import directly for asset import operations (no service wrapper needed)
- Runtime accesses engine functionality only through Services layer APIs

### Key Patterns

- **Provider Interfaces** (in `services/providers/`): Abstract interfaces that define engine capabilities. Core implements these via adapters.
- **Adapters** (in `core/adapters/`): Implement provider interfaces by wrapping Core/Graphics controllers. Enable dependency inversion.
- **Bootstrap** (in `core/bootstrap/`): Initialize Core/Graphics and create adapters. `EditorBootstrap` and `RuntimeBootstrap` encapsulate startup.
- **Controllers** (in `*/controllers/`): Internal facade interfaces within Core/Graphics. Not exposed to Editor/Runtime.
- **Handlers**: Internal orchestrators that coordinate subsystems within a module.
- **ECS**: EnTT-based. Entities wrap `entt::entity` handles; components defined in `utilities/components/Components.hpp`.
- **Async Resources**: `ResourceManager` loads assets via `std::async`, returns futures.
- **Event System**: `EventDispatcher` singleton for decoupled communication via CQRS pattern (see Services Layer below).

### Entry Points

| Target | Entry | Description |
|--------|-------|-------------|
| Editor | `VFEngine/editor/run/Main.cpp` | ImGui-based editor |
| Runtime | `VFEngine/runtime/run/Main.cpp` | Standalone game runtime |

## Project Structure

```
VFEngine/
├── core/                    # Engine core - lifecycle, bootstrapping, adapters
│   ├── adapters/            # Provider implementations (OffScreenAdapter, PreviewAdapter, etc.)
│   ├── bootstrap/           # EditorBootstrap, RuntimeBootstrap - initialization orchestration
│   ├── controllers/         # Internal facades (CoreInterface, OffScreen, EditorTextureController)
│   ├── core/                # MainLoop
│   └── handlers/            # GraphicsHandler, WindowHandler
├── graphics/                # Vulkan rendering
│   └── controllers/         # MaterialPreviewController, MeshPreviewController, etc.
├── window/                  # GLFW window, input handling
├── utilities/               # ECS components, scene graph, resource loading, serialization
│   ├── procedural/          # ProceduralGen DLL - heightmap noise generation
│   └── imageprocessing/     # ImageProcessing DLL - background removal
├── import/                  # Asset import pipeline (mesh, texture, audio, animation)
├── services/                # Service layer - abstraction for Editor/Runtime
│   ├── providers/           # Provider interfaces (IOffScreenProvider, IPreviewProvider, etc.)
│   ├── interfaces/          # Service interfaces (IRenderService, IPreviewService, etc.)
│   ├── impl/                # Service implementations
│   ├── events/              # CQRS events (Commands, Queries, Notifications)
│   └── data/                # DTOs, EntityHandle
├── editor/                  # Editor application
│   ├── handlers/            # EditorHandler, WindowImguiHandler
│   ├── windows/             # ImGui panels (MaterialEditorWindow, MeshPreviewWindow, etc.)
│   │   ├── procedural/      # Heightmap generator tool window
│   │   └── imageprocessing/ # Background removal tool window
│   └── run/                 # Main.cpp entry point
├── runtime/                 # Standalone game runtime
│   ├── handlers/            # RuntimeHandler
│   └── run/                 # Main.cpp entry point
├── plugin/                  # Engine-side plugin SDK + loader (links into Editor)
│   ├── api/                 # IPlugin, PluginContext, PluginDescriptor, PluginExport, PluginVersion
│   └── core/                # PluginManager, DynamicLibrary, PluginContextImpl, PluginEventBus
├── mcp/                     # MCP server StaticLib (links into Editor + Tests) — see "MCP Server" below
│   ├── protocol/            # JsonRpc, McpServer (lifecycle + tools/*), ToolRegistry, ArgReader
│   ├── transport/           # HttpMessage parser, loopback HttpServer (winsock), SecurityPolicy
│   ├── dispatch/            # MainThreadQueue (connection thread -> editor main thread)
│   └── tools/               # Tool groups (EditorTools, SceneTools, ...) + CoreTools registration
└── tests/                   # doctest-based unit tests (Tests.exe)
    ├── main.cpp             # doctest entry point — do not duplicate
    └── test_*.cpp           # one TU per feature area; auto-globbed by premake
```

External plugins live outside `VFEngine/` under `plugins/<PluginName>/` (e.g. `plugins/PluginAPITest/`, `plugins/HexTerrain/`). Each plugin folder is self-contained:
- carries its own `premake5.lua` (one line: `vfPluginProject("<Name>")` — shared setup in `plugins/plugin_sdk.lua`) — auto-discovered by the root `premake5.lua` under `group "Plugins"`, no root edits needed
- compiles against the exported SDK headers in `sdk/` (NOT engine source) — `premake5 vs2022` auto-refreshes `sdk/` on every run, so **after editing engine headers re-run premake before building plugins**
- ships a `<PluginName>.vfplugin` descriptor (JSON) alongside the DLL
- copies its DLL back into `plugins/<PluginName>/` via postbuild, where `PluginManager` discovers it at runtime
- out-of-tree development: `premake5 export-sdk` packages `sdk/` (headers + imgui.lib + project template); see `sdk/README.md`

### MCP Server (AI agent control)

The Editor embeds a Model Context Protocol server (`VFEngine/mcp/`, owned by `editor/mcp/EditorMcpHost`) so an AI agent such as Claude Code can build scenes, write and build mType scripts, run Play mode and read logs. It lives in Editor.exe because `EventDispatcher` is per-binary. Transport is Streamable HTTP on **127.0.0.1 only** (`POST /mcp`, one JSON response per request) — stdio is impossible because every `vfLog*` writes to stdout. `GET /mcp` with `Accept: text/event-stream` opens the standalone SSE stream (`transport/EventStreamHub`: at most 2 streams, a 3rd evicts the oldest, 15 s `: keepalive`, chunked) used only for server→client notifications (`notifications/tools/list_changed`); `McpService::stop()` order is `queue.shutdown → hub.close → http.stop`, and the hub mutex is never held during a socket send.

- **Enable**: Preferences > AI / MCP (enable, port, optional bearer token, "Copy Claude Code command"), or per session with `Editor.exe <project> --mcp` / `--mcp-port <n>` / `--mcp-token <t>` (command-line flags are never saved; applying an MCP preference change takes over). The status bar shows `MCP off` / `MCP :<port>` / `MCP error`.
- **Connect**: `claude mcp add --transport http vertexforge http://127.0.0.1:7878/mcp` (add `--header "Authorization: Bearer <token>"` when a token is set).
- **Adding a tool**: add a `mcp::ToolDef` (name, description, JSON `inputSchema`, handler returning `ToolResult::ok/error`) to the matching `register*Tools` group in `mcp/tools/*.cpp`; new groups are declared in `CoreTools.hpp` and called from `registerCoreTools`. Tools use only Services/Import/Utilities headers and reach the engine through `EventDispatcher`.
  - **Affinity**: `ThreadAffinity::Main` (default) runs the handler on the editor main thread inside `EditorMcpHost::drain()` — required for anything touching EventDispatcher handlers, EnTT or ImGui. `ThreadAffinity::Worker` runs on the HTTP connection thread and is only for filesystem/long work (script writes, imports); such a handler must wrap every dispatcher call in `context.runOnMain(...)`. Raise `timeout` for long tools (build/import).
  - **Drain placement** (VK-1653): `EditorHandler`'s frame callback drains MCP FIRST, before `frameTaskGraph->execute()`, because only there is the render thread idle (`MainLoop` already waited in `beginFrame()`; the graph's `Render` task wakes it via `FrameSynchronizer::endFrame()`, after which it walks EnTT and the terrain grids). Never move the drain after `execute()`. Corollary: a Main tool must never wait for a rendered frame — nothing renders until the drain returns; only Worker tools poll across frames (`viewport_screenshot`, `terrain_generate_heightmap`).
  - **Capture by value**: handlers outlive the `register*Tools` call, and a main-thread task can still run after its caller timed out, so handler and `runOnMain` lambdas capture the `ToolContext`, arguments and any other state BY VALUE — never `[&]`.
  - Bad arguments throw `ArgError` from `ArgReader` helpers and engine failures throw or return `ToolResult::error`; both reach the agent in-band as `isError:true` results. Only a main-thread timeout is a JSON-RPC error (-32001).
  - **Images**: return `ToolResult::image(base64, "image/png", structured)`; the block is appended after the text block. `mcp/util/ImageEncode` holds the half→RGBA8, downscale, PNG (the Mcp copy of `stb_image_write`, static) and base64 helpers. `viewport_screenshot` polls `RequestViewportReadbackCommand` / `TakeViewportReadbackQuery` (GPU copy recorded in `OffScreenViewPort::render`, completed after that frame slot's fence).
  - **Editor-only tools** that need editor objects (e.g. `camera_*` on `EditorCamera`) are registered from `EditorMcpHost::init()` after `registerCoreTools`, not in `mcp/tools/`.
  - **Undo**: every entity/component/`material_assign` mutation records `mcp/undo` commands through `UndoRecorder`, which pushes ONE `BatchUndoCommand` via `PushUndoableCommand` after the edit succeeds. Never dispatch `BeginBatchCommand`/`EndBatchCommand` from MCP — an async file operation may hold the batch open. Re-created entities get new entt ids; undo commands store original ids and resolve them through `EntityIdRemap` (cleared on `SceneClearedNotification`). `.vfAnimator` edits are the one asset exception: they ARE undoable through the hash-guarded `AnimatorAssetUndo` (see Animation tools). Asset creation (`material_create`, a new `.vfAnimator`) and the other asset, script, scene, play and camera tools are not undoable.
  - **Plugin components** are reached through the JSON events in `services/events/scene/PluginComponentEvents.hpp` (handled by `editor/handlers/PluginComponentHandler`, strict patching via `utilities/serialization/MetaFieldPatch.hpp`). Never copy-construct the `meta_any` returned by `bridge.metaType.from_void(ptr)` — the copy is owning and writes are lost.
- **Resources** (`protocol/ResourceRegistry`, registered in `mcp/tools/ResourceDefs.cpp` → `registerCoreResources`): `vf://scene/hierarchy`, `vf://logs`, `vf://docs/components`, `vf://docs/mtype-api`, templates `vf://scripts/{+path}` (sandboxed like `script_read`) and `vf://docs/mtype-api/{+module}`. A reader throws `ResourceNotFound` for -32002. Readers share helpers with the tools (`mcp/tools/ContentHelpers.hpp`). `vf://docs/mtype-api` = the embedded primer in `MTypeApiDoc.cpp` (every rule verified against mType/engine source; `PlayerMovement.mt`/`Projectile.mt`/`AnimatorLocomotion.mt` quoted verbatim and guarded by `test_mtype_api_parser`) + signatures parsed live from `<project>/scripts/lib`. Keep the primer accurate: it is what lets an agent's first script compile.
- **Prompts** (`protocol/PromptRegistry`, `mcp/tools/PromptDefs.cpp`): `create_platformer_template`, `create_top_down_template`, both with an optional animated-player step (VK-1656). Prompt text may name only tools that exist (`test_mcp_prompts` enforces it).
- **Plugin tools** (API v22): `PluginContext::registerMcpTool(PluginMcpToolDesc)` (editor capability; agent sees `<plugin>_<tool>`; handler runs on the main thread). The plugin's handler stays in `plugin/core/PluginMcpToolRegistry` — MCP reaches it only by name through `services/events/editor/PluginMcpToolEvents.hpp` (handled by `editor/handlers/PluginMcpToolHandler`), so nothing in `mcp/` can outlive a plugin DLL. `mcp/tools/PluginToolBridge` mirrors them into the `ToolRegistry` group `"plugins"` (it can never shadow a core tool) on start and on `drain()`, and broadcasts `tools/list_changed`. Tools of a plugin deactivated for the current scene (VK-1365) are hidden. `ToolRegistry::find` returns `shared_ptr` — capture that, never a raw `ToolDef*`.
- **Terrain tools** (VK-1653, `mcp/tools/TerrainTools.cpp`): `terrain_get_info/create/generate_heightmap/sculpt/paint_layer/add_layer/set_layer/height_at/save/delete`.
  - Edits go through the self-describing AUTHORING events in `services/events/terrain/TerrainAuthoringEvents.hpp` (`SculptTerrainStrokeCommand`, `PaintTerrainLayerStrokeCommand`, `ApplyHeightmapCommand`, `ListTerrainsQuery`, `GetTerrainHeightsQuery`, `SaveTerrainsCommand`) — never the editor brush commands (they read tool-mode state) nor the VK-1624 `terrainEdit` runtime events (transient: no markDirty, no undo). They drive the same mode-free cores as the editor brush (`TerrainService::applySculptDab` / `applyLayerPaintDab`); raise/lower `amount` and paint `strength` are per STROKE, normalised by `terrain::strokePeakOverlap` (`utilities/terrain/BrushStroke.hpp`).
  - **Undo**: each call is exactly zero or one VK-1615 stroke entry pushed by `TerrainService` itself (`beginStroke(..., label)` → `finalizeTerrainStroke()`); never wrap terrain edits in `UndoRecorder`.
  - **Persistence**: terrain data lives only in the `.vfTerrain`; `SaveSceneCommand` does not write it. MCP `scene_save` runs `SaveTerrainsCommand` (lock → prepare → save → flush → unlock, incremental only into the terrain's own file) for every unsaved/dirty terrain first, `terrain_create` saves immediately, and `play_start` refuses unsaved terrain (Play→Stop reloads terrain from disk).
  - `.vfTerrainMat` layers via `services/events/terrain/TerrainMaterialAssetEvents.hpp` (edits the shared `ResourceManager` cached instance, then `TerrainMaterialCompiledNotification`). Heightmap generation via `services/events/terrain/HeightmapGenerationEvents.hpp`, handled by `editor/handlers/HeightmapGenerationHandler` because ProceduralGen.dll is editor-only; presets live in `procedural/generator/HeightmapPresets`.
  - Generic entity tools refuse Terrain/TerrainTile entities (`rejectTerrain`); the hierarchy collapses tile children into `terrainTileCount`. All event paths are absolute; agent paths are project-relative and sandboxed.
- **Environment tools** (VK-1654, `mcp/tools/EnvironmentTools.cpp` + `EnvironmentToolSupport`): `environment_get/set` (atmosphere, clouds, time of day, sun), `ibl_set/bake`, `gi_get/set/debug`, `fog_get/set` (volumetric + height fog), `weather_get/set`.
  - Every environment setting exists twice: AUTHORED in the scene's `types::RenderSettings` (`Get/SetRenderSettings`, saved to `.vfSettings`; IBL = the root `IBLComponent`) and LIVE in the renderer (`Apply*`). `env::writeSections` re-reads the settings fresh once, replaces only the given sections, stores them, pushes each live copy at most once, then publishes `RenderSettingsChangedNotification` (`services/events/scene/SceneSettingsEvents.hpp`) so the config windows reload. Never write a whole `RenderSettings` from a cached copy — editor windows merge the sections they don't own.
  - The field tables (`env::atmosphereTable/cloudTable/fogTable/giTable`; the generic table machinery is `mcp/tools/SettingsTable.hpp`) are the single source of keys and ranges; every range lies inside the scene loader's clamp (`tests/test_mcp_environment_ranges.cpp` round-trips min/max through save + load). Updates are strict and all-or-nothing; floats are reported shortest-round-trip (`env::floatJson`) so a `*_get` value is always accepted by `*_set`.
  - Atmosphere live writes use `ApplyAtmosphereSettingsNoSunCommand` (no editor auto-"Sun" create/delete); the tools create/rotate the sun light themselves, undoably. The day-night cycle never ticks in edit mode, so `timeOfDay` goes through `dayNightAngles` + `directionalLightEulerForSun`; `GetSunEntityQuery` returns the light the renderer uses as the sun.
  - GI is per scene: `GIServiceImpl` (Editor only) applies `RenderSettings.gi` on `RenderSettingsChangedNotification{sceneLoad=true}` (published by `ScenePersistenceService` after newScene/finishLoad); `OffScreenController` retries GI and volumetric fog until the GPU-driven renderer is initialized; structural GI changes rebuild via `giSettingsRequireRebuild` (`graphics/render/gi/GIRebuildPolicy.hpp`).
  - Weather is session-only and overrides the LIVE fog density/colour and clouds while enabled (it applies the sky via `ApplyAtmosphereSettingsNoSunCommand`, so it never creates/deletes the auto-Sun); turning it off restores the authored fog/clouds. `fog_get` reports authored vs effective.
  - Light shafts: `volumetric_inject.glsl` shadows the sun's in-scatter with ONE hardware-PCF tap into the sun's VSM clipmap per froxel (`sampleDirectionalShadowVolumetric`; finest→coarsest level, invalid page → next level, fades out at the coarsest edge ≈256 m). Not with RT sun shadows (`rtShadowActive` leaves the clipmap un-rendered → fog unshadowed). The inject pass needs the shadow sets 4/5 (fog won't init / dispatch without them); pool + ShadowData barriers include `eComputeShader`.
  - Undo: one batch per call (`mcp/undo/EnvironmentUndo`: `RenderSettingsSectionUndo`, `IblEnvironmentUndo`, `WeatherUndo`, plus the entity undos for the Sun); a mid-way failure calls `UndoRecorder::rollback()`. `gi_debug` and `ibl_bake` are not undoable. Edit tools refuse in Play mode and while `IsSceneLoadPendingQuery` is true.
- **Render-settings tools** (VK-1655, `mcp/tools/RenderSettingsTools.cpp` + `RenderSettingsToolSupport`): `render_settings_get/set` (master `postProcess.enabled`, toneMapping, autoExposure, bloom, colorGrading incl. `.cube` LUT paths, ssao, vignette, chromaticAberration, filmGrain, depthOfField, edgeDetection, upscale = DLSS/DLAA; `shadows` quality + filtering; `display.msaa`), `postprocess_effect_list/set` (plugin effects, VK-1409).
  - Scene-level only: all of it lives in the scene's `types::RenderSettings` (`.vfSettings`); no project/editor render store exists. The volumetric fog stays with `fog_set`; `underwater`/`rainDroplets` (runtime-driven), `ssr` (never saved), `frameGen`/`reflex` are refused with a reason. There is no FXAA: anti-aliasing = MSAA or DLAA.
  - Sections: each serializer sub-struct is one `SectionValue` (projections `PostProcessMasterSettings`, `ShadowQualitySettings`, `DisplayAntiAliasingSettings` in `RenderSettingsSections.hpp` copy only the fields the tools own), so one call = ONE `writeSections` (one `SetRenderSettingsCommand`, at most one `ApplyPostProcessSettingsCommand` that keeps the live fog/runtime effects, one `ApplyShadowSettingsCommand`, one `ApplyDisplaySettingsNotification`) and ONE `RenderSettingsSectionUndo` holding only the changed sections. Ranges = the loader clamps (`SceneSerializePostProcess.cpp`; shadows: the Render window sliders) — `tests/test_mcp_render_settings_ranges.cpp` round-trips them.
  - Plugin effects: `services/events/editor/PluginPostProcessEffectEvents.hpp`, handled by `editor/handlers/PluginPostProcessEffectHandler` through `PluginContextImpl::setPostProcessEffectEnabled` (keeps the plugin's request and per-scene activation), then PluginEventBus `"vf.postprocess.effectEnabled"` `{id, plugin, name, enabled}` so the plugin's UI follows (PostProcessFX subscribes). Session-only, undoable (`mcp/undo/PluginEffectUndo`).
- **Animation tools** (VK-1656, `mcp/tools/AnimationTools.cpp`, `AnimationIkTools.cpp`, `AnimationSocketTools.cpp`, `AnimatorBinding.cpp`; helpers `AnimationToolSupport`, `AnimationRigSupport`): `animation_list_clips/get_state_machine/set_state_machine/preview/get_runtime_state`, `animation_ik_get/set/remove/set_target`, `animation_list_sockets/attach_to_socket/detach_from_socket/set_socket_active`, and the `Animator` type of `component_*`.
  - **Animator binding** (`makeAnimatorBinding()`): present when the entity's `MeshData.animatorRef` is valid. Per-entity writable fields `animatorRef`, `retargetRef`, `applyRootMotion`; everything else it reports (skeleton, states, parameters, layers, `defaultState`, ...) is read-only and edited with `animation_set_state_machine`. Creation seeds `defaultClip` (required), `defaultState`, `playbackSpeed`, `loop` count only when `component_add` creates a new `.vfAnimator` (no `animatorRef` → `<mesh dir>/<entity name>.vfAnimator`, sandboxed) and are an `ArgError` on an existing one. `add` needs a skinned Mesh; `remove` clears all three fields, while undoing an add restores their previous values (`ComponentBinding::hostType/hostFields` → a partial `BuiltinComponentPatchUndo` of the Mesh, so a `retargetRef` set before survives).
  - Asset access goes through `events::animationAuthoring` (`services/events/animation/AnimationAuthoringEvents.hpp`, data `services/data/AnimationAuthoringData.hpp`), handled by `core::AnimationAuthoringAdapter` (Editor only, main thread): `.vfAnimator` load/save straight from disk with a content hash, a mesh's skeleton/sockets/IK chains read fresh from the `.vfMesh`, clip summaries, retarget bindings, the edit-mode preview. `SaveAnimatorAssetCommand` refuses a stale `expectedContentHash` (or an existing file with `createNew`), then publishes `AssetSavedNotification`: the asset database registers the GUID, live animators reload, and an open Animator Editor reloads (a dirty one shows a "changed on disk" banner). `animator::validateAnimator` (`utilities/animator/AnimatorValidation.hpp`, also the editor's Validate): errors block the save, warnings are returned.
  - **Hot reload**: Animation.dll links Services/Utilities statically, so its `EventDispatcher`, `ResourceManager` caches and `engineTime::Timer` are per-binary copies: never subscribe/publish in Animation.dll expecting the EXE's dispatcher, never read its Timer (it never ticks) or expect the EXE's resource caches there. EXE-side forwarders in `graphics/controllers/AnimatorSystemController` (`AssetSavedNotification` of a `.vfAnimator`, socket/IK-chain saves, `MeshDataChangedNotification`, `EditorModeChangedNotification`) only call the thread-safe `RuntimeAnimatorSystem` enqueue API (`requestAnimatorReload/requestMeshReload/requestModeTransition/markEditPreviewDirty`), applied by `syncWithRegistry()` on the main thread (dependent animators are destroyed before cache entries are erased, then re-initialised from disk with default parameters). Mode changes keep animators; `updateEditModePreview(deltaTime)` takes the caller's delta.
  - **Undo**: `animation_set_state_machine` on an existing file pushes ONE batch: `mcp/undo/AnimatorAssetUndo` (before/after data + content hash; replays `SaveAnimatorAssetCommand` with `expectedContentHash`, so an edit made elsewhere since makes it refuse) first, assignment entries after. Creating a `.vfAnimator` is not undoable. IK edits: one `IkStateUndo` per call (replayed through `SetIKChainsCommand`); sockets: `SocketAttachmentUndo` / `SocketActiveUndo` (prior attachment + local transform). `animation_preview` and `animation_ik_set_target` are not undoable (targets are never saved; its optional `weight` writes the chain's saved weight).
  - **Preview**: `animation_preview` plays a base-layer state or any compatible clip on a separate one-state stack whose pose is copied into the entity's animator; edit mode only (refused when layer 0 is a DirectClip layer, which never plays), it ends on stop, mode change, animator reload/destroy or mesh change. The agent checks the pose with `viewport_screenshot`.
  - `animation_set_state_machine`, `animation_preview`, `animation_ik_set/remove` and creating a `.vfAnimator` refuse in Play mode and while a scene load is pending (`env::editBlocker`); the socket tools (Stop restores the snapshot), `animation_ik_set_target` and `animation_get_runtime_state` work in Play.
  - Mesh imports report every take as an `Animation` result (`ImportFileResult::assetType`, `<file>_<take>.vfAnim`), so clips get a `.vfmeta`/GUID and show in `assets_list` type `Animation`. Removing an animator puts the mesh back in its bind pose (`BoneMatrixManager::resetToBindPose`).

### Services Layer

The `services/` module provides decoupled communication via CQRS pattern:

```cpp
// Commands - execute actions, may return results
events::EventDispatcher::instance().execute(SomeCommand{args});

// Queries - read-only data retrieval
auto result = events::EventDispatcher::instance().query(SomeQuery{});

// Notifications - fire-and-forget broadcasts (pub/sub)
events::EventDispatcher::instance().publish(SomeNotification{data});

// Subscribe to notifications
auto token = dispatcher.subscribe<SomeNotification>([](const auto& n) { ... });
dispatcher.unsubscribe(token);
```

Event types are defined in `services/events/` (e.g., `ResourceEvents.hpp`, `SceneEvents.hpp`, `PreviewEvents.hpp`).

### Provider/Adapter Pattern

Services define **provider interfaces** that abstract engine capabilities:

```cpp
// In services/providers/IPreviewProvider.hpp
class IPreviewProvider {
    virtual void initMaterialPreview(void* instanceId) = 0;
    virtual void* renderMaterialPreview(void* instanceId) = 0;
    // ...
};
```

Core implements these via **adapters** that wrap internal controllers:

```cpp
// In core/adapters/PreviewAdapter.hpp
class PreviewAdapter : public services::IPreviewProvider {
    std::unordered_map<void*, std::unique_ptr<MaterialPreviewController>> materialControllers;
    // Implements interface by delegating to controllers
};
```

**Multi-instance support**: Preview services use `void* instanceId` (typically `this` pointer of window) to manage independent controller instances per editor window.

### Bootstrap Pattern

Editor and Runtime use bootstrap classes to initialize the engine:

```cpp
// Editor initialization flow
EditorHandler::init() {
    bootstrap = std::make_unique<EditorBootstrap>();
    bootstrap->init();  // Creates Core, Graphics, Adapters

    // Get providers from bootstrap, pass to services
    auto previewService = std::make_shared<PreviewServiceImpl>(bootstrap->getPreviewProvider());
    previewService->registerEventHandlers();
}
```

This encapsulates all Core/Graphics dependencies within bootstrap, keeping Editor/Runtime clean.

### Import Pipeline

Asset imports flow through a staged pipeline (`import/pipeline/`):

```
File → HeaderValidationStage → FileTypeDetectionStage → FileProcessingStage → .vf* output
```

Each asset type (Mesh, Texture, Audio) has a processor in `import/types/` with progress callback support:
```cpp
using MeshProgressCallback = std::function<void(float progress)>;
meshProcessor.loadFromFile(file, fileName, location, progressCallback);
```

## Naming Conventions

- **Classes/Files**: PascalCase, matching names (`CoreInterface.hpp` contains `class CoreInterface`)
- **Variables**: camelCase
- **Namespaces**: lowercase (`core::`, `controllers::`, `components::`, `scene::`, `resource::`, `services::`)
- **Suffixes**:
  - `*Controller` - Internal facade within module
  - `*Handler` - Orchestrator coordinating subsystems
  - `*Adapter` - Implements provider interface, wraps controllers
  - `*Provider` - Interface prefix (`I*Provider`)
  - `*Service` - Interface prefix (`I*Service`)
  - `*ServiceImpl` - Service implementation
  - `*Bootstrap` - Initialization orchestrator
  - `*Manager`, `*System`, `*Component`, `*Generator` - Other common patterns

## Custom Asset Formats

`.vfImage`, `.vfHdr`, `.vfMesh`, `.vfAudio`, `.vfAnim`, `.vfImposter`, `.vfWorld` - Binary formats for engine-processed assets.

## Adding New Features

### New ECS Component
1. Add struct to `utilities/components/Components.hpp`
2. Use via `Entity::addComponent<T>()` / `Entity::getComponent<T>()`

### New Asset Type
1. Create loader in `import/types/`
2. Add pipeline stage in `import/pipeline/stages/`
3. Define resource type in `utilities/resource/Types.hpp`

### New Editor Window
1. Create class in `editor/windows/`
2. Register in `WindowImguiHandler`
3. Use `EventDispatcher` to access engine functionality (never include Core/Graphics directly)

### New Editor Tool (DLL-backed, e.g., ProceduralGen, ImageProcessing)
1. Create DLL subsystem under `utilities/<toolname>/` with export header (`VF_<NAME>_BUILD_DLL` / `VF_<NAME>_API`)
2. Add SharedLib project to `premake5.lua` in "Subsystems" group, add `removefiles` to Utilities, link from Editor
3. Postbuild: copy DLL to `bin/Editor/{Debug,Release}/x64/`
4. Create editor window in `editor/windows/<toolname>/` — member of `MainImguiWindow`, toggled via `show()` from `MainMenuBar`
5. Add to `handleToolsMenu()` in `MainMenuBar.cpp`
6. Wire into `MainImguiWindow.hpp/cpp`: include, member, setter, `draw()` call
7. For async work: use `std::async` + `std::future`, poll in `draw()`. Wait for futures in destructor to avoid dangling pointers
8. For GPU preview: use `LoadEditorTextureFromDataCommand` / `ReleaseEditorTextureCommand`. Update preview BEFORE `ImGui::Image()` (not after) to avoid invalid descriptor sets
9. Export via `nfd::FileDialog::saveFileDialog()` + `.vfmeta` sidecar via `AssetMetadataSerializer`

### New Service
1. Create interface in `services/interfaces/I<Name>Service.hpp`
2. Create provider interface in `services/providers/I<Name>Provider.hpp`
3. Create implementation in `services/impl/<Name>ServiceImpl.hpp/cpp`
4. Create events in `services/events/<Name>Events.hpp`
5. Create adapter in `core/adapters/<Name>Adapter.hpp/cpp`
6. Wire up in bootstrap classes (`EditorBootstrap`, `RuntimeBootstrap`)

### New External Plugin
1. Create `plugins/<PluginName>/<PluginName>.cpp` implementing `IPlugin` (see `VFEngine/plugin/api/IPlugin.hpp`); export via `VF_PLUGIN_EXPORT` macros from `PluginExport.hpp`
2. Author `plugins/<PluginName>/<PluginName>.vfplugin` (JSON descriptor: name, version, entry, dependencies — parsed by `PluginDescriptor`)
3. Add `plugins/<PluginName>/premake5.lua` containing just `vfPluginProject("<PluginName>")` (+ `links { "imgui" }` if it has editor UI) — all common setup lives in `plugins/plugin_sdk.lua`. The root `premake5.lua` auto-discovers it — just re-run `premake5 vs2022`
4. `PluginManager` discovers and loads it from `plugins/` at editor startup; subscribe to engine events via `PluginEventBus` / `PluginContext`
5. Custom shaders/geometry: `ctx->createCustomPipeline` / `uploadCustomMesh` / `drawCustomMesh` (graphics capability). Editor UI: implement `ImguiWindow`, `links { "imgui" }`, `ImGui::SetCurrentContext(ctx->getImGuiContext())`, then `ctx->registerEditorWindow(window, "Title")` — titled windows appear in the editor's Plugins menu
6. AI agent tools (editor capability): `ctx->registerMcpTool({name, title, description, inputSchema, readOnly, destructive, timeoutMs, handler})` — see `plugins/PluginAPITest` (`pluginapitest_echo`). The `.vfplugin` `apiVersion` must equal `VF_PLUGIN_API_VERSION` exactly

### New Unit Test
1. Drop `test_<feature>.cpp` into `VFEngine/tests/` — no premake edit needed (glob)
2. Use `TEST_CASE("...")` / `SUBCASE("...")`; CPU-only (no Vulkan device, no GLFW window)
3. Re-run with `Tests.exe --test-case="<feature>*"` while iterating

### Exposing Graphics Functionality to Editor
1. Add method to appropriate provider interface (e.g., `IPreviewProvider`)
2. Implement in corresponding adapter (e.g., `PreviewAdapter`)
3. Add service method in interface and implementation
4. Create Command/Query event type
5. Register handler in `registerEventHandlers()`
6. Use via `EventDispatcher` in Editor code

## Third-Party Libraries

| Library | Purpose |
|---------|---------|
| Vulkan + shaderc | Graphics rendering, runtime shader compilation |
| GLFW | Window/input |
| ImGui + ImGuizmo + imgui-node-editor | Editor UI |
| IconFontCppHeaders | Icon font rendering for UI |
| EnTT | Entity Component System |
| Assimp | 3D model importing (requires CMake build) |
| OpenAL + libogg + libvorbis | Audio playback and Ogg Vorbis codec (requires CMake build) |
| JoltPhysics | Physics simulation (SharedLib) |
| v-hacd | Convex mesh decomposition for physics colliders |
| recastnavigation | Navigation mesh generation and AI pathfinding |
| mType + asmjit | Scripting language interpreter with JIT compilation |
| enkiTS | Parallel task scheduling |
| meshoptimizer | Mesh/meshlet optimization and LOD generation (SharedLib) |
| ispc_texcomp + bcdec | BC7/BC6H texture compression / decompression |
| freetype | Font rasterization (requires CMake build) |
| msdfgen | Core-only MTSDF glyph generation for imported font atlases |
| NVIDIA Streamline | DLSS / DLSS-G frame generation / Reflex (`VF_STREAMLINE_ENABLED`, Graphics module; dev DLLs copied for Debug/Development, production DLLs for Release) |
| LZ4 | Compression for `.vfpak` game archives |
| nlohmann/json | JSON serialization for scenes/assets/config |
| spdlog | Logging |
| GLM | Mathematics |
| stb, tinyexr, dr_libs | Image/audio file loading |

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
