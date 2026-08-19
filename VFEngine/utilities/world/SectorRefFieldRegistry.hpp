#pragma once

#include "WorldExport.hpp"
#include "PendingReferenceResolver.hpp"
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>

namespace world
{
    // VK-1590. Explicit registration table of every component field that stores a cross-sector
    // entity reference. Deliberately NOT reflection (none exists in this engine): the set is
    // tiny, hand-auditable, and each row owns both directions — read the target UUID out, and
    // write the resolved handle back in.
    //
    // Exactly ONE row exists today: SocketAttachmentComponent::parentEntityUUID.
    // ReferenceType::Parent and ::IKTarget are reserved tags with no row — see the enum in
    // PendingReferenceResolver.hpp for why neither is representable. ScriptComponent public
    // entity fields and entity-to-entity physics constraints are not features of this engine
    // (ScriptEntry has no field storage; there is no Constraint/Joint component), so they have
    // no row either.
    //
    // To add a carrier: append ONE row to the table in the .cpp. Nothing else changes.
    struct SectorRefField
    {
        ReferenceType type;

        // Target UUID this entity references through this field; 0 == no reference.
        uint64_t (*collect)(entt::registry&, entt::entity source);

        // Append every (source, target) pair in the whole registry. Typed view, per row.
        void (*collectAll)(entt::registry&, std::vector<PendingReference>& out);

        // Write the resolved handle into the source component. False if the component is gone.
        bool (*apply)(entt::registry&, entt::entity source, entt::entity target);
    };

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_WORLD_API SectorRefFieldRegistry
    {
    public:
        // Enumerate one entity's outgoing references. Skips absent fields and self-references.
        static void collectReferences(entt::registry& registry, entt::entity source,
                                      uint64_t sourceUUID, std::vector<PendingReference>& out);

        // Enumerate every reference currently held anywhere in the registry.
        static void collectAllReferences(entt::registry& registry,
                                         std::vector<PendingReference>& out);

        // Resolve both endpoints by UUID and write the handle. False if either endpoint is
        // missing, or if the type has no table row.
        static bool applyReference(entt::registry& registry, const PendingReference& ref);

        // Policy helpers, so the world-sector service and the unit tests drive IDENTICAL code.
        //
        // Registers every reference held by `source` — even ones whose target is ALREADY live,
        // which is what lets the reference survive a later unload/reload of the target's
        // sector. Targets that are already resident are appended to outLiveTargets so the
        // caller can batch a single resolver probe per frame instead of one per entity.
        static void registerEntityReferences(entt::registry& registry, entt::entity source,
                                             uint64_t sourceUUID,
                                             PendingReferenceResolver& resolver,
                                             std::vector<uint64_t>& outLiveTargets);

        // Drains resolver.consumeNewlyResolved(), applies each, and prunes entries whose source
        // entity has since been destroyed. Returns the number actually applied.
        // MAIN THREAD ONLY — it mutates components and the EntityRegistry UUID maps.
        static size_t applyResolvedReferences(entt::registry& registry,
                                              PendingReferenceResolver& resolver);

        [[nodiscard]] static size_t fieldCount();
    };
#pragma warning(pop)

} // namespace world
