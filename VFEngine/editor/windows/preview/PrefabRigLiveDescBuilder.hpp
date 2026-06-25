#pragma once

// VK-1433 Phase 4 — live entity-tree -> PrefabRigDescDTO conversion.
//
// This is the entity-driven counterpart of buildPrefabRigDescDTO (PrefabRigDescBuilder.hpp), which
// builds the same DTO from a parsed .vfPrefab JSON tree. Phase 4 reframes the Prefab Rig Preview
// window onto a live, isolated sandbox subtree (a LoadPrefab'd entity hierarchy tagged with
// PreviewSandboxTagComponent): the window no longer parses the prefab JSON itself, it re-derives
// the rig description from the sandbox entities after every structural edit.
//
// The mapping reproduces buildPrefabRigDescDTO field-for-field so the two builders agree:
//   * DFS pre-order over the subtree; the k-th MESH-BEARING node is part k (Phases 1-3 rely on it).
//   * a ROOT part carries its accumulated local-to-sandbox-root transform (so the preview matches
//     the scene viewport); a socket-attached child carries rotation/scale only (the child rides the
//     socket origin, matching SocketAttachmentUpdater::applyModelOffset).
//   * name resolution is subtree-scoped: nameToPart is built only from the visited mesh-bearing
//     nodes, so a per-window sandbox never resolves an attachment against an entity outside it (Q2).
//
// Like PrefabRigDescBuilder.hpp this is header-only so the Tests project can exercise it CPU-only
// (it registers fake scene/material/socket/IK CQRS handlers); unlike that builder it reads the live
// registry through the service-layer CQRS (EventDispatcher), so the window stays entt-free.
//
// IK: each mesh-bearing visible node's IKTargetComponent chains (read via GetIKChainConfigsQuery)
// become desc.ik entries owned by that node's part — exactly the binding buildPrefabRigDescDTO
// reconstructed from IKTargetComponent.chains in the old JSON spine. The assembly builds its IK
// chains ONLY from desc.ik (PrefabRigAssembly.cpp:198-220), so populating it here is what keeps the
// IK panel (and Save IK Chains) alive after the Phase-4 reframe.

#include "data/PrefabRigDescDTO.hpp"
#include "data/EntityHandle.hpp"
#include "PrefabRigResolveCore.hpp" // shared ResolvedRigNode + resolveRigFromNodes (part/socket/IK core)
#include "math/TransformUtils.hpp" // math::composeMatrix (XYZ euler deg == TransformComponent)
#include "animator/IKTypes.hpp"    // animator::ik::IKChainConfig
#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"     // GetEntityQuery + EntityData
#include "events/scene/ComponentMediaEvents.hpp"      // GetMeshDataQuery + MeshData
#include "events/render/MaterialEvents.hpp"           // GetMaterialDataQuery + MaterialData
#include "events/physics/SocketEvents.hpp"            // GetSocketAttachmentDataQuery
#include "events/physics/IKEvents.hpp"                // GetIKChainConfigsQuery

