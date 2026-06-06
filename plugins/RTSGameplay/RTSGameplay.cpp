#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include "components/CoreComponents.hpp"
#include <entt/entt.hpp>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

// RTS gameplay tag components (VK-1302). Registered as plugin components so the
// engine core stays game-agnostic — scripts access them via PluginComponent.mt
// (has/getInt/add/remove/findAll) and designers via the Add Component popup.

// Marks an entity as selectable by the RTS selection system.
struct SelectableComponent
{
    bool canBeSelected = true;
};

// Runtime-only selection marker managed by the selection controller during play.
// Never authored in scenes (scenes are saved in edit mode, where it is absent).
struct SelectedComponent
{
    bool active = true;
};

// Faction ownership: 0 = Player, 1 = Enemy, 2 = Neutral.
struct TeamComponent
{
    int teamId = 0;
};

// Fog-of-war vision source (VK-1314): the entity reveals a circle of sightRadius
// world units around itself for its team. Author on player units and buildings.
struct VisionComponent
{
    float sightRadius = 24.0f;
};

class RTSGameplay : public plugin::IPlugin
{
public:
    plugin::PluginInfo getInfo() const override
    {
        return {"RTSGameplay", "VertexForge",
                "RTS gameplay: Selectable/Selected/Team/Vision components + fog of war (VK-1302/VK-1314)", 1, 1, 0};
    }

    bool onInitialize(plugin::PluginContext* context) override
    {
        ctx = context;

        ctx->registerNativeComponent<SelectableComponent>("Selectable")
            .data<&SelectableComponent::canBeSelected>("canBeSelected");

        ctx->registerNativeComponent<SelectedComponent>("Selected")
            .data<&SelectedComponent::active>("active");

        ctx->registerNativeComponent<TeamComponent>("Team")
            .data<&TeamComponent::teamId>("teamId");

        ctx->registerNativeComponent<VisionComponent>("Vision")
            .data<&VisionComponent::sightRadius>("sightRadius");

        if (ctx->hasCapability(std::string(plugin::capability::graphics)))
        {
            fogTexture = ctx->createTexture2D(FOG_GRID, FOG_GRID, plugin::TextureFormat::R8);
            if (fogTexture.isValid())
                visibilityGrid.resize(FOG_GRID * FOG_GRID);
            else
                ctx->logError("RTSGameplay: fog-of-war texture creation failed");
        }

        ctx->logInfo("RTSGameplay initialized - Selectable/Selected/Team/Vision components registered");
        return true;
    }

    void onUpdate(float deltaTime) override
    {
        (void)deltaTime;
        if (!fogTexture.isValid()) return;

        // F10: dev toggle — flips the runtime enabled flag only (no pipeline recreate).
        if (ctx->hasCapability(std::string(plugin::capability::input)) && ctx->isKeyPressed(299))
        {
            fogUserEnabled = !fogUserEnabled;
            ctx->logInfo(std::string("RTSGameplay: fog of war ") + (fogUserEnabled ? "ON" : "OFF"));
            if (fogBound)
                ctx->setWorldMaskParams(makeFogParams(fogUserEnabled));
        }

        updateVisibilityGrid();
    }

    void onShutdown() override
    {
        if (fogBound)
        {
            ctx->unbindWorldMask();
            fogBound = false;
        }
        if (fogTexture.isValid())
        {
            ctx->destroyTexture2D(fogTexture);
            fogTexture = {};
        }
        ctx->logInfo("RTSGameplay shutdown");
    }

private:
    // Two-state fog of war (visible / unseen) over the skirmish map bounds.
    // Bounds match the demo's RTSCameraController / BuildingPlacementController.
    static constexpr uint32_t FOG_GRID = 256;          // 2 m per cell over the 512 m map
    static constexpr float MAP_MIN = -256.0f;
    static constexpr float MAP_MAX = 256.0f;

