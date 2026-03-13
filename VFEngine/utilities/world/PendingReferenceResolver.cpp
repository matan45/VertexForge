#include "PendingReferenceResolver.hpp"
#include <algorithm>
#include <unordered_set>

namespace world
{
    void PendingReferenceResolver::addPendingReference(uint64_t sourceUUID, uint64_t targetUUID, ReferenceType type)
    {
        pendingRefs.push_back({sourceUUID, targetUUID, type});
    }

    void PendingReferenceResolver::onSectorLoaded(const std::vector<uint64_t>& loadedUUIDs)
    {
        std::unordered_set<uint64_t> loadedSet(loadedUUIDs.begin(), loadedUUIDs.end());

        auto it = pendingRefs.begin();
        while (it != pendingRefs.end())
        {
            if (loadedSet.contains(it->targetUUID))
            {
                resolvedRefs.push_back(*it);
                it = pendingRefs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void PendingReferenceResolver::onSectorUnloaded(const std::vector<uint64_t>& unloadedUUIDs)
    {
        std::unordered_set<uint64_t> unloadedSet(unloadedUUIDs.begin(), unloadedUUIDs.end());

        auto it = resolvedRefs.begin();
        while (it != resolvedRefs.end())
        {
            if (unloadedSet.contains(it->targetUUID))
            {
                pendingRefs.push_back(*it);
                it = resolvedRefs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

} // namespace world
