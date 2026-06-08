# VertexForge Plugin SDK

## Overview

VertexForge plugins are shared libraries (`.dll`) that extend engine functionality at runtime. Plugins can register native ECS components, editor windows, import pipeline stages, render hooks, script functions, and communicate via events.

## Quick Start

> **Scaffolding (VK-1284):** the editor can generate all of the boilerplate below. Open **Settings > Plugins > New Plugin...**, pick a name, capabilities and optional examples (component registration, editor window), and it creates `plugins/<Name>/` with the three authored files (`premake5.lua`, `<Name>.cpp`, `<Name>.vfplugin`) — `apiVersion` is always the engine's current `VF_PLUGIN_API_VERSION`. Then run `premake5 vs2022` (the project is auto-discovered), build it, and restart the editor. Out-of-tree authors keep using `sdk/template/` (`premake5 export-sdk`).

### 1. Create Plugin Source

```cpp
// HelloPlugin.cpp
#include "api/IPlugin.hpp"
#include "api/PluginContext.hpp"
#include "api/PluginExport.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>

// Define a native component struct
struct Health {
    int maxHP = 100;
    int currentHP = 100;
    float regenRate = 1.0f;
};

class HelloPlugin : public plugin::IPlugin
{
    plugin::PluginContext* ctx = nullptr;

public:
    plugin::PluginInfo getInfo() const override
    {
        return {"HelloPlugin", "YourName", "My first plugin", 1, 0, 0};
    }

    bool onInitialize(plugin::PluginContext* context) override
    {
        ctx = context;

        // Register component with meta reflection (enables inspector + Add Component UI)
        ctx->registerNativeComponent<Health>("Health")
            .data<&Health::maxHP>("maxHP")
            .data<&Health::currentHP>("currentHP")
            .data<&Health::regenRate>("regenRate");

        ctx->logInfo("Hello from plugin!");
        return true;
    }

    void onUpdate(float deltaTime) override
    {
        // Runs every frame in both editor and play mode
        auto& reg = ctx->getRegistry();
        for (auto [entity, health] : reg.view<Health>().each())
        {
            if (health.currentHP < health.maxHP)
                health.currentHP += static_cast<int>(health.regenRate * deltaTime);
        }
    }

    void onShutdown() override {}
};

VF_IMPLEMENT_PLUGIN(HelloPlugin)
```

### 2. Create Descriptor File

Create `HelloPlugin.vfplugin` alongside your DLL:

```json
{
    "name": "HelloPlugin",
    "version": "1.0.0",
    "apiVersion": 5,
    "author": "YourName",
    "description": "My first plugin",
    "capabilities": [],
    "dependencies": [],
    "loadOrder": 100,
    "enabled": true,
    "library": "HelloPlugin.dll"
}
```

### 3. Build Configuration (premake5.lua)

```lua
local vfRoot = "path/to/VertexForge"
local vulkanSdk = os.getenv("VULKAN_SDK")

project "HelloPlugin"
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"

    files { "**.hpp", "**.cpp" }

    includedirs {
        vfRoot .. "/dependencies/spdlog/include",
        vfRoot .. "/dependencies/glm",
        vfRoot .. "/dependencies/entt/single_include",
        vfRoot .. "/dependencies/imgui",
        vfRoot .. "/dependencies/json/single_include",
        vulkanSdk .. "/Include",
        vfRoot .. "/VFEngine/plugin",
        vfRoot .. "/VFEngine/utilities",
        vfRoot .. "/VFEngine/services"
    }

    defines { "_CRT_SECURE_NO_WARNINGS" }
```

### 4. Deploy

Copy the built `.dll` and `.vfplugin` file into a subfolder under the engine's `plugins/` directory:

```
plugins/
  HelloPlugin/
    HelloPlugin.dll
    HelloPlugin.vfplugin
```

---

## Plugin Lifecycle

```
loadAll()        → Engine scans plugins/ for .vfplugin files
                   Validates API version, checks dependencies
                   Loads DLL, calls vfCreatePlugin()

initializeAll()  → Calls onInitialize(context) for each plugin
                   Plugin registers components, windows, events, etc.
                   Return false to abort (plugin is unloaded)

updateAll(dt)    → Calls onUpdate(deltaTime) each frame
                   Runs in both editor and play mode

shutdownAll()    → Calls onShutdown() in reverse load order
                   Engine auto-cleans registered resources
                   Calls vfDestroyPlugin()
```

## Native Components

Plugins define components as plain C++ structs and register them with `registerNativeComponent<T>()`. The engine auto-generates inspector UI from meta reflection data.

### Registration

