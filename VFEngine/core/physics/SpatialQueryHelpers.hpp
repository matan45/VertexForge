#pragma once

#include <cstdint>
#include <vector>
#include <unordered_set>

// Pure-logic helpers shared by the physics spatial-query path (overlapSphere /
// overlapBox / overlapCapsule). Kept free of any Jolt include so they can be unit
// tested in the CPU-only Tests project, which does not link Jolt.
namespace core::physics
{
    // Mirrors the LayerMaskFilter collision test: a body on collision layer
    // `layer` participates only if the matching bit is set in `mask`. Layers >= 16
    // are rejected (the engine supports MAX_COLLISION_LAYERS = 16, packed into a
    // uint16_t mask).
    inline bool layerMaskAllows(uint16_t mask, uint32_t layer)
    {
        if (layer >= 16u) return false;
        return (mask & (1u << layer)) != 0u;
    }

    // A single shape-overlap query can report multiple sub-shape hits for the same
    // body (e.g. a compound / mesh shape), each mapping back to the same entity.
    // Collapse those to one entry per entity while preserving first-seen order.
    inline void dedupeEntityIds(std::vector<uint64_t>& ids)
    {
        std::unordered_set<uint64_t> seen;
        seen.reserve(ids.size());
        std::vector<uint64_t> unique;
        unique.reserve(ids.size());
        for (uint64_t id : ids)
        {
            if (seen.insert(id).second)
            {
                unique.push_back(id);
            }
        }
        ids.swap(unique);
    }
}
