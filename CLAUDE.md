# VertexForge Engine

A C++17 game engine using Vulkan, OpenAL, JoltPhysics, ImGui, EnTT, Assimp, and GLFW.

## Build System

- **Tool**: Premake5 generates Visual Studio 2022 solutions
- **Standard**: C++20
- **Platform**: Windows x64 (Debug/Release)
- **Build command**: `premake5 vs2022` then build in VS

## Project Structure

```
VFEngine/
├── core/         # Main loop, handlers, controllers
├── graphics/     # Vulkan rendering, IBL, shaders
├── window/       # GLFW window/input management
├── utilities/    # ECS components, scene, resources, serialization
├── import/       # Asset import pipeline (mesh, texture, audio)
├── editor/       # ImGui-based editor application
└── runtime/      # Standalone game runtime
```

## Module Dependencies

```
Editor → Core → Graphics → Window → Utilities
Runtime → Core
Import → Utilities
```

## Key Namespaces

- `core::` - Engine core
- `controllers::` - Facade interfaces
- `handlers::` - Orchestrators
- `components::` - ECS components
- `scene::` - Entity/level systems
- `resource::` - Resource management
- `render::` / `ibl::` - Rendering
- `pipeline::` / `types::` - Import system
- `windows::` - Editor UI panels

## Naming Conventions

- **Classes**: PascalCase (`CoreInterface`, `EntityRegistry`)
- **Files**: Match class names (`CoreInterface.hpp/.cpp`)
- **Variables**: camelCase
- **Suffixes**: `*Controller`, `*Handler`, `*Manager`, `*System`, `*Component`, `*Generator`

## Third-Party Libraries

| Library | Purpose |
|---------|---------|
| Vulkan | Graphics rendering |
| GLFW | Window/input management |
| ImGui | Editor UI (+ ImGuizmo, imgui-node-editor) |
| EnTT | Entity Component System |
| Assimp | 3D model importing |
| OpenAL | Audio playback |
| JoltPhysics | Physics simulation |
| spdlog | Logging |
| GLM | Mathematics |
| stb | Image loading |
| tinyexr | HDR image loading |
| dr_libs | Audio file loading (MP3, WAV, FLAC, Opus) |

## Custom File Formats

- `.vfImage` - Textures
- `.vfHdr` - HDR images
- `.vfMesh` - 3D meshes
- `.vfAudio` - Audio files
- `.vfAnim` - Animations

## Architecture Patterns

- **ECS**: EnTT-based entity component system
- **Facade**: Controllers provide simplified interfaces
- **Pipeline**: Stage-based asset import
- **Async Loading**: Resources loaded via `std::async`
- **RAII**: Smart pointers throughout

## Key Files

| File | Purpose |
|------|---------|
| `core/controllers/CoreInterface.hpp` | Main engine entry point |
| `core/core/MainLoop.hpp` | Engine loop orchestration |
| `graphics/core/VulkanContext.hpp` | Vulkan initialization |
| `graphics/render/IBL.hpp` | Image-based lighting |
| `utilities/components/Components.hpp` | ECS component definitions |
| `utilities/scene/Entity.hpp` | Entity wrapper |
| `utilities/scene/Level.hpp` | Scene container |
| `utilities/resource/ResourceManager.hpp` | Async resource loading |
| `import/controllers/Import.hpp` | Asset import interface |

## ECS Components

- `TransformComponent` - Position, rotation, scale
- `CameraComponent` - Perspective/orthographic cameras
- `NameComponent` - Entity naming
- `ParentComponent` / `ChildrenComponent` - Hierarchy
- `IBLComponent` - IBL texture references
- `WorldTransformComponent` - Computed world matrices

## Shaders

Located in `resources/shaders/`:
- IBL shaders: `brdf.glsl`, `equirectangular_convolution.glsl`, `prefilter.glsl`, `skybox.glsl`

## Entry Points

- **Editor**: `VFEngine/editor/run/Main.cpp`
- **Runtime**: `VFEngine/runtime/run/Main.cpp`

## Git Workflow

- **Main branch**: `dev`
- **Current branch**: `fix-ibl`

## Common Tasks

### Adding a new component
1. Define in `utilities/components/Components.hpp`
2. Register with EnTT via `Entity::addComponent<T>()`

### Adding a new asset type
1. Create loader in `import/types/`
2. Add stage handling in `import/pipeline/stages/`
3. Define resource type in `utilities/resource/Types.hpp`

### Adding editor window
1. Create class in `editor/windows/`
2. Inherit ImGui window pattern
3. Register in `WindowImguiHandler`