#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace windows
{
    // The re-derived rig description plus the parallel source entity of each part. partEntities[k]
    // is the sandbox entity that became desc.parts[k] (so the window can map a selected entity to a
    // part and back).
    struct LiveRigBuildResult
    {
        services::PrefabRigDescDTO desc;
        std::vector<services::EntityHandle> partEntities; // parallel to desc.parts
    };

    namespace detail
    {
        // Resolve every rig-relevant component on ONE sandbox node into a ResolvedRigNode (the shared
        // resolver input). B5: this collapses what were four separate per-mesh-node re-walks (mesh /
        // material / socket / IK) into a single visit during the DFS — still per-node CQRS, but each
        // node is now touched once instead of being re-queried in three later loops. (A single batched
        // subtree fetch would need new aggregate-query plumbing through the service layer; out of scope
        // here — see PrefabRigLiveDescBuilder note.) `data` is the already-fetched GetEntityQuery result
        // for `entity`, so this issues no extra GetEntityQuery.
        inline void resolveLiveNode(services::EntityHandle entity,
                                    const services::EntityData& data,
                                    const glm::mat4& world, ResolvedRigNode& node)
        {
            node.name = data.name;
            node.world = world;
            node.localRotationEuler = data.localTransform.rotation; // Euler degrees
            node.localScale = data.localTransform.scale;
            node.sourceEntity = entity;

            // Mesh / animator / retarget (a non-empty meshPath marks the node as a part).
            {
                events::scene::GetMeshDataQuery mq;
                mq.entity = entity;
                if (auto mesh = events::EventDispatcher::instance().query(mq); mesh.has_value())
                {
                    if (mesh->meshRef.isValid())
                        node.meshPath = mesh->meshRef.resolve();
                    if (mesh->animatorRef.isValid())
                        node.animatorPath = mesh->animatorRef.resolve();
                    if (mesh->retargetRef.isValid())
                        node.retargetPath = mesh->retargetRef.resolve();
                }
            }
            if (node.meshPath.empty()) return; // non-part: skip material / socket / IK resolution

            // Material refs (read by Layer B only). resolve() to the same path-shape the JSON builder
            // stores; an absent/invalid ref leaves the path empty.
            {
                events::material::GetMaterialDataQuery mq;
                mq.entity = entity;
                if (auto mat = events::EventDispatcher::instance().query(mq); mat.has_value())
                {
                    if (mat->defaultMaterialRef.isValid())
                        node.defaultMaterialPath = mat->defaultMaterialRef.resolve();
                    for (const auto& [submeshName, ref] : mat->subMeshMaterials)
                    {
                        if (ref.isValid())
                        {
                            const std::string p = ref.resolve();
                            if (!p.empty()) node.subMeshMaterials[submeshName] = p;
                        }
                    }
                }
            }

            // SocketAttachment parent link (resolved to a part by name in the shared core).
            {
                events::socket::GetSocketAttachmentDataQuery sq;
                sq.entity = entity;
                if (auto attach = events::EventDispatcher::instance().query(sq); attach.has_value())
                {
                    node.hasSocketAttachment = true;
                    node.attachParentEntityName = attach->parentEntityName;
                    node.attachSocketName = attach->socketName;
                }
            }

            // IK chains owned by this node (this node = the IK body part).
            {
                events::ik::GetIKChainConfigsQuery iq;
                iq.entity = entity;
                node.ikChains = events::EventDispatcher::instance().query(iq);
            }
        }

        // Depth-first flatten that accumulates each node's local-to-root world matrix and resolves its
        // rig components in the SAME visit, emitting ResolvedRigNodes in DFS pre-order (the ordering the
        // shared resolver relies on). Authored visibility follows NameComponent isActive, except the
        // sandbox root is treated as active because its inactive flag is a preview-isolation artifact.
        inline void flattenLiveNodes(services::EntityHandle entity, const glm::mat4& parentWorld,
                                     services::EntityHandle sandboxRoot, std::vector<ResolvedRigNode>& out)
        {
            if (!entity.isValid()) return;

            events::scene::GetEntityQuery q;
            q.entity = entity;
            auto data = events::EventDispatcher::instance().query(q);
            if (!data.has_value()) return;
            if (entity != sandboxRoot && !data->isActive) return;

            const glm::mat4 world = parentWorld
                * math::composeMatrix(data->localTransform.position,
                                      data->localTransform.rotation,
                                      data->localTransform.scale);

            ResolvedRigNode node;
            resolveLiveNode(entity, *data, world, node);
            out.push_back(std::move(node));

            for (const services::EntityHandle& child : data->children)
                flattenLiveNodes(child, world, sandboxRoot, out);
        }
    }

    // Re-derive the rig description from a live, isolated sandbox subtree. Non-root inactive nodes
    // are pruned from the DTO, matching the authored Active state used by the canvas builder. The
    // sandbox root itself is treated as active because MarkPreviewSandboxCommand holds it inactive
    // only to keep the preview subtree out of the main scene passes. The part/socket/IK resolution
    // is the shared resolveRigFromNodes core (identical to buildPrefabRigDescDTO).
    inline LiveRigBuildResult buildPrefabRigDescFromEntity(services::EntityHandle sandboxRoot)
    {
        LiveRigBuildResult result;
        if (!sandboxRoot.isValid()) return result;

        std::vector<ResolvedRigNode> nodes;
        detail::flattenLiveNodes(sandboxRoot, glm::mat4(1.0f), sandboxRoot, nodes);

        ResolvedRig resolved = resolveRigFromNodes(nodes);
        result.desc = std::move(resolved.desc);
        result.partEntities = std::move(resolved.partEntities);
        return result;
    }
}