    static plugin::WorldMaskParams makeFogParams(bool enabled)
    {
        plugin::WorldMaskParams params;
        params.enabled = enabled;
        params.affectsTerrain = true;
        params.terrainDimMin = 0.35f;     // unseen terrain keeps 35% albedo
        params.affectsEntities = true;
        params.entityDiscardBelow = 0.5f; // unseen entities are hidden
        return params;
    }

    void updateVisibilityGrid()
    {
        auto& registry = ctx->getRegistry();

        // Player-team visibility only (single bound mask renders the local player's
        // view; per-team grids for AI queries are a later story).
        std::memset(visibilityGrid.data(), 0, visibilityGrid.size());

        constexpr float cellSize = (MAP_MAX - MAP_MIN) / static_cast<float>(FOG_GRID);
        constexpr float invCellSize = 1.0f / cellSize;

        bool anyVisionSource = false;
        auto view = registry.view<VisionComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            // Entities without a Team default to the player team (0).
            if (const auto* team = registry.try_get<TeamComponent>(entity); team && team->teamId != 0)
                continue;

            const auto& vision = view.get<VisionComponent>(entity);
            if (vision.sightRadius <= 0.0f) continue;
            anyVisionSource = true;

            const auto& transform = view.get<components::TransformComponent>(entity);
            stampVisionCircle(transform.position.x, transform.position.z,
                              vision.sightRadius, invCellSize);
        }

        if (!anyVisionSource)
        {
            // No vision sources (e.g. edit mode before authoring) — keep fog inert
            // instead of blacking out the whole map.
            if (fogBound && fogActive)
            {
                ctx->setWorldMaskParams(makeFogParams(false));
                fogActive = false;
            }
            return;
        }

        ctx->updateTexture2D(fogTexture, visibilityGrid.data(), visibilityGrid.size());

        if (!fogBound)
        {
            ctx->bindWorldMask(fogTexture, glm::vec3(MAP_MIN, 0.0f, MAP_MIN),
                               glm::vec3(MAP_MAX, 0.0f, MAP_MAX), makeFogParams(fogUserEnabled));
            fogBound = true;
            fogActive = fogUserEnabled;
            ctx->logInfo("RTSGameplay: fog of war active (F10 to toggle)");
        }
        else if (!fogActive && fogUserEnabled)
        {
            ctx->setWorldMaskParams(makeFogParams(true));
            fogActive = true;
        }
    }

    void stampVisionCircle(float worldX, float worldZ, float radius, float invCellSize)
    {
        const float gridX = (worldX - MAP_MIN) * invCellSize;
        const float gridZ = (worldZ - MAP_MIN) * invCellSize;
        const float gridRadius = radius * invCellSize;
        const float radiusSq = gridRadius * gridRadius;

        const int minX = std::max(0, static_cast<int>(gridX - gridRadius));
        const int maxX = std::min(static_cast<int>(FOG_GRID) - 1, static_cast<int>(gridX + gridRadius) + 1);
        const int minZ = std::max(0, static_cast<int>(gridZ - gridRadius));
        const int maxZ = std::min(static_cast<int>(FOG_GRID) - 1, static_cast<int>(gridZ + gridRadius) + 1);

        for (int z = minZ; z <= maxZ; ++z)
        {
            const float dz = static_cast<float>(z) + 0.5f - gridZ;
            std::byte* row = visibilityGrid.data() + static_cast<size_t>(z) * FOG_GRID;
            for (int x = minX; x <= maxX; ++x)
            {
                const float dx = static_cast<float>(x) + 0.5f - gridX;
                if (dx * dx + dz * dz <= radiusSq)
                    row[x] = std::byte{0xFF};
            }
        }
    }

    plugin::PluginContext* ctx = nullptr;

    plugin::PluginTextureHandle fogTexture;
    std::vector<std::byte> visibilityGrid;
    bool fogBound = false;        // mask bound to the renderer
    bool fogActive = false;       // enabled flag currently set in the params UBO
    bool fogUserEnabled = true;   // F10 dev toggle
};

VF_IMPLEMENT_PLUGIN(RTSGameplay)
