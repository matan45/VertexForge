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

        // Health component — flat properties
        ctx->registerComponent("Health")
            .addInt("maxHP", 100, 1, 10000)
            .addInt("currentHP", 100, 0, 10000)
            .addFloat("regenRate", 1.0f, 0.0f, 100.0f)
            .addBool("invincible", false)
            .build();

        // Inventory component — uses array of item objects
        ctx->registerComponent("Inventory")
            .addInt("maxSlots", 20, 1, 100)
            .addArray("items")                      // dynamic list of items
                .addString("name", "Empty")         // each item has a name
                .addInt("count", 1, 0, 999)         // stack count
                .addFloat("weight", 0.0f, 0.0f, 100.0f)
                .addBool("equipped", false)
            .endArray()
            .build();

        // Quest component — uses nested object + array
        ctx->registerComponent("Quest")
            .addString("questId", "")
            .addString("title", "Untitled Quest")
            .addString("status", "inactive")        // inactive, active, completed, failed
            .addObject("reward")                    // nested object
                .addInt("gold", 0, 0, 99999)
                .addInt("experience", 0, 0, 99999)
                .addString("itemReward", "")
            .endObject()
            .addArray("objectives")                 // array of objective objects
                .addString("description", "")
                .addBool("completed", false)
                .addInt("current", 0, 0, 9999)
                .addInt("target", 1, 1, 9999)
            .endArray()
            .build();

        // Waypoint component — simple vec3 + color
        ctx->registerComponent("Waypoint")
            .addVec3("position", {0.0f, 0.0f, 0.0f})
            .addFloat("radius", 1.0f, 0.1f, 100.0f)
            .addColor("color", {1.0f, 1.0f, 0.0f, 1.0f})
            .addBool("active", true)
            .build();

        // Subscribe to custom events
        ctx->subscribeEvent("DamageEntity", [this](const nlohmann::json& data)
        {
            if (!data.contains("entity") || !data.contains("amount"))
                return;

            auto entity = static_cast<entt::entity>(data["entity"].get<uint32_t>());
            int damage = data["amount"].get<int>();

            auto* health = ctx->getPluginComponent(entity, "Health");
            if (!health || health->getBool("invincible"))
                return;

            int curHP = health->getInt("currentHP");
            int newHP = std::max(0, curHP - damage);
            health->setInt("currentHP", newHP);

            if (newHP <= 0)
                ctx->publishEvent("EntityDied", {{"entity", static_cast<uint32_t>(entity)}});
        });

        ctx->subscribeEvent("EntityDied", [this](const nlohmann::json&)
        {
            ctx->logInfo("Entity died!");
        });

        ctx->logInfo("TestComponentPlugin initialized");
        return true;
    }

    void onUpdate(float deltaTime) override
    {
        // Health regen
        ctx->forEachWithComponent("Health", [&](entt::entity entity, plugin::PluginComponentData& data)
        {
            if (data.getBool("invincible"))
                return;

            int maxHP = data.getInt("maxHP");
            int curHP = data.getInt("currentHP");
            float regen = data.getFloat("regenRate");

            if (curHP < maxHP)
            {
                float newHP = static_cast<float>(curHP) + regen * deltaTime;
                data.setInt("currentHP", std::min(static_cast<int>(newHP), maxHP));
            }
        });
    }

    void onShutdown() override
    {
        ctx->logInfo("TestComponentPlugin shutting down");
    }
};

VF_IMPLEMENT_PLUGIN(TestComponentPlugin)
