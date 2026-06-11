#include <doctest.h>
#include <render/lighting/LightStreamManager.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// ============================================================
// LightStreamManager: per-sector registration, priority-budgeted
// slot allocation, hysteresis, and defragmentation (CPU-only)
// ============================================================

namespace
{
    using render::lighting::LightStreamEntry;
    using render::lighting::LightStreamingConfig;
    using render::lighting::LightStreamManager;

    struct RegistryLights
    {
        std::vector<entt::entity> entities;

        uint32_t addPointLight(const glm::vec3& position, float intensity = 1.0f,
                               float radius = 10.0f, bool isStatic = false,
                               bool castsShadow = false)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            auto entity = registry.create();

            auto& light = registry.emplace<components::PointLightComponent>(entity);
            light.intensity = intensity;
            light.radius = radius;
            light.castsShadow = castsShadow;

            auto& transform = registry.emplace<components::TransformComponent>(entity);
            transform.position = position;
            transform.isStatic = isStatic;

            auto& world = registry.emplace<components::WorldTransformComponent>(entity);
            world.worldMatrix = glm::translate(glm::mat4(1.0f), position);

            entities.push_back(entity);
            return static_cast<uint32_t>(entity);
        }

        uint32_t addSpotLight(const glm::vec3& position)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            auto entity = registry.create();
            registry.emplace<components::SpotLightComponent>(entity);
            auto& world = registry.emplace<components::WorldTransformComponent>(entity);
            world.worldMatrix = glm::translate(glm::mat4(1.0f), position);
            entities.push_back(entity);
            return static_cast<uint32_t>(entity);
        }

        uint32_t addNonLight()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            auto entity = registry.create();
            registry.emplace<components::TransformComponent>(entity);
            entities.push_back(entity);
            return static_cast<uint32_t>(entity);
        }

        void setPosition(uint32_t entityId, const glm::vec3& position)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            auto entity = static_cast<entt::entity>(entityId);
            registry.get<components::WorldTransformComponent>(entity).worldMatrix =
                glm::translate(glm::mat4(1.0f), position);
        }

        ~RegistryLights()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            for (auto entity : entities)
            {
                if (registry.valid(entity))
                    registry.destroy(entity);
            }
        }
    };

    LightStreamingConfig smallConfig(uint32_t maxPoint, uint32_t maxSpot,
                                     float hysteresisMargin = 0.0f)
    {
        LightStreamingConfig config;
        config.maxPointLights = maxPoint;
        config.maxSpotLights = maxSpot;
        config.hysteresisMargin = hysteresisMargin;
        return config;
    }
}

