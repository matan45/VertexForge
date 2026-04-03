#include "FragmentPool.hpp"

namespace destruction
{
    FragmentPool::FragmentPool(size_t maxPoolSize)
        : maxPoolSize(maxPoolSize)
    {
    }

    uint64_t FragmentPool::acquire(uint64_t assetKey)
    {
        auto it = pooledByAsset.find(assetKey);
        if (it == pooledByAsset.end() || it->second.empty())
        {
            return ~0ULL;
        }

        uint64_t entityId = it->second.back();
        it->second.pop_back();
        --currentSize;

        if (it->second.empty())
        {
            pooledByAsset.erase(it);
        }

        return entityId;
    }

    bool FragmentPool::release(uint64_t entityId, uint64_t assetKey)
    {
        if (currentSize >= maxPoolSize)
        {
            return false;
        }

        pooledByAsset[assetKey].push_back(entityId);
        ++currentSize;
        return true;
    }

    std::vector<uint64_t> FragmentPool::drainAll()
    {
        std::vector<uint64_t> all;
        all.reserve(currentSize);

        for (auto& [key, entities] : pooledByAsset)
        {
            all.insert(all.end(), entities.begin(), entities.end());
        }

        pooledByAsset.clear();
        currentSize = 0;
        return all;
    }

    size_t FragmentPool::size() const
    {
        return currentSize;
    }

    void FragmentPool::clear()
    {
        pooledByAsset.clear();
        currentSize = 0;
    }
}
