# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Generate Visual Studio 2022 solution
premake5 vs2022

# Build from command line (after generating solution)
msbuild VFEngine/VertexForge.sln /p:Configuration=Debug /p:Platform=x64
msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64

# Build a single project (faster iteration)
msbuild VFEngine/VertexForge.sln /t:Editor /p:Configuration=Debug /p:Platform=x64
msbuild VFEngine/VertexForge.sln /t:Tests  /p:Configuration=Debug /p:Platform=x64
```

**Prerequisites**: Vulkan SDK installed with `VULKAN_SDK` environment variable set.

**Toolchain**: C++20, MSVC toolset `v145` (VS2026), latest Windows SDK. Global flags `/utf-8 /MP` (required by spdlog/fmt). Engine-wide defines: `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1`, `GLM_FORCE_DEPTH_ZERO_TO_ONE` (Vulkan [0,1] depth range).

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
Editor   ──> Services, Import, Core, Plugin, ProceduralGen, ImageProcessing, GameExport, ECSRegistry (ONLY)
Import   ──> Utilities (SharedLib/DLL, requires CMake-built libs: assimp, freetype, libogg, libvorbis)
Runtime  ──> Services, Core (ONLY)
Services ──> Utilities, Terrain, Serialization, World, Window
Core     ──> Graphics, Audio, Physics, Animation, mType, jolt, recast
Graphics ──> Window, VFX, imgui
Plugin   ──> Services, Utilities (engine-side plugin SDK + loader)
```

**Premake groups**: `Engine` (Editor, Runtime, Core, Graphics, Window, Import, Services, Plugin, Utilities), `Subsystems` (extracted modules — see table below), `libs` (vendored third-party), `Plugins` (sample/external plugin DLLs under `plugins/`).

Module kinds: Editor/Runtime/Tests are `ConsoleApp`. Most modules are `StaticLib`. `SharedLib/DLL`: Import, ProceduralGen, ImageProcessing, Audio, Terrain, ProceduralGen, ImageProcessing, GameExport, ECSRegistry, jolt, meshoptimizer, and external plugins.

### Subsystem Extraction

Large modules are split into separately compiled subsystem projects using `removefiles` in the parent and dedicated projects for each subsystem. This reduces compile times and clarifies boundaries.

| Subsystem | Extracted From | Compiles | Kind |
|-----------|---------------|----------|------|
| Audio | Core | `core/audio/**` | SharedLib (`VF_AUDIO_BUILD_DLL`) |
| Physics | Core | `core/physics/**` | StaticLib |
| Animation | Graphics + Utilities | `graphics/animation/**` + `utilities/animator/**` | StaticLib |
| VFX | Graphics + Utilities | `graphics/render/vfx/**` + `utilities/vfx/**` | StaticLib |
| Terrain | Utilities | `utilities/terrain/**` | SharedLib |
| Serialization | Utilities | `utilities/serialization/**` + `utilities/world/World*Serialization.*` | StaticLib |
| World | Utilities | `utilities/world/**` (excluding serialization files) | StaticLib |
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
└── tests/                   # doctest-based unit tests (Tests.exe)
    ├── main.cpp             # doctest entry point — do not duplicate
    └── test_*.cpp           # one TU per feature area; auto-globbed by premake
```

External plugins live outside `VFEngine/` under `plugins/<PluginName>/` (e.g. `plugins/PluginAPITest/`, `plugins/HexTerrain/`). Each plugin folder is self-contained:
- carries its own `premake5.lua` (SharedLib project, paths relative to the plugin folder) — auto-discovered by the root `premake5.lua` under `group "Plugins"`, no root edits needed
- includes `VFEngine/plugin` for the SDK
- ships a `<PluginName>.vfplugin` descriptor (JSON) alongside the DLL
- copies its DLL back into `plugins/<PluginName>/` via postbuild, where `PluginManager` discovers it at runtime

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
3. Add `plugins/<PluginName>/premake5.lua` (copy from `plugins/HexTerrain/premake5.lua`: SharedLib, paths relative to the plugin folder, postbuild-copy the DLL back to `plugins/<PluginName>/`). The root `premake5.lua` auto-discovers it — just re-run `premake5 vs2022`
4. `PluginManager` discovers and loads it from `plugins/` at editor startup; subscribe to engine events via `PluginEventBus` / `PluginContext`
5. Custom shaders/geometry: `ctx->createCustomPipeline` / `uploadCustomMesh` / `drawCustomMesh` (graphics capability). Editor UI: implement `ImguiWindow`, `links { "imgui" }`, `ImGui::SetCurrentContext(ctx->getImGuiContext())`, then `ctx->registerEditorWindow(window, "Title")` — titled windows appear in the editor's Plugins menu

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
| ispc_texcomp | BC7/BC6H texture compression |
| freetype | Font rasterization (requires CMake build) |
| nlohmann/json | JSON serialization for scenes/assets/config |
| spdlog | Logging |
| GLM | Mathematics |
| stb, tinyexr, dr_libs | Image/audio file loading |
