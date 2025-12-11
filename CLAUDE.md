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
Editor ──┬──> Core ──> Graphics ──> Window ──> Utilities
         ├──> Import ──────────────────────────> Utilities
         └──> Services ────────────────────────> Utilities
Runtime ────> Core
```

All modules are static libraries except Editor and Runtime (ConsoleApp executables).

### Key Patterns

- **Controllers** (in `*/controllers/`): Public facade interfaces for each module. Other modules only include controller headers.
- **Handlers**: Internal orchestrators that coordinate subsystems within a module.
- **ECS**: EnTT-based. Entities wrap `entt::entity` handles; components defined in `utilities/components/Components.hpp`.
- **Async Resources**: `ResourceManager` loads assets via `std::async`, returns futures.
- **Event System**: `EventDispatcher` singleton for decoupled communication between modules (see Services Layer below).

### Entry Points

| Target | Entry | Description |
|--------|-------|-------------|
| Editor | `VFEngine/editor/run/Main.cpp` | ImGui-based editor |
| Runtime | `VFEngine/runtime/run/Main.cpp` | Standalone game runtime |

## Project Structure

```
VFEngine/
├── core/           # MainLoop, engine lifecycle, editor texture management
├── graphics/       # Vulkan context, rendering, IBL pipeline, shaders
├── window/         # GLFW window, input handling
├── utilities/      # ECS components, scene graph, resource loading, serialization
├── import/         # Asset import pipeline (mesh, texture, audio, animation)
├── services/       # Event dispatcher, service interfaces/implementations, DTOs
├── editor/         # ImGui panels, gizmos, node editor
└── runtime/        # Standalone runtime (minimal)
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

Event types are defined in `services/events/` (e.g., `ResourceEvents.hpp`, `SceneEvents.hpp`, `InputEvents.hpp`).

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
- **Namespaces**: lowercase (`core::`, `controllers::`, `components::`, `scene::`, `resource::`)
- **Suffixes**: `*Controller` (facade), `*Handler` (orchestrator), `*Manager`, `*System`, `*Component`, `*Generator`

## Custom Asset Formats

`.vfImage`, `.vfHdr`, `.vfMesh`, `.vfAudio`, `.vfAnim` - Binary formats for engine-processed assets.

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
