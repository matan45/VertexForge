#include "TerrainService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainSurfaceMaskAsset.hpp"
#include "../../data/EntityConversion.hpp"

// VK-1614 world-anchored wetness/snow mask — service-side lifecycle.
//
// The service owns the paintable master copy; the graphics side keeps its own GPU image and pulls
// through ITerrainRenderProvider. Two dirty flags rather than one because "a mask was assigned" costs
// an image recreate + descriptor rewrite + pipeline recreate, while "the pixels changed" costs one
// buffer-to-image copy — and the second happens at paint-stroke rates.

namespace services
{
    namespace
    {
        // The AUTHORED rect is snapshotted from the terrain's CURRENT bounds exactly once, here, and
        // never recomputed. Deriving it live would be the natural-looking mistake and a bad one:
        // TileCoord is signed and the tile map is sparse and mutable at runtime, so one tile added at
        // a negative coord would re-normalise every mask UV and slide the whole painted mask across
        // the terrain. Tiles that later appear outside the rect simply sample 0 and fall back to
        // global-only weather.
        bool snapshotTerrainWorldRect(uint64_t terrainEntityId, glm::vec4& outRect)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            const entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
            if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
                return false;

            const auto& comp = registry.get<components::TerrainComponent>(ent);
            outRect = glm::vec4(
                static_cast<float>(comp.gridMinX) * comp.worldTileSize,
                static_cast<float>(comp.gridMinZ) * comp.worldTileSize,
                static_cast<float>(comp.gridMaxX + 1) * comp.worldTileSize,
                static_cast<float>(comp.gridMaxZ + 1) * comp.worldTileSize);
            return (outRect.z - outRect.x) > 1e-4f && (outRect.w - outRect.y) > 1e-4f;
        }

        void writeComponentMaskFields(uint64_t terrainEntityId, const std::string& path,
                                      const glm::vec4& rect, uint32_t resolution)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            const entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
            if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
                return;

            auto& comp = registry.get<components::TerrainComponent>(ent);
            comp.surfaceMaskPath = path;
            comp.surfaceMaskWorldRect = rect;
            comp.surfaceMaskResolution = resolution;
            comp.saveDirty = true;
        }
    }

    bool TerrainService::createSurfaceMask(uint64_t terrainEntityId, uint32_t resolution)
    {
        glm::vec4 rect(0.0f);
        if (!snapshotTerrainWorldRect(terrainEntityId, rect))
        {
            vfLogError("TerrainService: cannot create a surface mask — entity {} has no sized terrain",
                       terrainEntityId);
            return false;
        }

        auto mask = terrain::TerrainSurfaceMaskAsset::createEmpty(resolution);
        if (!mask || !mask->isValid())
            return false;

        // VK-1615: an open mask stroke's snapshot addresses the OLD image, so it cannot be
        // restored into the new one.
        discardTerrainStroke();

        surfaceMask = std::move(mask);
        surfaceMaskWorldRect = rect;
        surfaceMaskOwner = terrainEntityId;
        surfaceMaskPath.clear(); // unsaved until the user picks a path
        surfaceMaskAssignDirty.store(true, std::memory_order_release);
        surfaceMaskPixelsDirty.store(true, std::memory_order_release);

        writeComponentMaskFields(terrainEntityId, surfaceMaskPath, surfaceMaskWorldRect,
                                 surfaceMask->width);

        vfLogInfo("TerrainService: created a {}x{} surface mask over [{}, {}]..[{}, {}]",
                  surfaceMask->width, surfaceMask->height,
                  rect.x, rect.y, rect.z, rect.w);
        return true;
    }

    bool TerrainService::loadSurfaceMask(uint64_t terrainEntityId, const std::string& path)
    {
        auto mask = terrain::TerrainSurfaceMaskAsset::load(path);
        if (!mask || !mask->isValid())
            return false;

        // The rect comes from the COMPONENT, not from a fresh snapshot — it was authored when the
        // mask was created and travels with the scene. Only fall back to a snapshot when the scene
        // carries no rect at all (a mask assigned by hand to a terrain that never had one).
        auto& registry = scene::EntityRegistry::getRegistry();
        const entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
        glm::vec4 rect(0.0f);
        bool haveRect = false;
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            const auto& comp = registry.get<components::TerrainComponent>(ent);
            rect = comp.surfaceMaskWorldRect;
            haveRect = (rect.z - rect.x) > 1e-4f && (rect.w - rect.y) > 1e-4f;
        }
        if (!haveRect && !snapshotTerrainWorldRect(terrainEntityId, rect))
        {
            vfLogError("TerrainService: surface mask {} has no world rect and the terrain has no bounds",
                       path);
            return false;
        }

        // VK-1615: see createSurfaceMask -- the pending snapshot belongs to the old image.
        discardTerrainStroke();

        surfaceMask = std::move(mask);
        surfaceMaskWorldRect = rect;
        surfaceMaskOwner = terrainEntityId;
        surfaceMaskPath = path;
        surfaceMaskAssignDirty.store(true, std::memory_order_release);
        surfaceMaskPixelsDirty.store(true, std::memory_order_release);

        writeComponentMaskFields(terrainEntityId, surfaceMaskPath, surfaceMaskWorldRect,
                                 surfaceMask->width);
        return true;
    }

    bool TerrainService::saveSurfaceMask(const std::string& path)
    {
        if (!surfaceMask || !surfaceMask->isValid())
        {
            vfLogWarning("TerrainService: no surface mask to save");
            return false;
        }

        if (!terrain::TerrainSurfaceMaskAsset::save(path, *surfaceMask))
            return false;

        surfaceMaskPath = path;
        // Persist the path + rect together: the `.vfImage` header has no room for the rect, so the
        // scene is the only place the pair stays consistent.
        writeComponentMaskFields(surfaceMaskOwner, surfaceMaskPath, surfaceMaskWorldRect,
                                 surfaceMask->width);
        return true;
    }

    void TerrainService::clearSurfaceMask()
    {
        if (!surfaceMask)
            return;

        // VK-1615: the image is going away, so an open mask stroke has nothing to snapshot
        // against.
        discardTerrainStroke();

        const uint64_t owner = surfaceMaskOwner;
        surfaceMask.reset();
        surfaceMaskWorldRect = glm::vec4(0.0f);
        surfaceMaskPath.clear();
        surfaceMaskOwner = 0;
        // Assign-dirty, not pixels-dirty: graphics must release the image and drop the macro, and a
        // pixel upload with no image would be dropped anyway.
        surfaceMaskAssignDirty.store(true, std::memory_order_release);
        surfaceMaskPixelsDirty.store(false, std::memory_order_release);

        writeComponentMaskFields(owner, std::string{}, glm::vec4(0.0f),
                                 terrain::SURFACE_MASK_DEFAULT_RESOLUTION);
    }
}
