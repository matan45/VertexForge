# VFEngine Architecture

## Layer Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                         │
│  ┌─────────────┐    ┌──────────┐       ┌─────────────┐     │
│  │   Editor    │───▶│  Import  │       │   Runtime   │     │
│  │  (ImGui UI) │    │ (Assets) │       │ (Standalone)│     │
│  └──────┬──────┘    └────┬─────┘       └──────┬──────┘     │
│         │                │                    │            │
│         └────────────────┼────────────────────┘            │
│                          ▼                                  │
├─────────────────────────────────────────────────────────────┤
│                    SERVICES LAYER                           │
│  ┌────────────────────────────────────────────────────┐    │
│  │  ISceneService  │ IRenderService │ IInputService   │    │
│  │  IResourceService │ EventDispatcher │ ServiceLocator│   │
│  └────────────────────────────────────────────────────┘    │
│                        │                                    │
│                        ▼                                    │
├─────────────────────────────────────────────────────────────┤
│                      CORE LAYER                             │
│        (Unified API - Exposes all engine systems)           │
│  ┌────────────────────────────────────────────────────┐    │
│  │  CoreInterface │ OffScreen │ EditorTextureController│   │
│  │  MainLoop │ ImguiWindowHandler                      │    │
│  └────────────────────────────────────────────────────┘    │
│                        │                                    │
│         ┌──────────────┼──────────────┐                    │
│         ▼              ▼              ▼                    │
├─────────────────────────────────────────────────────────────┤
│                    ENGINE LAYER                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ Graphics │  │  Window  │  │  Audio   │  │ Physics  │   │
│  │ (Vulkan) │  │  (GLFW)  │  │ (OpenAL) │  │  (Jolt)  │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
│  ┌──────────┐  ┌──────────┐                               │
│  │  Import  │  │ Scripts  │                               │
│  │ (Assets) │  │          │                               │
│  └──────────┘  └──────────┘                               │
├─────────────────────────────────────────────────────────────┤
│                  UTILITIES LAYER                            │
│         (Can be included by ANY layer above)                │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Components │ EntityRegistry │ SceneGraph │ Math   │    │
│  │  Logging │ Timer │ Serialization │ ResourceManager │    │
│  └────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

## Dependency Rules

### Allowed Dependencies (Top to Bottom)
- **Editor** → Services, Import, Core, Utilities
- **Runtime** → Core, Utilities
- **Services** → Core, Utilities
- **Core** → Graphics, Window, Audio, Physics, Utilities
- **Import** → Utilities
- **Graphics** → Window, Utilities
- **Window** → Utilities
- **All Layers** → Utilities

### Forbidden Dependencies
- Lower layers CANNOT depend on higher layers
- Services CANNOT directly access Graphics/Window (must go through Core)
- Editor windows CANNOT include Graphics headers (use Services interfaces)

## Layer Responsibilities

### Application Layer
| Project | Responsibility |
|---------|---------------|
| **Editor** | ImGui-based editor UI, windows, gizmos |
| **Runtime** | Standalone game execution |

### Services Layer
| Component | Responsibility |
|-----------|---------------|
| **ISceneService** | Entity/component operations, scene graph |
| **IRenderService** | Viewport rendering, IBL, textures |
| **IInputService** | Mouse/keyboard input queries |
| **IResourceService** | Asset import, resource management |
| **EventDispatcher** | Pub/sub event system |
| **ServiceLocator** | Dependency injection container |

### Core Layer
| Component | Responsibility |
|-----------|---------------|
| **CoreInterface** | Main entry point, lifecycle management |
| **MainLoop** | Frame loop, input polling, update cycle |
| **OffScreen** | Off-screen rendering API for editor |
| **EditorTextureController** | Texture loading for editor UI |
| **ImguiWindowHandler** | ImGui window registration/drawing |

### Engine Layer
| Project | Responsibility |
|---------|---------------|
| **Graphics** | Vulkan context, rendering, shaders, IBL |
| **Window** | GLFW window, input handling |
| **Audio** | OpenAL audio playback (planned) |
| **Physics** | Jolt physics simulation (planned) |

### Import Layer
| Project | Responsibility |
|---------|---------------|
| **Import** | Asset pipeline (mesh, texture, audio, animation) - used directly by Editor |

### Utilities Layer
| Component | Responsibility |
|-----------|---------------|
| **Components** | ECS component definitions |
| **EntityRegistry** | EnTT wrapper |
| **SceneGraphSystem** | Hierarchical scene management |
| **ResourceManager** | Async resource loading |
| **Logging** | spdlog wrapper |
| **Math** | GLM utilities |

## Data Flow Examples

### Rendering a Frame
```
Editor (ViewPort window)
    │
    ▼ calls IRenderService.getViewportTexture()
Services (RenderServiceImpl)
    │
    ▼ calls offScreen->render()
Core (OffScreen)
    │
    ▼ calls offScreenController->render()
Graphics (OffScreenController)
    │
    ▼ Vulkan draw commands
```

### Creating an Entity
```
Editor (SceneGraph window)
    │
    ▼ calls ISceneService.createEntity()
Services (SceneServiceImpl)
    │
    ▼ calls sceneGraphSystem->createEntity()
Utilities (SceneGraphSystem)
    │
    ▼ EntityRegistry::create()
```

### Importing an Asset
```
Editor (ContentBrowser)
    │
    ▼ calls IResourceService.importFiles()
Services (ResourceServiceImpl)
    │
    ▼ ImportDelegate callback
Import (Import controller)
    │
    ▼ Asset pipeline processing
Utilities (ResourceManager)
```

## Project Build Order

```
1. Utilities (no dependencies)
2. Window → depends on Utilities
3. Graphics → depends on Window, Utilities
4. Import → depends on Utilities
5. Core → depends on Graphics, Window, Utilities
6. Services → depends on Core, Utilities
7. Editor → depends on Services, Import, Core
8. Runtime → depends on Core
```

## Key Design Patterns

### Service Locator
```cpp
// Registration (in EditorHandler)
ServiceLocator::instance().registerService<ISceneService>(sceneService);

// Resolution (in UI windows)
auto& sceneService = ServiceLocator::instance().get<ISceneService>();
```

### Event-Driven Communication
```cpp
// Publishing
EventDispatcher::instance().publish(EntityCreatedNotification{entityId});

// Subscribing
EventDispatcher::instance().subscribe<EntityCreatedNotification>(
    [](const auto& event) { /* handle */ }
);
```

### Opaque Handles
```cpp
// EntityHandle hides entt::entity from presentation layer
struct EntityHandle {
    uint64_t id;
    bool isValid() const;
};
```
