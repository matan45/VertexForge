#include "SectorRefFieldRegistry.hpp"
#include "../components/Components.hpp"
#include "../scene/EntityRegistry.hpp"

namespace world
{
    namespace
    {
        using Att = components::SocketAttachmentComponent;

        uint64_t socketCollect(entt::registry& registry, entt::entity source)
        {
            const auto* attachment = registry.try_get<Att>(source);
            return attachment ? attachment->parentEntityUUID : 0;
        }

        void socketCollectAll(entt::registry& registry, std::vector<PendingReference>& out)
        {
            auto view = registry.view<Att, components::UUIDComponent>();
            for (auto entity : view)
            {
                const auto& attachment = view.get<Att>(entity);
                const uint64_t sourceUUID = view.get<components::UUIDComponent>(entity).id.getValue();
                if (attachment.parentEntityUUID != 0 && attachment.parentEntityUUID != sourceUUID)
                {
                    out.push_back({sourceUUID, attachment.parentEntityUUID,
                                   ReferenceType::SocketAttachment});
                }
            }
        }

        bool socketApply(entt::registry& registry, entt::entity source, entt::entity target)
        {
            auto* attachment = registry.try_get<Att>(source);
            if (!attachment)
                return false;

            attachment->parentEntity = target;
            attachment->cachedSocketIndex = -1;              // re-resolve the socket index
            attachment->parentKind = Att::ParentKind::Unknown; // drop the VK-1432 static-offset cache
            attachment->needsParentResolution = false;       // the handle is authoritative now

            // Fill, never overwrite: the editor's part-rename path owns parentEntityName.
            if (attachment->parentEntityName.empty())
            {
                if (const auto* name = registry.try_get<components::NameComponent>(target))
                {
                    attachment->parentEntityName = name->name;
                }
            }
            return true;
        }

        constexpr SectorRefField kFields[] = {
            {ReferenceType::SocketAttachment, socketCollect, socketCollectAll, socketApply},
        };

        constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

        const SectorRefField* findField(ReferenceType type)
        {
            for (const auto& field : kFields)
            {
                if (field.type == type)
                    return &field;
            }
            return nullptr;
        }
    }

    void SectorRefFieldRegistry::collectReferences(entt::registry& registry, entt::entity source,
                                                   uint64_t sourceUUID,
                                                   std::vector<PendingReference>& out)
    {
        if (source == entt::null || !registry.valid(source) || sourceUUID == 0)
            return;

        for (const auto& field : kFields)
        {
            const uint64_t targetUUID = field.collect(registry, source);
            // A self-reference would resolve trivially and then apply a socket parent of
            // itself — reject it at the source rather than at every consumer.
            if (targetUUID != 0 && targetUUID != sourceUUID)
            {
                out.push_back({sourceUUID, targetUUID, field.type});
            }
        }
    }

    void SectorRefFieldRegistry::collectAllReferences(entt::registry& registry,
                                                      std::vector<PendingReference>& out)
    {
        for (const auto& field : kFields)
        {
            field.collectAll(registry, out);
        }
    }

    bool SectorRefFieldRegistry::applyReference(entt::registry& registry, const PendingReference& ref)
    {
        const SectorRefField* field = findField(ref.type);
        if (!field)
            return false; // reserved type with no row (Parent / IKTarget)

        const auto source = scene::EntityRegistry::findByUUID(ref.sourceUUID);
        const auto target = scene::EntityRegistry::findByUUID(ref.targetUUID);
        if (source == entt::null || target == entt::null ||
            !registry.valid(source) || !registry.valid(target))
        {
            return false;
        }

        return field->apply(registry, source, target);
    }

    void SectorRefFieldRegistry::registerEntityReferences(entt::registry& registry,
                                                         entt::entity source, uint64_t sourceUUID,
                                                         PendingReferenceResolver& resolver,
                                                         std::vector<uint64_t>& outLiveTargets)
    {
        std::vector<PendingReference> refs;
        collectReferences(registry, source, sourceUUID, refs);

        for (const auto& ref : refs)
        {
            resolver.addPendingReference(ref.sourceUUID, ref.targetUUID, ref.type);

            // Already resident? Queue the target so the caller's batched probe promotes it.
            // We still registered it above — that is what makes the reference re-heal if the
            // target's sector later unloads and streams back in.
            if (scene::EntityRegistry::findByUUID(ref.targetUUID) != entt::null)
            {
                outLiveTargets.push_back(ref.targetUUID);
            }
        }
    }

    size_t SectorRefFieldRegistry::applyResolvedReferences(entt::registry& registry,
                                                           PendingReferenceResolver& resolver)
    {
        const auto batch = resolver.consumeNewlyResolved();
        if (batch.empty())
            return 0;

        std::vector<uint64_t> deadSources;
        size_t applied = 0;
        for (const auto& ref : batch)
        {
            if (applyReference(registry, ref))
            {
                ++applied;
                continue;
            }

            // Prune ONLY when the source itself is gone (e.g. destroyed by gameplay rather
            // than by a sector unload, which prunes on its own). If the source is alive the
            // failure means the target vanished — leave the ledger entry be, so the normal
            // unload/reload demote-and-promote cycle re-applies it.
            const auto source = scene::EntityRegistry::findByUUID(ref.sourceUUID);
            if (source == entt::null || !registry.valid(source))
            {
                deadSources.push_back(ref.sourceUUID);
            }
        }

        if (!deadSources.empty())
        {
            resolver.removeReferencesFrom(deadSources);
        }
        return applied;
    }

    size_t SectorRefFieldRegistry::fieldCount()
    {
        return kFieldCount;
    }

} // namespace world