TEST_SUITE("LightStreaming")
{
    TEST_CASE("registerSectorLights only accepts entities with light components")
    {
        RegistryLights scope;
        uint32_t point = scope.addPointLight({0.0f, 0.0f, 0.0f});
        uint32_t spot = scope.addSpotLight({5.0f, 0.0f, 0.0f});
        uint32_t plain = scope.addNonLight();

        LightStreamManager manager;
        manager.init(smallConfig(8, 8));
        manager.registerSectorLights(1, {point, spot, plain});

        auto stats = manager.getStats();
        CHECK(stats.registeredPointLights == 1);
        CHECK(stats.registeredSpotLights == 1);
        CHECK(manager.isLightActive(point));
        CHECK(manager.isLightActive(spot));
        CHECK_FALSE(manager.isLightActive(plain));
    }

    TEST_CASE("lights beyond the slot budget stay inactive")
    {
        RegistryLights scope;
        std::vector<uint32_t> lights;
        for (int i = 0; i < 3; ++i)
            lights.push_back(scope.addPointLight({static_cast<float>(i * 10), 0.0f, 0.0f}));

        LightStreamManager manager;
        manager.init(smallConfig(2, 1));
        manager.registerSectorLights(1, lights);

        auto stats = manager.getStats();
        CHECK(stats.registeredPointLights == 3);
        CHECK(stats.activePointLights == 2);
        CHECK(stats.excludedByBudget == 1);
        CHECK(stats.pointPoolUtilization == doctest::Approx(1.0f));
    }

    TEST_CASE("applyBudget swaps the active light to the higher-priority one")
    {
        RegistryLights scope;
        // Register the far light first so it grabs the only slot
        uint32_t farLight = scope.addPointLight({1000.0f, 0.0f, 0.0f});
        uint32_t nearLight = scope.addPointLight({10.0f, 0.0f, 0.0f});

        LightStreamManager manager;
        manager.init(smallConfig(1, 1));
        manager.registerSectorLights(1, {farLight, nearLight});
        REQUIRE(manager.isLightActive(farLight));
        REQUIRE_FALSE(manager.isLightActive(nearLight));

        manager.updatePriorities(glm::vec3(0.0f));
        manager.applyBudget();
        CHECK(manager.isLightActive(nearLight));
        CHECK_FALSE(manager.isLightActive(farLight));

        // The lights trade places in the world; the budget follows
        scope.setPosition(farLight, {10.0f, 0.0f, 0.0f});
        scope.setPosition(nearLight, {1000.0f, 0.0f, 0.0f});
        manager.updatePriorities(glm::vec3(0.0f));
        manager.applyBudget();
        CHECK(manager.isLightActive(farLight));
        CHECK_FALSE(manager.isLightActive(nearLight));
    }

    TEST_CASE("hysteresis margin keeps a marginal light from popping out")
    {
        RegistryLights scope;
        // Two lights at nearly identical distance: priority gap smaller than margin.
        // The slightly-farther light registers first and holds the only slot.
        uint32_t holder = scope.addPointLight({101.0f, 0.0f, 0.0f});
        uint32_t challenger = scope.addPointLight({100.0f, 0.0f, 0.0f});

        LightStreamManager manager;
        manager.init(smallConfig(1, 1, 0.5f));
        manager.registerSectorLights(1, {holder, challenger});
        REQUIRE(manager.isLightActive(holder));
        REQUIRE_FALSE(manager.isLightActive(challenger));

        // The challenger wins on raw priority, but only by a hair: within the
        // hysteresis margin the holder keeps its slot (no per-frame thrashing)
        manager.updatePriorities(glm::vec3(0.0f));
        manager.applyBudget();
        CHECK(manager.isLightActive(holder));
        CHECK_FALSE(manager.isLightActive(challenger));
    }

    TEST_CASE("static lights earn a priority bonus")
    {
        RegistryLights scope;
        uint32_t staticLight = scope.addPointLight({50.0f, 0.0f, 0.0f}, 1.0f, 10.0f, true);
        uint32_t dynamicLight = scope.addPointLight({50.0f, 0.0f, 5.0f}, 1.0f, 10.0f, false);

        LightStreamManager manager;
        manager.init(smallConfig(8, 8));
        manager.registerSectorLights(1, {staticLight, dynamicLight});
        manager.updatePriorities(glm::vec3(50.0f, 0.0f, 0.0f));

        CHECK(manager.getShadowPriority(staticLight) >
              manager.getShadowPriority(dynamicLight));
    }

    TEST_CASE("unregisterSectorLights releases slots to other sectors")
    {
        RegistryLights scope;
        uint32_t first = scope.addPointLight({0.0f, 0.0f, 0.0f});
        uint32_t second = scope.addPointLight({10.0f, 0.0f, 0.0f});

        LightStreamManager manager;
        manager.init(smallConfig(1, 1));
        manager.registerSectorLights(1, {first});
        manager.registerSectorLights(2, {second});
        REQUIRE(manager.isLightActive(first));
        REQUIRE_FALSE(manager.isLightActive(second));

        manager.unregisterSectorLights(1);
        CHECK_FALSE(manager.isLightActive(first));
        CHECK(manager.getStats().registeredPointLights == 1);

        manager.updatePriorities(glm::vec3(0.0f));
        manager.applyBudget();
        CHECK(manager.isLightActive(second));
    }

    TEST_CASE("unregisterLight removes a single light")
    {
        RegistryLights scope;
        uint32_t light = scope.addPointLight({0.0f, 0.0f, 0.0f});

        LightStreamManager manager;
        manager.init(smallConfig(4, 4));
        manager.registerLight(light, LightStreamEntry::LightType::Point, 7);
        REQUIRE(manager.isLightActive(light));

        manager.unregisterLight(light);
        CHECK_FALSE(manager.isLightActive(light));
        CHECK(manager.getStats().registeredPointLights == 0);
        CHECK(manager.getSlotIndex(light) ==
              render::gpudriven::FreeListAllocator::ALLOCATION_FAILED);
    }

    TEST_CASE("defragStep compacts active slots after unregistration holes")
    {
        LightStreamManager manager;
        manager.init(smallConfig(8, 8));

        // Slot order follows registration order: ids 1..4 -> slots 0..3
        for (uint32_t id = 1; id <= 4; ++id)
            manager.registerLight(id, LightStreamEntry::LightType::Point);

        manager.unregisterLight(1); // frees slot 0
        manager.unregisterLight(3); // frees slot 2

        while (true)
        {
            manager.defragStep(16);
            if (!manager.isDefragInProgress())
                break;
        }

        uint32_t slot2 = manager.getSlotIndex(2);
        uint32_t slot4 = manager.getSlotIndex(4);
        CHECK(slot2 < 2);
        CHECK(slot4 < 2);
        CHECK(slot2 != slot4);
    }

    TEST_CASE("stats report utilization per pool")
    {
        RegistryLights scope;
        uint32_t point = scope.addPointLight({0.0f, 0.0f, 0.0f});
        uint32_t spot = scope.addSpotLight({0.0f, 0.0f, 0.0f});

        LightStreamManager manager;
        manager.init(smallConfig(4, 2));
        manager.registerSectorLights(1, {point, spot});

        auto stats = manager.getStats();
        CHECK(stats.activePointLights == 1);
        CHECK(stats.activeSpotLights == 1);
        CHECK(stats.pointPoolUtilization == doctest::Approx(0.25f));
        CHECK(stats.spotPoolUtilization == doctest::Approx(0.5f));
        CHECK(stats.excludedByBudget == 0);
    }
}
