#pragma once
// VK-1584 — pure, Vulkan-free proximity-activation logic for foliage colliders.
// Kept header-only and dependency-light (glm + std only) so the enter/leave "set-difference"
// (which instances gain a collider, which lose it) is exercisable by the doctest Tests target
// with no physics/graphics dependencies. The impure side (services FoliagePhysicsActivator)
// gathers the watched positions + collision candidates and drives IPhysicsProvider
// create/destroy from the sets this header computes.
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <unordered_set>

namespace foliage
{
    // Stable identity for one instance's collider across frames. The packed store swap-and-pops
    // on erase (TerrainServiceHandlers RemoveFoliageInstancesFromTileCommand) and reloads on
    // stream-in, so the instance INDEX is not stable between ticks; position is static, so
    // (tile coord, per-instance seed) is a stable key. A seed collision within a single tile is
    // astronomically unlikely and, at worst, co-activates two colliders — never a crash.
    struct FoliageColliderKey
    {
        int32_t  tileX = 0;
        int32_t  tileZ = 0;
        uint32_t seed  = 0;

        bool operator==(const FoliageColliderKey& o) const
        {
            return tileX == o.tileX && tileZ == o.tileZ && seed == o.seed;
        }
    };

    struct FoliageColliderKeyHash
    {
        std::size_t operator()(const FoliageColliderKey& k) const
        {
            // FNV-1a mix of the three 32-bit fields.
            std::uint64_t h = 1469598103934665603ull;
            auto mix = [&h](std::uint32_t v) { h = (h ^ v) * 1099511628211ull; };
            mix(static_cast<std::uint32_t>(k.tileX));
            mix(static_cast<std::uint32_t>(k.tileZ));
            mix(k.seed);
            return static_cast<std::size_t>(h);
        }
    };

    using FoliageColliderKeySet = std::unordered_set<FoliageColliderKey, FoliageColliderKeyHash>;

    // A collision-enabled instance considered for activation this tick. position + key drive the
    // set-difference; rotationY/scale/typeIndex are payload the impure activator reads when it
    // builds the body (foliage colliders are upright — align-to-normal is intentionally ignored).
    struct FoliageColliderCandidate
    {
        FoliageColliderKey key;
        glm::vec3          position{0.0f};
        float              rotationY = 0.0f;
        glm::vec3          scale{1.0f};
        uint16_t           typeIndex = 0;
    };

    // Horizontal (XZ) proximity test against the nearest watched point. XZ — not full 3D — mirrors
    // the foliage tile-cull convention (foliageTileInRange) and keeps activation robust when a
    // watcher (camera) sits well above the terrain: colliders track the player's ground footprint,
    // and vertical separation only ever over-activates within the radius (harmless, bounded).
    inline bool withinAnyWatcherXZ(const glm::vec3& pos,
                                   const std::vector<glm::vec3>& watchers,
                                   float radiusSq)
    {
        for (const auto& w : watchers)
        {
            const float dx = pos.x - w.x;
            const float dz = pos.z - w.z;
            if (dx * dx + dz * dz <= radiusSq) return true;
        }
        return false;
    }

    // Pure enter/leave set-difference. Given the watched points, squared activation radius, the
    // collision candidates gathered from nearby tiles, and the map of currently-active colliders
    // (FoliageColliderKey -> body handle, i.e. the activator's own store), compute:
    //   outToCreate  = in-radius candidates NOT already active
    //   outToDestroy = active keys with NO in-radius candidate this tick
    //                  (covers "left the radius" AND "tile streamed out / instance erased")
    // Deterministic and side-effect free. Templated on the active map so the activator can pass its
    // real unordered_map<FoliageColliderKey, uint32_t> with zero copying; tests pass the same shape.
    template <typename ActiveMap>
    inline void computeColliderActivationDelta(
        const std::vector<glm::vec3>& watchers,
        float radiusSq,
        const std::vector<FoliageColliderCandidate>& candidates,
        const ActiveMap& active,
        std::vector<FoliageColliderCandidate>& outToCreate,
        std::vector<FoliageColliderKey>& outToDestroy)
    {
        outToCreate.clear();
        outToDestroy.clear();

        FoliageColliderKeySet inRadiusKeys;
        inRadiusKeys.reserve(candidates.size());
        for (const auto& c : candidates)
        {
            if (!withinAnyWatcherXZ(c.position, watchers, radiusSq)) continue;
            inRadiusKeys.insert(c.key);
            if (active.find(c.key) == active.end())
                outToCreate.push_back(c);
        }

        for (const auto& kv : active)
        {
            if (inRadiusKeys.find(kv.first) == inRadiusKeys.end())
                outToDestroy.push_back(kv.first);
        }
    }
}
