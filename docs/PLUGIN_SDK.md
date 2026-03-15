# VertexForge Plugin SDK

## Overview

VertexForge plugins are shared libraries (`.dll`) that extend engine functionality at runtime. Plugins can register custom ECS components, editor windows, import pipeline stages, render hooks, script functions, and communicate via events.

## Quick Start

### 1. Create Plugin Source

```cpp
// HelloPlugin.cpp
#include "api/IPlugin.hpp"
#include "api/PluginContext.hpp"
#include "api/PluginComponentBuilder.hpp"
#include "api/PluginComponentData.hpp"
#include "api/PluginExport.hpp"

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
        ctx->logInfo("Hello from plugin!");
        return true;
    }

    void onUpdate(float deltaTime) override {}
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
    "apiVersion": 4,
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
-- Adjust paths to point to your VertexForge installation
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

Copy the built `.dll` and `.vfplugin` file into the engine's `plugins/` directory.

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
                   Run game logic here

shutdownAll()    → Calls onShutdown() in reverse load order
                   Engine auto-cleans registered resources
                   Calls vfDestroyPlugin()
```

## Custom Components

Register components with typed properties. The engine auto-generates serialization and inspector UI.

```cpp
bool onInitialize(plugin::PluginContext* context) override
{
    ctx = context;

    // Declare a component with typed properties
    ctx->registerComponent("Health")
        .addInt("maxHP", 100, 1, 10000)       // name, default, min, max
        .addInt("currentHP", 100, 0, 10000)
        .addFloat("regenRate", 1.0f, 0.0f, 100.0f)
        .addBool("invincible", false)
        .build();

    return true;
}
```

### Available Property Types

| Method | Type | Inspector Control |
|--------|------|-------------------|
| `addInt(name, default, min, max)` | int | DragInt |
| `addFloat(name, default, min, max)` | float | DragFloat |
| `addBool(name, default)` | bool | Checkbox |
| `addString(name, default)` | string | InputText |
| `addVec2(name, default)` | glm::vec2 | DragFloat2 |
| `addVec3(name, default)` | glm::vec3 | DragFloat3 |
| `addVec4(name, default)` | glm::vec4 | DragFloat4 |
| `addColor(name, default)` | glm::vec4 | ColorEdit4 |
| `addArray(name)...endArray()` | JSON array | Expandable list with +/- buttons |
| `addObject(name)...endObject()` | JSON object | Expandable tree |

### Arrays and Objects

```cpp
ctx->registerComponent("Inventory")
    .addInt("maxSlots", 20, 1, 100)
    .addArray("items")                  // dynamic list
        .addString("name", "Empty")     // element schema
        .addInt("count", 1, 0, 999)
        .addBool("equipped", false)
    .endArray()
    .build();

ctx->registerComponent("Quest")
    .addString("title", "")
    .addObject("reward")                // nested object
        .addInt("gold", 0, 0, 99999)
        .addInt("experience", 0, 0, 99999)
    .endObject()
    .addArray("objectives")
        .addString("description", "")
        .addBool("completed", false)
        .addInt("current", 0, 0, 9999)
        .addInt("target", 1, 1, 9999)
    .endArray()
    .build();
```

### Accessing Component Data

```cpp
// Add component to an entity
ctx->addPluginComponent(entity, "Health");

// Read/write typed data (DLL-safe)
auto* health = ctx->getPluginComponent(entity, "Health");
if (health) {
    int hp = health->getInt("currentHP");
    health->setFloat("regenRate", 5.0f);
}

// Check existence
if (ctx->hasPluginComponent(entity, "Health")) { ... }

// Remove
ctx->removePluginComponent(entity, "Health");

// Iterate all entities with a component
ctx->forEachWithComponent("Health", [](entt::entity e, plugin::PluginComponentData& data) {
    int hp = data.getInt("currentHP");
    // game logic...
});
```

### Array Data Access

```cpp
auto* inv = ctx->getPluginComponent(entity, "Inventory");
if (inv) {
    size_t count = inv->getArraySize("items");
    auto item = inv->getArrayElement("items", 0);
    if (item.isValid()) {
        std::string name = item.getString("name");
    }

    // Add new element
    inv->addArrayElement("items", {{"name", "Sword"}, {"count", 1}, {"equipped", true}});

    // Remove element
    inv->removeArrayElement("items", 0);
}
```

### Custom Inspector Override

For advanced UI needs, provide a custom ImGui inspector:

```cpp
ctx->registerComponent("MyComponent")
    .addFloat("value", 0.0f)
    .setInspector([](nlohmann::json& data) -> bool {
        // Custom ImGui drawing (editor-only)
        // Return true if data was modified
        float v = data.value("value", 0.0f);
        if (ImGui::SliderFloat("Custom Slider", &v, 0.0f, 1.0f)) {
            data["value"] = v;
            return true;
        }
        return false;
    })
    .build();
```

**Note:** When using `setInspector()`, call `ImGui::SetCurrentContext(ctx->getImGuiContext())` in `onInitialize()` and gate behind `ctx->hasCapability(plugin::capability::editor)`.

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

## Capabilities

Check engine feature availability before using capability-specific APIs:

```cpp
if (ctx->hasCapability(plugin::capability::editor)) {
    // Editor-only features: registerEditorWindow, ImGui context
}
if (ctx->hasCapability(plugin::capability::graphics)) {
    // Render hooks
}
if (ctx->hasCapability(plugin::capability::scripting)) {
    // Script function registration
}
```

| Capability | Editor | Runtime | Features |
|-----------|--------|---------|----------|
| `editor` | Yes | No | Editor windows, ImGui |
| `audio` | Yes | Yes | Audio system |
| `physics` | Yes | Yes | Physics system |
| `import_` | Yes | No | Import pipeline stages |
| `scripting` | Yes | Yes | mType script functions |
| `graphics` | Yes | No | Render pass hooks |

## Editor Windows

Register custom ImGui windows (editor-only):

```cpp
#include "imguiHandler/ImguiWindow.hpp"

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
if (ctx->hasCapability(plugin::capability::editor)) {
    ImGui::SetCurrentContext(ctx->getImGuiContext());
    auto window = std::make_shared<MyWindow>();
    ctx->registerEditorWindow(window);
}
```

## Descriptor File (.vfplugin)

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Plugin display name (must match `getInfo().name`) |
| `version` | string | Yes | Semantic version `"major.minor.patch"` |
| `apiVersion` | int | Yes | Must match engine API version (currently 4) |
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

## DLL Boundary Safety

EnTT `type_id<T>` differs across DLL boundaries. Never call `registry.emplace<YourType>()` from a plugin DLL. Instead:

- Use `registerComponent()` + `addPluginComponent()` for custom components
- Use `forEachWithComponent()` to iterate
- Use `getPluginComponent()` / `PluginComponentData` for typed access
- Use `publishEvent()` / `subscribeEvent()` for events (JSON payloads)

The `getRegistry()` method is safe for **built-in** component types (TransformComponent, etc.) since those are defined in the engine executable.

## Plugin Manager

Access via **Settings > Plugin Manager** in the editor. Browse, inspect, and enable/disable plugins without editing files manually.
