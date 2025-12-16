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
│  │  IPreviewService │ EventDispatcher │ Providers     │    │
│  └────────────────────────────────────────────────────┘    │
│                        │                                    │
│                        ▼                                    │
├─────────────────────────────────────────────────────────────┤
│                      CORE LAYER                             │
│        (Unified API - Exposes all engine systems)           │
│  ┌────────────────────────────────────────────────────┐    │
│  │  CoreInterface │ MainLoop │ Bootstrap │ Adapters   │    │
│  │  OffScreen │ ImguiWindowHandler                    │    │
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
- **Editor** → Services, Import (direct), Utilities
- **Runtime** → Services, Utilities
- **Services** → Utilities (uses provider interfaces implemented by Core)
- **Core** → Graphics, Window, Services (implements provider adapters)
- **Import** → Utilities
- **Graphics** → Window, Utilities
- **Window** → Utilities
- **All Layers** → Utilities

### Forbidden Dependencies
- Lower layers CANNOT depend on higher layers
- Services CANNOT directly access Core/Graphics/Window (uses provider interfaces)
- Editor windows CANNOT include Core/Graphics headers (use Services interfaces)
- Editor calls Import directly (no service wrapper needed for import operations)

## Layer Responsibilities

### Application Layer
| Project | Responsibility |
|---------|---------------|
| **Editor** | ImGui-based editor UI, windows, gizmos, event subscriptions |
| **Runtime** | Standalone game execution |

### Services Layer
| Component | Responsibility |
|-----------|---------------|
| **ISceneService** | Entity/component operations, scene graph |
| **IRenderService** | Viewport rendering, IBL, textures |
| **IInputService** | Mouse/keyboard input, window events |
| **IPreviewService** | Material and mesh preview rendering |
| **EventDispatcher** | Pub/sub event system (CQRS pattern) |
| **Providers** | Interfaces for Core to implement (IOffScreenProvider, etc.) |

### Core Layer
| Component | Responsibility |
|-----------|---------------|
| **CoreInterface** | Main entry point, lifecycle management |
| **MainLoop** | Frame loop, frame callbacks, update cycle |
| **EditorBootstrap** | Editor initialization, creates adapters |
| **RuntimeBootstrap** | Runtime initialization, creates adapters |
| **Adapters** | Implement provider interfaces (OffScreenAdapter, etc.) |
| **OffScreen** | Off-screen rendering API for editor |

### Engine Layer
| Project | Responsibility |
|---------|---------------|
| **Graphics** | Vulkan context, rendering, shaders, IBL |
| **Window** | GLFW window, input handling, window state callbacks |
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

## Key Design Patterns

### Provider/Adapter Pattern

Services defines interfaces (providers) that Core implements (adapters):

```
┌─────────────────┐         ┌─────────────────┐
│    Services     │         │      Core       │
├─────────────────┤         ├─────────────────┤
│ IOffScreenProvider ◄──────│ OffScreenAdapter│
│ IEditorTextureProvider ◄──│ EditorTextureAdapter│
│ IPreviewProvider ◄────────│ PreviewAdapter  │
└─────────────────┘         └─────────────────┘
```

```cpp
// In Services (interface)
class IOffScreenProvider {
public:
    virtual void* render() = 0;
    virtual void iblSet(std::string_view path) = 0;
};

// In Core (implementation)
class OffScreenAdapter : public IOffScreenProvider {
    OffScreen* offScreen;
public:
    void* render() override { return offScreen->render(); }
    void iblSet(std::string_view path) override { offScreen->iblSet(path); }
};
```

### Bootstrap Pattern

Bootstrap classes encapsulate initialization and provide adapters:

```cpp
// EditorHandler uses EditorBootstrap
class EditorHandler {
    std::unique_ptr<core::EditorBootstrap> bootstrap;

    void init() {
        bootstrap->init();

        // Get providers from bootstrap
        auto renderService = std::make_shared<RenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            bootstrap->getEditorTextureProvider()
        );
    }
};
```

### Frame Callback Pattern

Core provides frame callbacks for Services to update each frame:

```cpp
// EditorHandler sets callback
bootstrap->setFrameCallback([this]() {
    inputService->update();  // Publishes window events
});

// MainLoop calls it each frame
void MainLoop::run() {
    while (!shouldClose()) {
        mainWindow->pollEvents();
        if (frameCallback) {
            frameCallback();  // Services update here
        }
        // ... render
    }
}
```

### Event-Driven Communication (CQRS)

```cpp
// Commands - execute actions
events::application::CloseCommand cmd;
dispatcher.execute(cmd);

// Queries - read data
events::scene::GetEntityDataQuery query;
auto result = dispatcher.query(query);

// Notifications - broadcast state changes
events::application::WindowResizedNotification notif{width, height};
dispatcher.publish(notif);

// Subscriptions
auto token = dispatcher.subscribe<WindowResizedNotification>(
    [](const auto& event) { /* handle */ }
);
```

## Data Flow Examples

### Window Events Flow

