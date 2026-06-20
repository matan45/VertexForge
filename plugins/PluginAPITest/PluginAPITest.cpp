#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include "asset/AssetRef.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cmath>

enum class ElementType : int { Fire = 0, Water, Earth, Wind };

struct BuffEntry
{
    std::string name = "unnamed";
    float duration = 0.0f;
    int stacks = 1;
    bool isPermanent = false;
};

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
    std::vector<BuffEntry> buffs{{"Shield", 10.0f, 1, false}, {"Regen", 5.0f, 3, true}};
    std::map<std::string, BuffEntry> namedBuffs{{"primary", {"Haste", 8.0f, 2, false}}};
    glm::vec4 tintColor{1.0f, 1.0f, 1.0f, 1.0f};
    // Asset reference field (VK-1422 Phase 2). The plugin only declares the
    // field; the engine owns all serialization/resolution — never call
    // resolve()/fromPath() from plugin code (they need the engine AssetDatabase).
    asset::AssetRef iconAsset;
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

        // Register struct used as container element
        entt::meta_factory<BuffEntry>()
            .type("BuffEntry")
            .ctor<>()
            .data<&BuffEntry::name>("name")
            .data<&BuffEntry::duration>("duration")
            .data<&BuffEntry::stacks>("stacks")
            .data<&BuffEntry::isPermanent>("isPermanent");

        ctx->registerNativeComponent<TestComponent>("TestComponent")
            .data<&TestComponent::health>("health")
            .data<&TestComponent::speed>("speed")
            .data<&TestComponent::isActive>("isActive")
            .data<&TestComponent::offset>("offset")
            .data<&TestComponent::scores>("scores")
            .data<&TestComponent::stats>("stats")
            .data<&TestComponent::element>("element")
            .data<&TestComponent::buffs>("buffs")
            .data<&TestComponent::namedBuffs>("namedBuffs")
            .data<&TestComponent::tintColor>("tintColor")
            .data<&TestComponent::iconAsset>("iconAsset");

        // ----------------------------------------------------------------
        // VK-1422 Phase 3: per-field inspector attributes (API v14).
        // Field names must match the .data<>() names registered above.
        // ----------------------------------------------------------------
        namespace insp = plugin::inspector;

        // Ranged slider on a float, with a display name, units, tooltip, and a
        // shared "Stats" group header (shared with `speed` below).
        ctx->setFieldAttributes("TestComponent", "health",
            insp::Field{}
                .name("Health")
                .help("Current hit points")
                .range(0.f, 1000.f)
                .slider()
                .units("hp")
                .group("Stats")
                .build());

        // Second field in the same "Stats" group, demonstrating a ranged drag.
        ctx->setFieldAttributes("TestComponent", "speed",
            insp::Field{}
                .name("Move Speed")
                .help("World units per second")
                .range(0.f, 50.f)
                .units("m/s")
                .group("Stats")
                .build());

        // Color picker on a vec4 (drawn as ColorEdit4; serialization unchanged).
        ctx->setFieldAttributes("TestComponent", "tintColor",
            insp::Field{}
                .name("Tint Color")
                .help("Multiplied with the base color")
                .color()
                .build());

        // Read-only display field.
        ctx->setFieldAttributes("TestComponent", "isActive",
            insp::Field{}
                .name("Active (read-only)")
                .help("Runtime-managed flag; not author-editable")
                .readOnly()
                .build());

        // AssetRef field restricted to .vfimage in the drag-drop picker.
        ctx->setFieldAttributes("TestComponent", "iconAsset",
            insp::Field{}
                .name("Icon")
                .help("Icon image shown in UI")
                .asset(".vfimage")
                .build());

        ctx->logInfo("PluginAPITest initialized - TestComponent registered via meta");

        // ================================================================
        // Custom render pipeline test: vertex-colored triangle (toggle F8)
        // ================================================================
        if (ctx->hasCapability(std::string(plugin::capability::graphics)))
        {
            initCustomPipelineTest();
        }

        return true;
    }

    void initCustomPipelineTest()
    {
        plugin::CustomPipelineDesc desc;
        desc.glslSource = R"(#type VERTEX
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(push_constant) uniform PC { mat4 mvp; } pc;
layout(location = 0) out vec4 fragColor;
void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
}