```cpp
struct Health {
    int maxHP = 100;
    int currentHP = 100;
    float regenRate = 1.0f;
    bool invincible = false;
};

bool onInitialize(plugin::PluginContext* context) override
{
    ctx = context;

    ctx->registerNativeComponent<Health>("Health")
        .data<&Health::maxHP>("maxHP")
        .data<&Health::currentHP>("currentHP")
        .data<&Health::regenRate>("regenRate")
        .data<&Health::invincible>("invincible");

    return true;
}
```

### Supported Property Types for Auto-Inspector

| C++ Type | Inspector Widget |
|----------|-----------------|
| `int` | DragInt |
| `float` | DragFloat |
| `bool` | Checkbox |
| `std::string` | InputText |
| `glm::vec2` | DragFloat2 |
| `glm::vec3` | DragFloat3 |
| `glm::vec4` | DragFloat4 |
| `glm::quat` | DragFloat3 (euler angles) |

### Accessing Components

Use the EnTT registry directly — zero overhead, cache-friendly:

```cpp
auto& reg = ctx->getRegistry();

// Add component to entity
reg.emplace<Health>(entity, Health{200, 200, 5.0f, false});

// Read/write
auto& health = reg.get<Health>(entity);
health.currentHP -= 10;

// Check existence
if (reg.all_of<Health>(entity)) { ... }

// Remove
reg.remove<Health>(entity);

// Iterate all entities with component (fast dense iteration)
for (auto [entity, health] : reg.view<Health>().each())
{
    // game logic...
}

// Multi-component views
for (auto [entity, health, transform] : reg.view<Health, components::TransformComponent>().each())
{
    // ...
}
```

### Add Component UI

Registered components appear in the editor's **Add Component > Plugins** menu. Selecting a component adds it to the selected entity with default values. The inspector auto-generates controls for each registered data member.

### Important: Component Names Must Be Unique

Component names are hashed to `entt::id_type`. Two plugins registering the same name (e.g. `"Health"`) will collide. Use prefixed names if needed (e.g. `"MyPlugin_Health"`).

## Plugin Events

Plugins communicate via a string-keyed event bus with JSON payloads.

```cpp
// Publish (fire-and-forget, any plugin can listen)
ctx->publishEvent("DamageEntity", {{"entity", entityId}, {"amount", 50}});

// Subscribe (auto-cleaned on plugin unload)
ctx->subscribeEvent("EntityDied", [](const nlohmann::json& data) {
    uint32_t entity = data["entity"].get<uint32_t>();
    // handle death...
});
```

Events are decoupled — publishers and subscribers don't know about each other. Use this for cross-plugin communication.

## Engine Event System (CQRS)

For engine events, use the `EventDispatcher` directly:

```cpp
auto& dispatcher = ctx->getEventDispatcher();

// Subscribe to engine notifications
auto token = ctx->managedSubscribe(
    dispatcher.subscribe<events::scene::EntityCreatedNotification>(
        [](const auto& n) {
            // An entity was created
        }
    )
);
```

Use `managedSubscribe()` to ensure automatic cleanup on plugin unload.

## Engine API

PluginContext provides direct API methods for common engine systems. All are capability-gated.

### Audio

```cpp
auto handle = ctx->playSound3D("assets/audio/explosion.vfAudio", position);
ctx->setSoundVolume(handle, 0.8f);
ctx->stopSound(handle);
bool playing = ctx->isSoundPlaying(handle);
ctx->setBusVolume("SFX", 0.5f);
```

### Physics

```cpp
auto hit = ctx->raycast(origin, direction, 100.0f);
if (hit.hit) { /* hit.point, hit.normal, hit.distance */ }

auto hits = ctx->raycastAll(origin, direction, 100.0f);

ctx->applyForce(entity, glm::vec3(0, 100, 0));
ctx->applyImpulse(entity, glm::vec3(10, 0, 0));
ctx->setLinearVelocity(entity, glm::vec3(0, 5, 0));
glm::vec3 vel = ctx->getLinearVelocity(entity);
bool grounded = ctx->isGrounded(entity);
```

### Terrain

```cpp
auto result = ctx->getTerrainHeightAt(worldX, worldZ);
if (result.valid) { float height = result.height; }

auto hit = ctx->getTerrainHit(); // editor cursor raycast
```

### Input

```cpp
// Raw input
if (ctx->isKeyPressed(GLFW_KEY_SPACE)) { /* just pressed this frame */ }
if (ctx->isKeyDown(GLFW_KEY_W)) { /* held down */ }
if (ctx->isMouseButtonDown(0)) { /* left mouse held */ }
glm::vec2 mousePos = ctx->getMousePosition();
glm::vec2 delta = ctx->getMouseDelta();

// Action-based input (uses registered action mappings)
if (ctx->isActionPressed("Jump")) { /* ... */ }
float moveX = ctx->getAxis1DValue("MoveHorizontal");
glm::vec2 move = ctx->getAxis2DValue("Move");
```

### NavMesh

