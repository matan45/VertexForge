#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace destruction
{
    class FragmentPool
    {
    public:
        explicit FragmentPool(size_t maxPoolSize = 100);

        // Get a pooled entity ID for the given asset key, or ~0ULL if none
        uint64_t acquire(uint64_t assetKey);

        // Return entity to pool. Returns false if pool is full (caller should delete).
        bool release(uint64_t entityId, uint64_t assetKey);

        // Drain all pooled entity IDs for bulk destruction
        std::vector<uint64_t> drainAll();

        size_t size() const;
        void clear();

    private:
        size_t maxPoolSize;
        size_t currentSize = 0;
        std::unordered_map<uint64_t, std::vector<uint64_t>> pooledByAsset;
    };
}
