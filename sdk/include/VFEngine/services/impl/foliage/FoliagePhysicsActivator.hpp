#pragma once
// VK-1584 — proximity-activated foliage colliders (services side). Each tick this gathers the
// watched positions (render camera + AI crowd agents), the collision-enabled instances from loaded
// tiles near those watchers, and uses the pure foliage::computeColliderActivationDelta to CREATE a
// Jolt static body for every instance entering the activation radius and DESTROY those leaving it
// (or whose tile streamed out). ONE shared shape per FoliageType is built down in the physics layer
// (cached by meshPath). Gated to play mode so no bodies exist while editing. Owned + ticked by
// TerrainService, which already has the camera position, loaded tiles, palette, and physics
// provider all in one scope.
#include "providers/physics/IPhysicsProvider.hpp"
#include "foliage/FoliageActivation.hpp"
#include "foliage/FoliageTypes.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace terrain { class TerrainTile; }

namespace services
{
    class FoliagePhysicsActivator
    {
    public:
        struct Config
        {
            float activationRadius     = 40.0f; // metres (XZ) around each watcher
            int   maxCreationsPerFrame = 8;     // throttle body creation to avoid teleport hitches
        };

        void setPhysicsProvider(IPhysicsProvider* provider) { physicsProvider = provider; }
        void setConfig(const Config& c) { config = c; }

        // Per-frame driver. `loadedTiles` = all resident terrain tiles; `palette` = the foliage
        // type palette (collision flag + shape read live per instance).
        void update(const glm::vec3& cameraPosition,
                    const std::vector<terrain::TerrainTile*>& loadedTiles,
                    const std::vector<foliage::FoliageType>& palette);

        // Destroy every active body (leaving play mode / teardown).
        void clearAll();

        [[nodiscard]] std::size_t activeColliderCount() const { return activeBodies.size(); }

    private:
        // Capsule/Box collider dimensions resolved once per type from the mesh-local AABB.
        struct TypeColliderInfo
        {
            glm::vec3 aabbCenter{0.0f};
            glm::vec3 aabbHalfExtents{0.3f};
        };

        [[nodiscard]] bool isActiveMode() const;
        void gatherWatchers(const glm::vec3& cameraPosition);
        [[nodiscard]] TypeColliderInfo resolveTypeInfo(uint16_t typeIndex, const foliage::FoliageType& type);
        [[nodiscard]] IPhysicsProvider::FoliageColliderDesc buildDesc(
            const foliage::FoliageColliderCandidate& cand, const foliage::FoliageType& type);

        IPhysicsProvider* physicsProvider = nullptr;
        Config config;

        std::unordered_map<foliage::FoliageColliderKey, uint32_t, foliage::FoliageColliderKeyHash> activeBodies;
        std::unordered_map<uint16_t, TypeColliderInfo> typeInfoCache; // resolved AABB dims per type

        // Frame scratch reused to avoid per-tick allocation.
        std::vector<glm::vec3>                        watcherScratch;
        std::vector<foliage::FoliageColliderCandidate> candidateScratch;
        std::vector<foliage::FoliageColliderCandidate> toCreateScratch;
        std::vector<foliage::FoliageColliderKey>       toDestroyScratch;
    };
}