```cpp
ctx->setAgentDestination(entity, targetPos);
ctx->stopAgent(entity);
glm::vec3 vel = ctx->getAgentVelocity(entity);
float speed = ctx->getAgentSpeed(entity);

auto path = ctx->findPath(start, end);
if (path.isValid) { /* path.waypoints */ }

bool onNav = ctx->isPointOnNavmesh(point);
glm::vec3 closest = ctx->getClosestPointOnNavmesh(point);
bool hasNav = ctx->hasNavmesh();
```

### VFX

```cpp
services::VFXRuntimeParams params;
params.vfxAssetPath = "assets/vfx/explosion.vfx";
params.worldTransform = transform;
auto id = ctx->createVFXInstance(params);

ctx->playVFXInstance(id);
ctx->setVFXInstanceTransform(id, newTransform);
ctx->stopVFXInstance(id);
ctx->destroyVFXInstance(id);
```

## Capabilities

Check engine feature availability before using capability-specific APIs:

```cpp
if (ctx->hasCapability(std::string(plugin::capability::editor))) {
    // Editor-only features: registerEditorWindow, ImGui context
}
```

| Capability | Editor | Runtime | Features |
|-----------|--------|---------|----------|
| `editor` | Yes | No | Editor windows, ImGui |
| `audio` | Yes | Yes | Sound playback, bus control |
| `physics` | Yes | Yes | Raycast, forces, velocity |
| `terrain` | Yes | Yes | Height queries, cursor hit |
| `input` | Yes | Yes | Key/mouse/action polling |
| `navmesh` | Yes | Yes | Navigation, pathfinding |
| `vfx` | Yes | Yes | Particle effects |
| `import_` | Yes | No | Import pipeline stages |
| `scripting` | Yes | Yes | mType script functions |
| `graphics` | Yes | No | Render pass hooks |

## Editor Windows

Register custom ImGui windows (editor-only). Requires linking `imgui`:

```cpp
#include "imguiHandler/ImguiWindow.hpp"
#include <imgui.h>

class MyWindow : public controllers::imguiHandler::ImguiWindow {
    bool visible = false;
public:
    void draw() override {
        if (!visible) return;
        if (ImGui::Begin("My Plugin Window", &visible)) {
            ImGui::Text("Hello from plugin!");
        }
        ImGui::End();
    }
    void show() { visible = true; }
};

// In onInitialize:
if (ctx->hasCapability(std::string(plugin::capability::editor))) {
    ImGui::SetCurrentContext(ctx->getImGuiContext());
    auto window = std::make_shared<MyWindow>();
    ctx->registerEditorWindow(window);
}
```

**Note:** Only plugins that draw custom ImGui windows need to link `imgui` and call `SetCurrentContext()`. Plugins that only register components and use the PluginContext API do not need ImGui.

## Descriptor File (.vfplugin)

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Unique plugin name |
| `version` | string | Yes | Semantic version `"major.minor.patch"` |
| `apiVersion` | int | Yes | Must match engine API version (currently 5) |
| `author` | string | No | Author name |
| `description` | string | No | Plugin description |
| `capabilities` | string[] | No | Required engine capabilities |
| `dependencies` | string[] | No | Names of plugins this depends on |
| `loadOrder` | int | No | Lower = loaded first (default 100) |
| `enabled` | bool | No | Set false to disable (default true) |
| `library` | string | Yes | DLL filename relative to descriptor |

### Dependencies and Load Order

Plugins are loaded via topological sort based on `dependencies`. Within the same dependency depth, `loadOrder` is the tiebreaker. Circular dependencies are detected and rejected.

```json
{
    "name": "CombatPlugin",
    "dependencies": ["CorePlugin", "HealthPlugin"],
    "loadOrder": 200
}
```

## DLL Boundary Notes

Each plugin DLL has its own EnTT type ID space. This means:

- `registry.emplace<T>()` / `registry.get<T>()` / `registry.view<T>()` all work **within the same plugin DLL**
- The engine accesses plugin components through type-erased bridges (created automatically by `registerNativeComponent<T>()`)
- The engine's auto-inspector and Add Component UI use these bridges — no shared type IDs needed
- `getRegistry()` is safe for **built-in** engine component types (TransformComponent, etc.) from any plugin
- Use `publishEvent()` / `subscribeEvent()` with JSON payloads for cross-plugin data exchange

## Plugin Manager

Access via **Settings > Plugins** in the editor. Browse all discovered plugins, view details (version, author, capabilities, dependencies), and enable/disable plugins. Changes take effect on next editor launch.

The **New Plugin...** button scaffolds a ready-to-build plugin project under `plugins/` (see Quick Start above). The new entry appears in the list as *Not Loaded* immediately; it shows *Loaded* after you regenerate the solution, build it, and restart the editor.
