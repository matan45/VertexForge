#include "api/IPlugin.hpp"
#include "api/PluginContext.hpp"
#include "api/PluginComponentBuilder.hpp"
#include "api/PluginComponentData.hpp"
#include "api/PluginExport.hpp"

class TestComponentPlugin : public plugin::IPlugin
{
    plugin::PluginContext* ctx = nullptr;

public:
    plugin::PluginInfo getInfo() const override
    {
        return {"TestComponents", "VertexForge", "Test plugin for custom component registration", 1, 0, 0};
    }

    bool onInitialize(plugin::PluginContext* context) override
    {
        ctx = context;

        // Register a "Health" component with auto-generated inspector
        ctx->registerComponent("Health")
            .addInt("maxHP", 100, 1, 10000)
            .addInt("currentHP", 100, 0, 10000)
            .addFloat("regenRate", 1.0f, 0.0f, 100.0f)
            .addBool("invincible", false)
            .build();

        // Register an "Inventory" component
        ctx->registerComponent("Inventory")
            .addInt("slots", 10, 1, 100)
            .addString("containerType", "backpack")
            .build();

        // Register a "Waypoint" component with vec3 and color
        ctx->registerComponent("Waypoint")
            .addVec3("position", {0.0f, 0.0f, 0.0f})
            .addFloat("radius", 1.0f, 0.1f, 100.0f)
            .addColor("color", {1.0f, 1.0f, 0.0f, 1.0f})
            .addBool("active", true)
            .build();

        ctx->logInfo("TestComponentPlugin initialized with Health, Inventory, and Waypoint components");
        return true;
    }

    void onUpdate(float deltaTime) override
    {
        (void)deltaTime;
    }

    void onShutdown() override
    {
        ctx->logInfo("TestComponentPlugin shutting down");
    }
};

VF_IMPLEMENT_PLUGIN(TestComponentPlugin)
