#include "FogOfWar.hpp"
#include "RTSComponents.hpp"
#include "components/CoreComponents.hpp"
#include <entt/entt.hpp>
#include <algorithm>
#include <cstring>

void FogOfWarSystem::initialize(plugin::PluginContext* context, std::shared_ptr<FogSettings> sharedSettings)
{
    ctx = context;
    settings = std::move(sharedSettings);

    if (!ctx->hasCapability(std::string(plugin::capability::graphics)))
        return;

    fogTexture = ctx->createTexture2D(FOG_GRID, FOG_GRID, plugin::TextureFormat::R8);
    if (fogTexture.isValid())
        visibilityGrid.resize(FOG_GRID * FOG_GRID);
    else
        ctx->logError("RTSGameplay: fog-of-war texture creation failed");
}

void FogOfWarSystem::update()
{
    if (!fogTexture.isValid()) return;

    // F10: dev toggle — flips the runtime enabled flag only (no pipeline recreate).
    if (ctx->hasCapability(std::string(plugin::capability::input)) && ctx->isKeyPressed(299))
    {
        settings->enabled = !settings->enabled;
        settings->paramsDirty = true;
        ctx->logInfo(std::string("RTSGameplay: fog of war ") + (settings->enabled ? "ON" : "OFF"));
    }

    updateVisibilityGrid();
}

void FogOfWarSystem::shutdown()
{
    if (fogBound)
    {
        ctx->unbindWorldMask();
        fogBound = false;
        settings->bound = false;
    }
    if (fogTexture.isValid())
    {
        ctx->destroyTexture2D(fogTexture);
        fogTexture = {};
    }
}

plugin::WorldMaskParams FogOfWarSystem::makeFogParams(bool enabled) const
{
    plugin::WorldMaskParams params;
    params.enabled = enabled;
    params.affectsTerrain = true;
    params.terrainDimMin = settings->terrainDimMin;
    params.affectsEntities = true;
    params.entityDiscardBelow = settings->entityDiscardBelow;
    return params;
}

void FogOfWarSystem::updateVisibilityGrid()
{
    const auto start = std::chrono::steady_clock::now();

    auto& registry = ctx->getRegistry();

    // Player-team visibility only (single bound mask renders the local player's
    // view; per-team grids for AI queries are a later story).
    std::memset(visibilityGrid.data(), 0, visibilityGrid.size());

    constexpr float cellSize = (MAP_MAX - MAP_MIN) / static_cast<float>(FOG_GRID);
    constexpr float invCellSize = 1.0f / cellSize;

    int visionSources = 0;
    auto view = registry.view<VisionComponent, components::TransformComponent>();
    for (auto entity : view)
    {
        // Entities without a Team default to the player team (0).
        if (const auto* team = registry.try_get<TeamComponent>(entity); team && team->teamId != 0)
            continue;

        const auto& vision = view.get<VisionComponent>(entity);
        if (vision.sightRadius <= 0.0f) continue;
        ++visionSources;

        const auto& transform = view.get<components::TransformComponent>(entity);
        stampVisionCircle(transform.position.x, transform.position.z,
                          vision.sightRadius, invCellSize);
    }
    settings->visionSources = visionSources;

    if (visionSources == 0)
    {
        // No vision sources (e.g. edit mode before authoring) — keep fog inert
        // instead of blacking out the whole map.
        if (fogBound && fogActive)
        {
            ctx->setWorldMaskParams(makeFogParams(false));
            fogActive = false;
        }
        settings->gridUpdateMs = elapsedMs(start);
        return;
    }

    ctx->updateTexture2D(fogTexture, visibilityGrid.data(), visibilityGrid.size());

    if (!fogBound)
    {
        ctx->bindWorldMask(fogTexture, glm::vec3(MAP_MIN, 0.0f, MAP_MIN),
                           glm::vec3(MAP_MAX, 0.0f, MAP_MAX), makeFogParams(settings->enabled));
        fogBound = true;
        settings->bound = true;
        fogActive = settings->enabled;
        settings->paramsDirty = false;
        ctx->logInfo("RTSGameplay: fog of war active (F10 / Plugins > Fog of War)");
    }
    else if (settings->paramsDirty || fogActive != settings->enabled)
    {
        ctx->setWorldMaskParams(makeFogParams(settings->enabled));
        fogActive = settings->enabled;
        settings->paramsDirty = false;
    }

    settings->gridUpdateMs = elapsedMs(start);
}

void FogOfWarSystem::stampVisionCircle(float worldX, float worldZ, float radius, float invCellSize)
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
