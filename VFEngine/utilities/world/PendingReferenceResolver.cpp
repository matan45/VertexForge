#include "PendingReferenceResolver.hpp"
#include <algorithm>
#include <unordered_set>
#include <utility>

namespace world
{
    namespace
    {
        bool sameReference(const PendingReference& a, uint64_t sourceUUID, uint64_t targetUUID,
                           ReferenceType type)
        {
            return a.sourceUUID == sourceUUID && a.targetUUID == targetUUID && a.type == type;
        }

        bool containsReference(const std::vector<PendingReference>& refs, uint64_t sourceUUID,
                               uint64_t targetUUID, ReferenceType type)
        {
            return std::any_of(refs.begin(), refs.end(), [&](const PendingReference& ref)
            {
                return sameReference(ref, sourceUUID, targetUUID, type);
            });
        }
    }

    void PendingReferenceResolver::addPendingReference(uint64_t sourceUUID, uint64_t targetUUID, ReferenceType type)
    {
        // VK-1590: linear is the right shape here. Both vectors hold only live cross-entity
        // references (tens, not thousands), and a hash index would not survive the erase-based
        // promotion loops below.
        if (containsReference(pendingRefs, sourceUUID, targetUUID, type) ||
            containsReference(resolvedRefs, sourceUUID, targetUUID, type))
        {
            return;
        }

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
                // VK-1590: mirror the promotion so the service applies it exactly once.
                newlyResolved.push_back(*it);
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

    void PendingReferenceResolver::removeReferencesFrom(const std::vector<uint64_t>& sourceUUIDs)
    {
        if (sourceUUIDs.empty())
            return;

        std::unordered_set<uint64_t> deadSet(sourceUUIDs.begin(), sourceUUIDs.end());
        const auto isDead = [&](const PendingReference& ref)
        {
            return deadSet.contains(ref.sourceUUID);
        };

        std::erase_if(pendingRefs, isDead);
        std::erase_if(resolvedRefs, isDead);
        // newlyResolved is deliberately untouched: it is drained and pruned within the same
        // frame by the service, which is also what calls this.
    }

    std::vector<PendingReference> PendingReferenceResolver::consumeNewlyResolved()
    {
        return std::exchange(newlyResolved, {});
    }

} // namespace world
