# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Generate Visual Studio 2022 solution
premake5 vs2022

# Build from command line (after generating solution)
msbuild VFEngine/VertexForge.sln /p:Configuration=Debug /p:Platform=x64
msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64
```

**Prerequisites**: Vulkan SDK installed with `VULKAN_SDK` environment variable set.

**Output**: Executables in `bin/Editor/<Config>/x64/` and `bin/Runtime/<Config>/x64/`

## Architecture

### Module Dependency Graph

```
Editor   ──> Services, Import (ONLY)
Import   ──> Utilities (ONLY)
Runtime  ──> Services (ONLY)
Services ──> Utilities (ONLY - uses provider interfaces implemented by Core)
Core     ──> Graphics, Window, Services (implements provider adapters)
Graphics ──> Window, Utilities
```

All modules are static libraries except Editor and Runtime (ConsoleApp executables).

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
│   └── run/                 # Main.cpp entry point
└── runtime/                 # Standalone game runtime
    ├── handlers/            # RuntimeHandler
    └── run/                 # Main.cpp entry point
```

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

### New Service
1. Create interface in `services/interfaces/I<Name>Service.hpp`
2. Create provider interface in `services/providers/I<Name>Provider.hpp`
3. Create implementation in `services/impl/<Name>ServiceImpl.hpp/cpp`
4. Create events in `services/events/<Name>Events.hpp`
5. Create adapter in `core/adapters/<Name>Adapter.hpp/cpp`
6. Wire up in bootstrap classes (`EditorBootstrap`, `RuntimeBootstrap`)

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
| EnTT | Entity Component System |
| Assimp | 3D model importing (requires CMake build) |
| OpenAL | Audio playback (requires CMake build) |
| JoltPhysics | Physics simulation |
| spdlog | Logging |
| GLM | Mathematics |
| stb, tinyexr, dr_libs | Image/audio file loading |
