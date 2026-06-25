#pragma once

// VK-1433 — shared part/socket/IK resolution core for the two prefab-rig DTO builders.
//
// buildPrefabRigDescDTO (PrefabRigDescBuilder.hpp, JSON-tree input, Tests-only parity oracle) and
// buildPrefabRigDescFromEntity (PrefabRigLiveDescBuilder.hpp, live-sandbox CQRS input, the sole
// production builder) used to duplicate the same field-for-field resolution logic:
//   * DFS world accumulation (parentWorld * composeMatrix(local)) into a flat node list;
//   * "the k-th MESH-BEARING node (DFS pre-order) is part k", with name->part ("last node wins on a
//     duplicate name") for parent resolution;
//   * SocketAttachment parent-link + child rotation/scale (translation intentionally dropped — the
//     child rides the socket origin, matching SocketAttachmentUpdater::applyModelOffset);
//   * default IK target = the first STATIC part (empty animatorPath).
//
// That shared core lives here. Each front-end now only adapts its INPUT source (JSON tree vs live
// entity subtree via CQRS) into a flat std::vector<ResolvedRigNode> and calls resolveRigFromNodes().
// The DFS itself stays in each front-end because the traversal differs (in-memory child vectors vs
// CQRS child queries) — but the node ORDER it must produce is identical (DFS pre-order), which is the
// only ordering invariant the resolver relies on.
//
// Header-only (like the two builders) so the Tests project can exercise it CPU-only.

#include "data/PrefabRigDescDTO.hpp"
#include "data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace windows
{
    // One flattened prefab node, already in DFS pre-order with its accumulated local-to-root world.
    // The front-ends fill this from their own source; the resolver consumes only these fields so the
    // two builders produce byte-identical PrefabRigDescDTOs.
    struct ResolvedRigNode
    {
        std::string name;
        glm::mat4 world{1.0f}; // accumulated local-to-root transform (root parts apply it; sockets ignore)

        // Source LOCAL rotation/scale of this node (TransformComponent convention: Euler degrees).
        // A socket-attached child carries these onto the part (translation is intentionally dropped).
        glm::vec3 localRotationEuler{0.0f};
        glm::vec3 localScale{1.0f};

        // Resolved asset paths (empty == absent). A non-empty meshPath marks the node as a part.
        std::string meshPath;
        std::string animatorPath;       // empty => static part
        std::string retargetPath;
        std::string defaultMaterialPath;
        std::map<std::string, std::string> subMeshMaterials;

        // SocketAttachmentComponent (parentEntityName resolved to a part by name later).
        bool hasSocketAttachment = false;
        std::string attachParentEntityName;
        std::string attachSocketName;

        // IKTargetComponent chains owned by this node (this node = the IK body part).
        std::vector<animator::ik::IKChainConfig> ikChains;

        // Live-builder only: the sandbox entity this node came from (parallel to desc.parts when this
        // node becomes a part). Left invalid by the JSON front-end, which discards the parallel vector.
        services::EntityHandle sourceEntity = services::EntityHandle::invalid();

        bool hasMesh() const { return !meshPath.empty(); }
    };

    struct ResolvedRig
    {
        services::PrefabRigDescDTO desc;
        std::vector<services::EntityHandle> partEntities; // parallel to desc.parts (live builder only)
    };

    // Shared resolution core. `nodes` MUST be in DFS pre-order so the k-th mesh-bearing node maps to
    // part k (Phases 1-3 rely on this ordering). Produces exactly what both builders produced inline.
    inline ResolvedRig resolveRigFromNodes(const std::vector<ResolvedRigNode>& nodes)
    {
        ResolvedRig result;
        services::PrefabRigDescDTO& desc = result.desc;

        // Mesh-bearing node -> part index; name -> part index for parent resolution (subtree-scoped).
        std::vector<int> nodePartIndex(nodes.size(), -1);
        std::map<std::string, int> nameToPart;

        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const ResolvedRigNode& n = nodes[i];
            if (!n.hasMesh()) continue;

            services::PrefabRigPartDTO part;
            part.meshPath = n.meshPath;
            part.animatorPath = n.animatorPath;
            part.retargetPath = n.retargetPath;
            part.defaultMaterialPath = n.defaultMaterialPath;
            part.subMeshMaterials = n.subMeshMaterials;
            part.localTransform = n.world; // authored TRS (root parts apply it; sockets ignore)

            const int partIndex = static_cast<int>(desc.parts.size());
            nodePartIndex[i] = partIndex;
            nameToPart[n.name] = partIndex; // last node wins on duplicate names
            desc.parts.push_back(std::move(part));
            result.partEntities.push_back(n.sourceEntity);
        }

        // Resolve socket-attachment parent links + child transforms.
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const int partIndex = nodePartIndex[i];
            if (partIndex < 0) continue;

            const ResolvedRigNode& n = nodes[i];
            if (!n.hasSocketAttachment) continue;

            auto it = nameToPart.find(n.attachParentEntityName);
            if (it == nameToPart.end()) continue; // parent has no (visible) mesh part -> leave as a root

            services::PrefabRigPartDTO& part = desc.parts[partIndex];
            part.parentPartIndex = it->second;
            part.attachParentSocket = n.attachSocketName;
            part.attachChildRotation = n.localRotationEuler; // Euler degrees, as TransformComponent.rotation
            part.attachChildScale = n.localScale;
        }

        // IK chains. Each owning node's part is the body part; default the target binding to the first
        // STATIC part (a weapon/prop typically holds the grip socket the hand IK-s to). The window
        // lets the user override both the part and the socket.
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const int bodyPart = nodePartIndex[i];
            if (bodyPart < 0) continue;

            for (const animator::ik::IKChainConfig& chainCfg : nodes[i].ikChains)
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
