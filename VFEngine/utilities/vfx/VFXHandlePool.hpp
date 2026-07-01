#pragma once

// VK-1453 (VFXSequence Phase 4) — dormant VFX instance reuse bookkeeping.
//
// Tracks finished-but-retained ("dormant") fire-and-forget VFX instances so a later
// spawn of the SAME asset path can revive an existing slot instead of allocating
// fresh (skips the .vfVFX JSON re-parse and GPU emitter-slot churn). Pure bookkeeping:
// the renderer owns the real instances and GPU slots; this only decides which id to
// reuse and which to evict. Header-only / CPU-testable.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace vfx
{
    class VFXHandlePool
    {
    public:
        using Id = uint32_t;

        explicit VFXHandlePool(size_t maxDormant = 64) : capacity(maxDormant == 0 ? 1 : maxDormant) {}

        // Retain a finished instance for later reuse of `path`. If the pool is full
        // the oldest dormant instance is evicted and its id returned (0 if none) so
        // the caller can truly destroy it.
        Id retain(Id id, const std::string& path)
        {
            Id evicted = 0;
            if (order.size() >= capacity && !order.empty())
            {
                evicted = order.front();
                order.pop_front();
                removeFromBucket(evicted);
            }
            byPath[path].push_back(id);
            pathOf[id] = path;
            order.push_back(id);
            return evicted;
        }

        // Revive a dormant instance for `path`, or 0 if none is available.
        Id acquire(const std::string& path)
        {
            auto it = byPath.find(path);
            if (it == byPath.end() || it->second.empty())
                return 0;
            const Id id = it->second.back();
            it->second.pop_back();
            if (it->second.empty())
                byPath.erase(it);
            pathOf.erase(id);
            eraseFromOrder(id);
            return id;
        }

        // Drop a specific dormant instance without reviving it (e.g. its asset was
        // re-saved). The caller is responsible for destroying the underlying slot.
        void forget(Id id)
        {
            removeFromBucket(id);
            eraseFromOrder(id);
        }

        size_t dormantCount() const { return order.size(); }
        size_t maxDormant() const { return capacity; }
        void clear()
        {
            byPath.clear();
            pathOf.clear();
            order.clear();
        }

    private:
        void removeFromBucket(Id id)
        {
            auto pit = pathOf.find(id);
            if (pit == pathOf.end())
                return;
            auto bit = byPath.find(pit->second);
            if (bit != byPath.end())
            {
                auto& v = bit->second;
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
                if (v.empty())
                    byPath.erase(bit);
            }
            pathOf.erase(pit);
        }

        void eraseFromOrder(Id id)
        {
            for (auto it = order.begin(); it != order.end(); ++it)
            {
                if (*it == id)
                {
                    order.erase(it);
                    return;
                }
            }
        }

        size_t capacity;
        std::unordered_map<std::string, std::vector<Id>> byPath;
        std::unordered_map<Id, std::string> pathOf;
        std::deque<Id> order; // insertion order; oldest at front for eviction
    };
}
