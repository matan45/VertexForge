#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>

enum class ElementType : int { Fire = 0, Water, Earth, Wind };

// Native component struct — registered with EnTT meta for cross-DLL type identity
struct TestComponent
{
    int health = 100;
    float speed = 5.0f;
    bool isActive = true;
    glm::vec3 offset{0.0f, 1.0f, 0.0f};
    std::vector<int> scores{10, 20, 30};
    std::map<std::string, float> stats{{"strength", 5.0f}, {"agility", 3.5f}};
    ElementType element = ElementType::Fire;
};

class PluginAPITest : public plugin::IPlugin
{
public:
    plugin::PluginInfo getInfo() const override
    {
        return {"PluginAPITest", "VertexForge", "Tests plugin APIs: VK-1276/1286/1287/1288", 1, 0, 0};
    }

    bool onInitialize(plugin::PluginContext* context) override
    {
        ctx = context;

        // ================================================================
        // VK-1288: Register native component with meta reflection
        // ================================================================
        // Register enum type with named values
        entt::meta_factory<ElementType>()
            .data<ElementType::Fire>("Fire")
            .data<ElementType::Water>("Water")
            .data<ElementType::Earth>("Earth")
            .data<ElementType::Wind>("Wind");

        ctx->registerNativeComponent<TestComponent>("TestComponent")
            .data<&TestComponent::health>("health")
            .data<&TestComponent::speed>("speed")
            .data<&TestComponent::isActive>("isActive")
            .data<&TestComponent::offset>("offset")
            .data<&TestComponent::scores>("scores")
            .data<&TestComponent::stats>("stats")
            .data<&TestComponent::element>("element");

        ctx->logInfo("PluginAPITest initialized - TestComponent registered via meta");
        return true;
    }

    void onUpdate(float deltaTime) override
    {
        // ================================================================
        // VK-1276: Audio/Physics/Terrain/Input API tests
        // ================================================================
        if (ctx->hasCapability(std::string(plugin::capability::input)))
        {
            // F9: Test native component + engine queries
            if (ctx->isKeyPressed(298))
            {
                ctx->logInfo("[Input] F9 pressed!");

                // Test native EnTT component via registry
                auto& reg = ctx->getRegistry();
                auto testEntity = reg.create();
                reg.emplace<TestComponent>(testEntity, TestComponent{200, 10.0f, true, {1, 2, 3}});

                auto& comp = reg.get<TestComponent>(testEntity);
                ctx->logInfo("[ECS] Native component: health=" + std::to_string(comp.health) +
                    " speed=" + std::to_string(comp.speed));

                // Count via view
                int count = 0;
                for (auto e : reg.view<TestComponent>()) { count++; (void)e; }
                ctx->logInfo("[ECS] Entities with TestComponent: " + std::to_string(count));

                reg.destroy(testEntity);

                // Test terrain
                if (ctx->hasCapability(std::string(plugin::capability::terrain)))
                {
                    auto result = ctx->getTerrainHeightAt(0.0f, 0.0f);
                    ctx->logInfo("[Terrain] Height at (0,0): " +
                        (result.valid ? std::to_string(result.height) : "no terrain"));
                }

                // Test physics
                if (ctx->hasCapability(std::string(plugin::capability::physics)))
                {
                    auto hit = ctx->raycast(glm::vec3(0, 100, 0), glm::vec3(0, -1, 0), 200.0f);
                    ctx->logInfo("[Physics] Raycast: " +
                        (hit.hit ? "hit at " + std::to_string(hit.distance) : "miss"));
                }

                // Test navmesh
                if (ctx->hasCapability(std::string(plugin::capability::navmesh)))
                {
                    ctx->logInfo("[NavMesh] Has navmesh: " +
                        std::string(ctx->hasNavmesh() ? "yes" : "no"));
                }
            }

            // F10: Test audio
            if (ctx->isKeyPressed(299))
            {
                if (ctx->hasCapability(std::string(plugin::capability::audio)))
                {
                    ctx->logInfo("[Audio] Master bus volume: " +
                        std::to_string(ctx->getBusVolume("Master")));
                }
            }

            // F11: Test mouse
            if (ctx->isKeyPressed(300))
            {
                auto pos = ctx->getMousePosition();
                ctx->logInfo("[Input] Mouse: (" +
                    std::to_string(pos.x) + ", " + std::to_string(pos.y) + ")");
            }
        }
    }

    void onShutdown() override
    {
        ctx->logInfo("PluginAPITest shutdown");
    }

private:
    plugin::PluginContext* ctx = nullptr;
};

VF_IMPLEMENT_PLUGIN(PluginAPITest)