#type FRAGMENT
#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = fragColor;
}
)";
        desc.vertexLayout = {plugin::CustomVertexAttribute::Float3, plugin::CustomVertexAttribute::Float4};
        desc.cullMode = plugin::CustomCullMode::None;

        trianglePipeline = ctx->createCustomPipeline(desc);
        if (!trianglePipeline.isValid())
        {
            ctx->logError("[CustomPipeline] Pipeline creation failed");
            return;
        }

        struct Vertex { glm::vec3 position; glm::vec4 color; };
        const Vertex vertices[] = {
            {{0.0f, 8.0f, 0.0f},  {1.0f, 0.0f, 0.0f, 1.0f}},
            {{-4.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {{4.0f, 0.0f, 0.0f},  {0.0f, 0.0f, 1.0f, 1.0f}},
        };

        plugin::CustomMeshData meshData;
        meshData.vertexData.resize(sizeof(vertices));
        std::memcpy(meshData.vertexData.data(), vertices, sizeof(vertices));
        meshData.vertexCount = 3;

        triangleMesh = ctx->uploadCustomMesh(std::move(meshData));
        if (!triangleMesh.isValid())
        {
            ctx->logError("[CustomPipeline] Mesh upload failed");
            return;
        }

        ctx->logInfo("[CustomPipeline] Test triangle ready - press F8 to toggle");

        // ================================================================
        // VK-1359: Plugin texture + world-space mask test (F12 to toggle)
        // ================================================================
        maskTexture = ctx->createTexture2D(MASK_SIZE, MASK_SIZE, plugin::TextureFormat::R8);
        if (maskTexture.isValid())
        {
            maskData.resize(MASK_SIZE * MASK_SIZE);
            ctx->logInfo("[PluginTexture] World mask texture ready - press F12 to toggle");
        }
        else
        {
            ctx->logError("[PluginTexture] Texture creation failed");
        }
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

            // F8: Toggle custom pipeline test triangle
            if (ctx->isKeyPressed(297))
            {
                showTriangle = !showTriangle;
                ctx->logInfo(std::string("[CustomPipeline] Triangle ") + (showTriangle ? "shown" : "hidden"));
            }

            // F12: Toggle world-space mask test (VK-1359)
            if (ctx->isKeyPressed(301) && maskTexture.isValid())
            {
                worldMaskActive = !worldMaskActive;
                if (worldMaskActive && !worldMaskBound)
                {
                    plugin::WorldMaskParams params;
                    params.affectsTerrain = true;
                    params.terrainDimMin = 0.25f;
                    params.affectsEntities = true;
                    params.entityDiscardBelow = 0.5f;
                    ctx->bindWorldMask(maskTexture, glm::vec3(-256.0f, 0.0f, -256.0f),
                                       glm::vec3(256.0f, 0.0f, 256.0f), params);
                    worldMaskBound = true;
                }
                else
                {
                    // Runtime flag flip only — must be hitch-free (no pipeline recreate)
                    plugin::WorldMaskParams params;
                    params.enabled = worldMaskActive;
                    params.affectsTerrain = true;
                    params.terrainDimMin = 0.25f;
                    params.affectsEntities = true;
                    params.entityDiscardBelow = 0.5f;
                    ctx->setWorldMaskParams(params);
                }
                ctx->logInfo(std::string("[PluginTexture] World mask ") + (worldMaskActive ? "ON" : "OFF"));
            }
        }

        // Animated mask: expanding/contracting visible circle, updated every frame
        if (worldMaskActive && maskTexture.isValid())
        {
            maskTime += deltaTime;
            const float center = MASK_SIZE * 0.5f;
            const float radius = (0.5f + 0.45f * std::sin(maskTime * 0.6f)) * center;
            const float radiusSq = radius * radius;
            for (uint32_t z = 0; z < MASK_SIZE; ++z)
            {
                const float dz = static_cast<float>(z) - center;
                for (uint32_t x = 0; x < MASK_SIZE; ++x)
                {
                    const float dx = static_cast<float>(x) - center;
                    maskData[z * MASK_SIZE + x] = (dx * dx + dz * dz) < radiusSq
                        ? std::byte{0xFF} : std::byte{0x00};
                }
            }
            ctx->updateTexture2D(maskTexture, maskData.data(), maskData.size());
        }

        if (showTriangle && trianglePipeline.isValid() && triangleMesh.isValid())
        {
            ctx->drawCustomMesh(trianglePipeline, triangleMesh, glm::mat4(1.0f));
        }
    }

    void onShutdown() override
    {
        ctx->logInfo("PluginAPITest shutdown");
    }

private:
    plugin::PluginContext* ctx = nullptr;
    plugin::CustomPipelineHandle trianglePipeline;
    plugin::CustomMeshHandle triangleMesh;
    bool showTriangle = false;

    // VK-1359 world mask test
    static constexpr uint32_t MASK_SIZE = 256;
    plugin::PluginTextureHandle maskTexture;
    std::vector<std::byte> maskData;
    bool worldMaskActive = false;
    bool worldMaskBound = false;
    float maskTime = 0.0f;
};

VF_IMPLEMENT_PLUGIN(PluginAPITest)
