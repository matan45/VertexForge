#include "FoliagePhysicsActivator.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainTile.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include <algorithm>

namespace services
{
    bool FoliagePhysicsActivator::isActiveMode() const
    {
        // Editor gates colliders to Play mode. Runtime has no editor-mode service, so the query has
        // no handler and throws -> the standalone game is always "running", so treat that as active.
        try
        {
            return events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{});
        }
        catch (...)
        {
            return true;
        }
    }

    void FoliagePhysicsActivator::gatherWatchers(const glm::vec3& cameraPosition)
    {
        watcherScratch.clear();
        watcherScratch.push_back(cameraPosition); // the player's viewpoint

        // AI crowd agents are ECS entities; NavmeshAgentManager keeps their TransformComponent
        // position live each frame, so reading it here is cheap and up to date.
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TransformComponent, components::NavmeshAgentComponent>();
        for (auto entity : view)
            watcherScratch.push_back(view.get<components::TransformComponent>(entity).position);
    }

    FoliagePhysicsActivator::TypeColliderInfo
    FoliagePhysicsActivator::resolveTypeInfo(uint16_t typeIndex, const foliage::FoliageType& type)
    {
        auto it = typeInfoCache.find(typeIndex);
        if (it != typeInfoCache.end())
            return it->second;

        // Fallback: a ~0.6 m diameter, ~2 m tall trunk, used until the mesh AABB is available.
        TypeColliderInfo info;
        info.aabbCenter = glm::vec3(0.0f, 1.0f, 0.0f);
        info.aabbHalfExtents = glm::vec3(0.3f, 1.0f, 0.3f);

        try
        {
            events::render::GetMeshBoundingBoxQuery q;
            q.meshPath = type.meshPath;
            auto bbox = events::EventDispatcher::instance().query(q);
            if (bbox.has_value())
            {
                info.aabbCenter = (bbox->min + bbox->max) * 0.5f;
                info.aabbHalfExtents = glm::max((bbox->max - bbox->min) * 0.5f, glm::vec3(0.01f));
                typeInfoCache[typeIndex] = info; // cache only successful resolves
            }
        }
        catch (...)
        {
            // No render service / mesh not loaded yet — use the transient default and retry later.
        }
        return info;
    }

    IPhysicsProvider::FoliageColliderDesc
    FoliagePhysicsActivator::buildDesc(const foliage::FoliageColliderCandidate& cand,
                                       const foliage::FoliageType& type)
    {
        IPhysicsProvider::FoliageColliderDesc desc;
        desc.meshPath = type.meshPath;
        desc.position = cand.position;
        desc.rotationY = cand.rotationY;
        desc.scale = cand.scale;
        desc.collisionLayer = 0; // static/world layer (matches the vegetation collider path)

        switch (type.colliderShape)
        {
        case foliage::FoliageColliderShape::ConvexHull:
            desc.shape = types::ColliderShape::ConvexMesh; // hull built + cached from meshPath
            break;
        case foliage::FoliageColliderShape::Box:
        {
            desc.shape = types::ColliderShape::Box;
            const TypeColliderInfo ti = resolveTypeInfo(cand.typeIndex, type);
            desc.localAabbCenter = ti.aabbCenter;
            desc.localAabbHalfExtents = ti.aabbHalfExtents;
            break;
        }
        case foliage::FoliageColliderShape::Capsule:
        default:
        {
            desc.shape = types::ColliderShape::Capsule;
            const TypeColliderInfo ti = resolveTypeInfo(cand.typeIndex, type);
            desc.localAabbCenter = ti.aabbCenter;
            desc.localAabbHalfExtents = ti.aabbHalfExtents;
            break;
        }
        }
        return desc;
    }

    void FoliagePhysicsActivator::update(const glm::vec3& cameraPosition,
                                         const std::vector<terrain::TerrainTile*>& loadedTiles,
                                         const std::vector<foliage::FoliageType>& palette)
    {
        if (!physicsProvider) return;

        // Not simulating (edit mode) -> tear down any bodies and bail.
        if (!isActiveMode())
        {
            if (!activeBodies.empty()) clearAll();
            return;
        }

        // Fast path for the common case (grass-only scenes): if no type opts into collision, there
        // is nothing to activate, so skip the per-instance scan of nearby tiles entirely.
        const bool anyCollision =
            std::any_of(palette.begin(), palette.end(),
                        [](const foliage::FoliageType& t) { return t.collision; });
        if (!anyCollision)
        {
            if (!activeBodies.empty()) clearAll();
            return;
        }

        gatherWatchers(cameraPosition);
        const float radiusSq = config.activationRadius * config.activationRadius;

        // Collect collision candidates from tiles near a watcher (point-vs-AABB XZ reject first).
        candidateScratch.clear();
        for (terrain::TerrainTile* tile : loadedTiles)
        {
            if (!tile || !tile->hasFoliageInstances()) continue;

            const glm::vec3 bMin = tile->worldBounds.min;
            const glm::vec3 bMax = tile->worldBounds.max;
            bool tileNear = false;
            for (const glm::vec3& w : watcherScratch)
            {
                const float cx = std::clamp(w.x, bMin.x, bMax.x);
                const float cz = std::clamp(w.z, bMin.z, bMax.z);
                const float dx = w.x - cx;
                const float dz = w.z - cz;
                if (dx * dx + dz * dz <= radiusSq) { tileNear = true; break; }
            }
            if (!tileNear) continue;

            for (const foliage::FoliageInstance& inst : tile->foliageInstances)
            {
                if (inst.typeIndex >= palette.size()) continue;
                if (!palette[inst.typeIndex].collision) continue; // live per-type opt-in
                foliage::FoliageColliderCandidate c;
                c.key = {tile->coord.x, tile->coord.z, inst.seed};
                c.position = inst.position;
                c.rotationY = inst.rotationY;
                c.scale = inst.scale;
                c.typeIndex = inst.typeIndex;
                candidateScratch.push_back(c);
            }
        }

        foliage::computeColliderActivationDelta(watcherScratch, radiusSq, candidateScratch,
                                                activeBodies, toCreateScratch, toDestroyScratch);

        // Destroy first (unbounded — cheap, frees memory before we create new bodies).
        for (const foliage::FoliageColliderKey& key : toDestroyScratch)
        {
            auto it = activeBodies.find(key);
            if (it != activeBodies.end())
            {
                physicsProvider->destroyFoliageStaticBody(it->second);
                activeBodies.erase(it);
            }
        }

        // Create up to the per-frame budget; the remainder arrive over subsequent ticks (they stay
        // in the in-radius set, so they are re-offered next frame until the budget catches up).
        int budget = config.maxCreationsPerFrame;
        for (const foliage::FoliageColliderCandidate& cand : toCreateScratch)
        {
            if (budget <= 0) break;
            const foliage::FoliageType& type = palette[cand.typeIndex];
            const uint32_t handle = physicsProvider->createFoliageStaticBody(buildDesc(cand, type));
            if (handle != IPhysicsProvider::INVALID_BODY_HANDLE)
            {
                activeBodies[cand.key] = handle;
                --budget;
            }
        }
    }

    void FoliagePhysicsActivator::clearAll()
    {
        if (physicsProvider)
        {
            for (const auto& [key, handle] : activeBodies)
                physicsProvider->destroyFoliageStaticBody(handle);
        }
        activeBodies.clear();
    }

    void FoliagePhysicsActivator::onPaletteChanged()
    {
        // Destroy active bodies (they were built from possibly-stale dims) then drop the per-type
        // AABB cache so the next resolveTypeInfo re-queries the current mesh bounds.
        clearAll();
        typeInfoCache.clear();
    }
}
