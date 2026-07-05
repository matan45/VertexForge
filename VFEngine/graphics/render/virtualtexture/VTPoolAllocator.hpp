#pragma once

#include "VTTypes.hpp"
#include <cstdint>
#include <vector>

// ============================================================================
// Virtual Texturing (VK-1209) — physical-tile free-list allocator. PURE /
// header-only, copy-adapted from VSMPhysicalTilePool's free-list
// (VSMPhysicalTilePool.cpp:174-200): LIFO with the list seeded in reverse so the
// first allocations hand out the lowest tile indices. Adds an explicit
// allocated-state array so double-free / out-of-range frees are caught (the VSM
// version has no guard) — cheap (1 byte/tile) and unit-testable.
// ============================================================================

namespace render::vt
{
    class VTPoolAllocator
    {
    public:
        void init(uint32_t tileCount)
        {
            capacity = tileCount;
            allocated.assign(tileCount, 0u);
            freeTiles.clear();
            freeTiles.reserve(tileCount);
            for (uint32_t i = tileCount; i-- > 0u;)
                freeTiles.push_back(i); // reversed -> pop_back yields lowest first
        }

        [[nodiscard]] uint32_t allocate()
        {
            if (freeTiles.empty())
                return VT_INVALID_TILE;
            const uint32_t tile = freeTiles.back();
            freeTiles.pop_back();
            allocated[tile] = 1u;
            return tile;
        }

        // Returns false on out-of-range or double free (tile not currently allocated).
        bool free(uint32_t tile)
        {
            if (tile >= capacity || allocated[tile] == 0u)
                return false;
            allocated[tile] = 0u;
            freeTiles.push_back(tile);
            return true;
        }

        void freeAll()
        {
            init(capacity);
        }

        [[nodiscard]] uint32_t freeCount() const { return static_cast<uint32_t>(freeTiles.size()); }
        [[nodiscard]] uint32_t allocatedCount() const { return capacity - static_cast<uint32_t>(freeTiles.size()); }
        [[nodiscard]] uint32_t capacityCount() const { return capacity; }
        [[nodiscard]] bool isAllocated(uint32_t tile) const { return tile < capacity && allocated[tile] != 0u; }

    private:
        std::vector<uint32_t> freeTiles;
        std::vector<uint8_t> allocated;
        uint32_t capacity = 0;
    };
}