```
┌─────────────────────────────────────────────────────────────┐
│ 1. GLFW CALLBACKS (Window module)                           │
│    Window.cpp registers callbacks:                          │
│    - glfwSetFramebufferSizeCallback → sets isResized flag  │
│    - glfwSetWindowIconifyCallback  → sets isMinimized flag │
│    - glfwSetWindowFocusCallback    → sets isFocused flag   │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. MAIN LOOP (Core module)                                  │
│    MainLoop::run() each frame:                              │
│      mainWindow->pollEvents();     // GLFW callbacks fire   │
│      if (frameCallback) {                                   │
│          frameCallback();          // Services update       │
│      }                                                      │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. INPUT SERVICE (Services module)                          │
│    InputServiceImpl::update():                              │
│      - Checks window state flags via InputController        │
│      - Publishes notifications via EventDispatcher          │
│      - Resets flags                                         │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. EVENT DISPATCHER broadcasts to subscribers:              │
│    - WindowResizedNotification    { width, height }        │
│    - WindowMinimizedNotification                            │
│    - WindowRestoredNotification                             │
│    - WindowFocusedNotification    { focused }              │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. EDITOR/RUNTIME HANDLERS subscribe and react:             │
│    dispatcher.subscribe<WindowResizedNotification>(         │
│        [this](const auto&) {                                │
│            bootstrap->triggerResize();                      │
│        });                                                  │
└─────────────────────────────────────────────────────────────┘
```

### Rendering a Frame

```
Editor (ViewPort window)
    │
    ▼ calls IRenderService.getViewportTexture()
Services (RenderServiceImpl)
    │
    ▼ calls offScreenProvider->render()
Core (OffScreenAdapter)
    │
    ▼ calls offScreen->render()
Graphics (OffScreenController)
    │
    ▼ Vulkan draw commands
```

### Creating an Entity

```
Editor (SceneGraph window)
    │
    ▼ dispatcher.execute(CreateEntityCommand{})
Services (SceneServiceImpl)
    │
    ▼ calls sceneGraphSystem->createEntity()
Utilities (SceneGraphSystem)
    │
    ▼ EntityRegistry::create()
```

### Importing an Asset

```
Editor (MainImguiWindow)
    │
    ▼ calls Import::importFiles() directly
Import (Import controller)
    │
    ├─▶ publishes ImportProgressNotification
    │
    ▼ Asset pipeline processing
Utilities (ResourceManager)
```

## Project Structure

```
VFEngine/
├── core/
│   ├── adapters/           # Provider implementations
│   │   ├── OffScreenAdapter.hpp/cpp
│   │   ├── EditorTextureAdapter.hpp/cpp
│   │   └── PreviewAdapter.hpp/cpp
│   ├── bootstrap/          # Initialization
│   │   ├── EditorBootstrap.hpp/cpp
│   │   └── RuntimeBootstrap.hpp/cpp
│   ├── controllers/        # Public API
│   │   ├── CoreInterface.hpp/cpp
│   │   └── OffScreen.hpp/cpp
│   └── core/
│       └── MainLoop.hpp/cpp
├── services/
│   ├── providers/          # Interfaces for Core to implement
│   │   ├── IOffScreenProvider.hpp
│   │   ├── IEditorTextureProvider.hpp
│   │   └── IPreviewProvider.hpp
│   ├── interfaces/         # Public service interfaces
│   │   ├── ISceneService.hpp
│   │   ├── IRenderService.hpp
│   │   ├── IInputService.hpp
│   │   └── IPreviewService.hpp
│   ├── impl/               # Service implementations
│   │   ├── SceneServiceImpl.hpp/cpp
│   │   ├── RenderServiceImpl.hpp/cpp
│   │   ├── InputServiceImpl.hpp/cpp
│   │   └── PreviewServiceImpl.hpp/cpp
│   ├── events/             # Event definitions
│   │   ├── EventDispatcher.hpp/cpp
│   │   ├── EventTypes.hpp
│   │   ├── ApplicationEvents.hpp
│   │   ├── SceneEvents.hpp
│   │   ├── RenderEvents.hpp
│   │   ├── InputEvents.hpp
│   │   └── ResourceEvents.hpp
│   └── data/
│       └── DTOs.hpp        # Data transfer objects
├── editor/
│   ├── handlers/
│   │   ├── EditorHandler.hpp/cpp
│   │   └── WindowImguiHandler.hpp/cpp
│   └── windows/            # ImGui windows
├── runtime/
│   ├── handlers/
│   │   └── RuntimeHandler.hpp/cpp
│   └── run/
│       └── Main.cpp
├── graphics/               # Vulkan rendering
├── window/                 # GLFW window & input
├── import/                 # Asset pipeline
└── utilities/              # Shared utilities
```

## Project Build Order

```
1. Utilities (no dependencies)
2. Window → depends on Utilities
3. Graphics → depends on Window, Utilities
4. Import → depends on Utilities
5. Services → depends on Utilities (interfaces only)
6. Core → depends on Graphics, Window, Services (implements adapters)
7. Editor → depends on Services, Import, Core
8. Runtime → depends on Services, Core
```

## Application Events

### ApplicationEvents.hpp
| Event | Type | Description |
|-------|------|-------------|
| CloseCommand | Command | Request application close |
| CloseRequestedNotification | Notification | Application close was requested |
| WindowResizedNotification | Notification | Window framebuffer size changed |
| WindowMinimizedNotification | Notification | Window was minimized |
| WindowRestoredNotification | Notification | Window was restored from minimize |
| WindowFocusedNotification | Notification | Window focus state changed |
