#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// VK-1501 -- pure CPU-side policy for the GPU event->child fast path.
//
// This header intentionally has no dependency on Vulkan or the graphics module, so the
// renderer and the CPU-only doctests share the same bit layout and region bookkeeping and
// cannot drift. The GLSL side (vfx_particle_sim.glsl / vfx_gpu_types.glsl) mirrors the same
// constants and unpack logic by hand -- keep the two in lockstep.
//
// Model: a "fast-path" OnDeath / OnCollision event on a parent emitter routes its child
// spawns GPU-side into a per-child region of the process-wide childSpawnBuffer (binding 11),
// bypassing the CPU event readback. Each distinct child .vfVFX (by asset path) owns one region,
// shared (refcounted) across all parents that reference it.
namespace vfx::child
{
    // --- Capacities (single source of truth for the C++ side; the GLSL shader mirrors these) ---
    inline constexpr uint32_t CHILD_MAX_REGIONS = 64;             // distinct GPU event->child channels
    inline constexpr uint32_t CHILD_MAX_REQUESTS_PER_REGION = 256; // requests / region / frame / ping-pong half
    inline constexpr uint32_t CHILD_REGION_NONE = 0xFFu;         // 8-bit region sentinel = "no fast-path child"

    static_assert(CHILD_MAX_REGIONS <= CHILD_REGION_NONE,
                  "region index must fit in 8 bits with 0xFF reserved as the sentinel");

    // --- eventChildSlot bit layout (per 16-bit half; low half = OnDeath, high half = OnCollision) ---
    inline constexpr uint32_t REGION_MASK        = 0xFFu;   // bits 0-7
    inline constexpr uint32_t INHERIT_COLOR_BIT  = 1u << 8; // apply impact color as a tint
    inline constexpr uint32_t INHERIT_SIZE_BIT   = 1u << 9; // scale child spawn by impact size
    inline constexpr uint32_t INHERIT_VEL_BIT    = 1u << 10; // override child spawn direction with impact velocity
    inline constexpr uint32_t HALF_MASK          = 0xFFFFu;
    inline constexpr uint32_t COLLISION_HALF_SHIFT = 16u;

    inline constexpr uint32_t PACKED_NONE = 0xFFFFFFFFu; // both halves region=0xFF (no fast-path child)

    // Pack one event type's routing into a 16-bit half.
    inline constexpr uint32_t packHalf(uint32_t region, bool inheritColor, bool inheritSize, bool inheritVelocity)
    {
        uint32_t h = (region & REGION_MASK);
        if (inheritColor)    h |= INHERIT_COLOR_BIT;
        if (inheritSize)     h |= INHERIT_SIZE_BIT;
        if (inheritVelocity) h |= INHERIT_VEL_BIT;
        return h & HALF_MASK;
    }

    // region = 0xFF (none); the upper bits are set too so packSlot(noneHalf, noneHalf) == PACKED_NONE.
    inline constexpr uint32_t noneHalf() { return HALF_MASK; }

    // Combine the OnDeath and OnCollision halves into the packed eventChildSlot uint.
    inline constexpr uint32_t packSlot(uint32_t deathHalf, uint32_t collisionHalf)
    {
        return (deathHalf & HALF_MASK) | ((collisionHalf & HALF_MASK) << COLLISION_HALF_SHIFT);
    }

    // --- Unpack accessors (deathHalf = index 0, collisionHalf = index 1) ---
    inline constexpr uint32_t halfOf(uint32_t slot, bool collision)
    {
        return collision ? ((slot >> COLLISION_HALF_SHIFT) & HALF_MASK) : (slot & HALF_MASK);
    }
    inline constexpr uint32_t regionOf(uint32_t half) { return half & REGION_MASK; }
    inline constexpr bool     hasRegion(uint32_t half) { return regionOf(half) != CHILD_REGION_NONE; }
    inline constexpr bool     inheritColorOf(uint32_t half) { return (half & INHERIT_COLOR_BIT) != 0u; }
    inline constexpr bool     inheritSizeOf(uint32_t half) { return (half & INHERIT_SIZE_BIT) != 0u; }
    inline constexpr bool     inheritVelOf(uint32_t half) { return (half & INHERIT_VEL_BIT) != 0u; }

    // Refcounted region allocator, keyed by child .vfVFX asset path. Many parents pointing at the
    // same child path share one region. acquire() returns nullopt when all regions are in use
    // (caller then falls back to the CPU readback path for that event). Pure/deterministic, so the
    // CPU-only tests can exercise it directly.
    class VFXChildRegionAllocator
    {
    public:
        // Returns the region for `path` (allocating on first use, incrementing its refcount on
        // reuse). nullopt when CHILD_MAX_REGIONS are already in use and `path` is new.
        std::optional<uint32_t> acquire(const std::string& path)
        {
            auto it = byPath.find(path);
            if (it != byPath.end())
            {
                ++it->second.refCount;
                return it->second.region;
            }

            uint32_t region;
            if (!freeList.empty())
            {
                region = freeList.back();
                freeList.pop_back();
            }
            else if (nextRegion < CHILD_MAX_REGIONS)
            {
                region = nextRegion++;
            }
            else
            {
                return std::nullopt; // exhausted
            }

            byPath.emplace(path, Entry{region, 1u});
            return region;
        }

        // Drop one reference to `path`; frees the region when the last reference is released.
        void release(const std::string& path)
        {
            auto it = byPath.find(path);
            if (it == byPath.end())
                return;
            if (--it->second.refCount == 0u)
            {
                freeList.push_back(it->second.region);
                byPath.erase(it);
            }
        }

        std::optional<uint32_t> regionOfPath(const std::string& path) const
        {
            auto it = byPath.find(path);
            if (it == byPath.end())
                return std::nullopt;
            return it->second.region;
        }

        uint32_t activeRegions() const { return static_cast<uint32_t>(byPath.size()); }

        void reset()
        {
            byPath.clear();
            freeList.clear();
            nextRegion = 0;
        }

    private:
        struct Entry
        {
            uint32_t region = 0;
            uint32_t refCount = 0;
        };
        std::unordered_map<std::string, Entry> byPath;
        std::vector<uint32_t> freeList;
        uint32_t nextRegion = 0;
    };
}
