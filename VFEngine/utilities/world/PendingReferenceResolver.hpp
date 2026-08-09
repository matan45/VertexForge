#pragma once

#include "WorldExport.hpp"
#include <cstdint>
#include <vector>

namespace world
{
    // VK-1590: only SocketAttachment has a backing field today. Parent and IKTarget are
    // reserved tags with no SectorRefFieldRegistry row:
    //   Parent   — scene hierarchy is JSON nesting under "children"; ParentComponent is never
    //              serialized and SectorEntityLoader re-parents every streamed entity to the
    //              scene root, so a cross-sector parent is structurally inexpressible.
    //   IKTarget — IKTargetComponent holds only bone-name-scoped IKChainConfig; there is no
    //              entity target to resolve.
    enum class ReferenceType : uint8_t
    {
        Parent,
        SocketAttachment,
        IKTarget
    };

    struct PendingReference
    {
        uint64_t sourceUUID;
        uint64_t targetUUID;
        ReferenceType type;
    };

#pragma warning(push)
#pragma warning(disable: 4251)
    // Tracks entity references whose target is not (yet) resident, and heals them when the
    // target's sector streams in. Deliberately free of the event dispatcher and the service
    // layer: the World DLL owns the bookkeeping, the world-sector service drives it from the
    // main thread (VK-1590 AC #2).
    class VF_WORLD_API PendingReferenceResolver
    {
    private:
        std::vector<PendingReference> pendingRefs;
        // Durable ledger. Survives drains so onSectorUnloaded can demote entries back to
        // pending when their target goes away.
        std::vector<PendingReference> resolvedRefs;
        // VK-1590: one-shot side channel mirroring every pending->resolved promotion, so
        // production applies each resolution exactly once while resolvedRefs stays intact.
        std::vector<PendingReference> newlyResolved;
    public:
        // No-op if the exact (source, target, type) triple is already pending or resolved.
        // Idempotence is what lets a source entity re-register on every respawn.
        void addPendingReference(uint64_t sourceUUID, uint64_t targetUUID, ReferenceType type);

        void onSectorLoaded(const std::vector<uint64_t>& loadedUUIDs);

        // Demotes resolved references whose TARGET just went away. Note the documented
        // asymmetry (VK-1590): nothing nulls the already-written handle on the source
        // component. That is safe only because entt versions its entity ids, so the stale
        // handle fails registry.valid() rather than aliasing a recycled slot.
        void onSectorUnloaded(const std::vector<uint64_t>& unloadedUUIDs);

        // Drops every reference whose SOURCE is in the list, from both queues. Without this
        // the ledger leaks entries for destroyed entities and the dedup above would suppress
        // re-registration when that source respawns.
        void removeReferencesFrom(const std::vector<uint64_t>& sourceUUIDs);

        // Moves the queue out. By value, not by const& — applying a reference may re-register
        // during iteration, which would invalidate a returned reference.
        [[nodiscard]] std::vector<PendingReference> consumeNewlyResolved();

        [[nodiscard]] const std::vector<PendingReference>& getResolved() const { return resolvedRefs; }
        void clearResolved() { resolvedRefs.clear(); }

        void clear()
        {
            pendingRefs.clear();
            resolvedRefs.clear();
            newlyResolved.clear();
        }

        [[nodiscard]] size_t pendingCount() const { return pendingRefs.size(); }
        [[nodiscard]] size_t resolvedCount() const { return resolvedRefs.size(); }
        [[nodiscard]] size_t newlyResolvedCount() const { return newlyResolved.size(); }
    };
#pragma warning(pop)

} // namespace world
