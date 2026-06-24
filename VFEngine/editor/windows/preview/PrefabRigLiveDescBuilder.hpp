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
#include <unordered_set>
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
        // One flattened sandbox node: the entity, its name, and its accumulated local-to-root
        // transform (parentWorld * composeMatrix(local)). Parallel arrays are intentional so the
        // mapping reads exactly like buildPrefabRigDescDTO's flattenNodesWithWorld output.
        struct LiveNode
        {
            services::EntityHandle entity;
            std::string name;
            glm::mat4 world{1.0f};
            services::TransformData localTransform;
        };

        // Depth-first flatten that accumulates each node's local-to-root world matrix, parallel to
        // buildPrefabRigDescDTO's flattenNodesWithWorld. EDITOR-ONLY hide: a node is pruned (skipped
        // with its whole subtree) iff it is in `hidden` — visibility keys off the SET, never off
        // entityData.isActive (so the sandbox root's isolation-inactive flag is irrelevant here, and
        // hiding never persists). Recurses children in order so the DFS pre-order index is stable.
        inline void flattenLiveNodesWithWorld(services::EntityHandle entity, const glm::mat4& parentWorld,
                                              const std::unordered_set<std::uint64_t>& hidden,
                                              std::vector<LiveNode>& out)
        {
            if (!entity.isValid()) return;
            if (hidden.count(entity.id) > 0) return; // pruned: skip this node and its descendants

            events::scene::GetEntityQuery q;
            q.entity = entity;
            auto data = events::EventDispatcher::instance().query(q);
            if (!data.has_value()) return;

            const glm::mat4 world = parentWorld
                * math::composeMatrix(data->localTransform.position,
                                      data->localTransform.rotation,
                                      data->localTransform.scale);

            LiveNode node;
            node.entity = entity;
            node.name = data->name;
            node.world = world;
            node.localTransform = data->localTransform;
            out.push_back(std::move(node));

            for (const services::EntityHandle& child : data->children)
                flattenLiveNodesWithWorld(child, world, hidden, out);
        }

        // Resolved asset paths of a mesh-bearing node (mesh / animator / retarget). Returns empty
        // strings for an absent component or unresolvable ref. A non-empty meshPath marks a part.
        struct LiveMeshPaths
        {
            std::string meshPath;
            std::string animatorPath;
            std::string retargetPath;
            bool hasMesh = false;
        };

        inline LiveMeshPaths meshPathsOf(services::EntityHandle entity)
        {
            LiveMeshPaths paths;
            events::scene::GetMeshDataQuery q;
            q.entity = entity;
            auto mesh = events::EventDispatcher::instance().query(q);
            if (!mesh.has_value()) return paths;

            if (mesh->meshRef.isValid())
            {
                paths.meshPath = mesh->meshRef.resolve();
                paths.hasMesh = !paths.meshPath.empty();
            }
            if (mesh->animatorRef.isValid())
                paths.animatorPath = mesh->animatorRef.resolve();
            if (mesh->retargetRef.isValid())
                paths.retargetPath = mesh->retargetRef.resolve();
            return paths;
        }
    }

    // Re-derive the rig description from a live, isolated sandbox subtree. `hiddenEntities` is the
    // window-side EDITOR-ONLY hide set: a node in the set (or any descendant of it) is pruned from
    // the DTO, leaving its isActive flag untouched (hiding is non-persistent — a hidden entity stays
    // active in the real game). Re-derive on every structural edit / hide toggle.
    inline LiveRigBuildResult buildPrefabRigDescFromEntity(
        services::EntityHandle sandboxRoot,
        const std::unordered_set<std::uint64_t>& hiddenEntities = {})
    {
        LiveRigBuildResult result;
        if (!sandboxRoot.isValid()) return result;

        services::PrefabRigDescDTO& desc = result.desc;

        std::vector<detail::LiveNode> nodes;
        detail::flattenLiveNodesWithWorld(sandboxRoot, glm::mat4(1.0f), hiddenEntities, nodes);

        // Mesh-bearing node -> part index; name -> part index for parent resolution (subtree-scoped).
        std::vector<int> nodePartIndex(nodes.size(), -1);
        std::map<std::string, int> nameToPart;

        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const detail::LiveMeshPaths paths = detail::meshPathsOf(nodes[i].entity);
            if (!paths.hasMesh) continue;

            services::PrefabRigPartDTO part;
            part.meshPath = paths.meshPath;
            part.animatorPath = paths.animatorPath;
            part.retargetPath = paths.retargetPath;
            part.localTransform = nodes[i].world; // authored TRS (root parts apply it; sockets ignore)

            // Material refs (read by Layer B only). resolve() to the same path-shape the JSON builder
            // stores; an absent/invalid ref leaves the path empty.
            events::material::GetMaterialDataQuery mq;
            mq.entity = nodes[i].entity;
            if (auto mat = events::EventDispatcher::instance().query(mq); mat.has_value())
            {
                if (mat->defaultMaterialRef.isValid())
                    part.defaultMaterialPath = mat->defaultMaterialRef.resolve();
                for (const auto& [submeshName, ref] : mat->subMeshMaterials)
                {
                    if (ref.isValid())
                    {
                        const std::string p = ref.resolve();
                        if (!p.empty()) part.subMeshMaterials[submeshName] = p;
                    }
                }
            }

            const int partIndex = static_cast<int>(desc.parts.size());
            nodePartIndex[i] = partIndex;
            nameToPart[nodes[i].name] = partIndex; // last node wins on duplicate names
            desc.parts.push_back(std::move(part));
            result.partEntities.push_back(nodes[i].entity);
        }

        // Resolve socket-attachment parent links + child transforms (mirror buildPrefabRigDescDTO).
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const int partIndex = nodePartIndex[i];
            if (partIndex < 0) continue;

            events::socket::GetSocketAttachmentDataQuery sq;
            sq.entity = nodes[i].entity;
            auto attach = events::EventDispatcher::instance().query(sq);
            if (!attach.has_value()) continue;

            auto it = nameToPart.find(attach->parentEntityName);
            if (it == nameToPart.end()) continue; // parent has no (visible) mesh part -> leave as a root

            services::PrefabRigPartDTO& part = desc.parts[partIndex];
            part.parentPartIndex = it->second;
            part.attachParentSocket = attach->socketName;
            part.attachChildRotation = nodes[i].localTransform.rotation; // Euler degrees
            part.attachChildScale = nodes[i].localTransform.scale;
        }

        // IK chains. Each mesh-bearing node's IKTargetComponent chains become ik[] entries owned by
        // that node's part (the body part); default the target binding to the first STATIC part (a
        // weapon/prop typically holds the grip socket the hand IK-s to). Mirrors buildPrefabRigDescDTO
        // exactly — the assembly builds its IK only from desc.ik, so this is what keeps IK alive.
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const int bodyPart = nodePartIndex[i];
            if (bodyPart < 0) continue;

            events::ik::GetIKChainConfigsQuery iq;
            iq.entity = nodes[i].entity;
            const std::vector<animator::ik::IKChainConfig> chains =
                events::EventDispatcher::instance().query(iq);

            for (const animator::ik::IKChainConfig& chainCfg : chains)
            {
                services::PrefabRigIKDTO ik;
                ik.chain = chainCfg;
                ik.bodyPartIndex = bodyPart;
                ik.targetPartIndex = -1;
                ik.targetSocketName.clear();

                for (std::size_t p = 0; p < desc.parts.size(); ++p)
                {
                    if (static_cast<int>(p) == bodyPart) continue;
                    if (desc.parts[p].animatorPath.empty()) // static part
                    {
                        ik.targetPartIndex = static_cast<int>(p);
                        break;
                    }
                }

                desc.ik.push_back(std::move(ik));
            }
        }

        return result;
    }
}
