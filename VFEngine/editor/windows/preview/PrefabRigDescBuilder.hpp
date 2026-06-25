#pragma once

// VK-1433 — header-only prefab-tree -> PrefabRigDescDTO conversion.
//
// Kept header-only (like the content-browser type table) so the Tests project — which includes
// VFEngine/editor + VFEngine/services but does NOT link the Editor — can exercise it CPU-only,
// with no imgui / no JSON / no Graphics. The editor window owns the JSON->PrefabEntityNode parse
// (PrefabPreviewWindow.cpp) and then calls buildPrefabRigDescDTO() here.

#include "data/PrefabRigDescDTO.hpp"
#include "PrefabRigResolveCore.hpp" // shared ResolvedRigNode + resolveRigFromNodes (part/socket/IK core)
#include "animator/IKTypes.hpp"
#include "math/TransformUtils.hpp" // math::composeMatrix (XYZ euler deg == TransformComponent)
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace windows
{
    // One parsed prefab entity node (rig-relevant fields only; the window adds display state).
    struct PrefabEntityNode
    {
        std::string name;
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f}; // Euler degrees (TransformComponent.rotation)
        glm::vec3 scale{1.0f};
        std::vector<std::string> componentTypes;
        std::vector<PrefabEntityNode> children;

        // Asset paths (resolved-path JSON keys: meshRefPath / animatorRefPath / etc.)
        std::string meshPath;
        std::string animatorPath;
        std::string retargetPath;
        std::string defaultMaterialPath;
        std::map<std::string, std::string> subMeshMaterials; // submesh name -> material path
        std::string audioPath;

        // SocketAttachmentComponent (parentEntityName resolved to a part later).
        bool hasSocketAttachment = false;
        std::string attachParentEntityName;
        std::string attachSocketName;

        // IKTargetComponent chains owned by this node (this node = the IK body part).
        std::vector<animator::ik::IKChainConfig> ikChains;

        bool hasMesh() const { return !meshPath.empty(); }
        bool isSkeletal() const { return !animatorPath.empty(); }
    };

    namespace detail
    {
        // Depth-first flatten, recording each node pointer (parents before children).
        inline void flattenNodes(const PrefabEntityNode& node, std::vector<const PrefabEntityNode*>& out)
        {
            out.push_back(&node);
            for (const auto& child : node.children)
                flattenNodes(child, out);
        }

        // Depth-first flatten that ALSO accumulates each node's local-to-prefab-root world matrix
        // (parentWorld * this node's composed TransformComponent), parallel to the node list. Lets
        // a ROOT part carry the prefab's authored rotation/scale/position so the preview matches
        // the scene viewport (which renders each mesh at its accumulated world transform).
        inline void flattenNodesWithWorld(const PrefabEntityNode& node, const glm::mat4& parentWorld,
                                          std::vector<const PrefabEntityNode*>& outNodes,
                                          std::vector<glm::mat4>& outWorlds)
        {
            const glm::mat4 world = parentWorld
                * math::composeMatrix(node.position, node.rotation, node.scale);
            outNodes.push_back(&node);
            outWorlds.push_back(world);
            for (const auto& child : node.children)
                flattenNodesWithWorld(child, world, outNodes, outWorlds);
        }
    }

    // Flattens the parsed prefab tree into a rig description. Each mesh-bearing node becomes a
    // part (skeletal if it has an animator). A node's SocketAttachmentComponent links it to a
    // parent part (parentEntityName -> node name) and supplies attachParentSocket + the child's
    // transform rotation/scale. IKTargetComponent chains become ik[] entries owned by their node's
    // part; the target binding is defaulted (first static part) and stays editor-transient.
    //
    // The part/socket/IK resolution is the shared resolveRigFromNodes core (PrefabRigResolveCore.hpp);
    // this front-end only flattens the JSON tree into the resolver's ResolvedRigNode inputs. The live
    // builder (buildPrefabRigDescFromEntity) feeds the SAME resolver from CQRS, so the two agree.
    inline services::PrefabRigDescDTO buildPrefabRigDescDTO(const PrefabEntityNode& root)
    {
        std::vector<const PrefabEntityNode*> srcNodes;
        std::vector<glm::mat4> nodeWorlds; // each node's accumulated local-to-prefab-root transform
        detail::flattenNodesWithWorld(root, glm::mat4(1.0f), srcNodes, nodeWorlds);

        std::vector<ResolvedRigNode> nodes;
        nodes.reserve(srcNodes.size());
        for (size_t i = 0; i < srcNodes.size(); ++i)
        {
            const PrefabEntityNode& n = *srcNodes[i];
            ResolvedRigNode node;
            node.name = n.name;
            node.world = nodeWorlds[i];
            node.localRotationEuler = n.rotation; // Euler degrees, as TransformComponent.rotation
            node.localScale = n.scale;
            node.meshPath = n.meshPath;
            node.animatorPath = n.animatorPath;
            node.retargetPath = n.retargetPath;
            node.defaultMaterialPath = n.defaultMaterialPath;
            node.subMeshMaterials = n.subMeshMaterials;
            node.hasSocketAttachment = n.hasSocketAttachment;
            node.attachParentEntityName = n.attachParentEntityName;
            node.attachSocketName = n.attachSocketName;
            node.ikChains = n.ikChains;
            // sourceEntity stays invalid: the JSON front-end discards the parallel part-entity vector.
            nodes.push_back(std::move(node));
        }

        return resolveRigFromNodes(nodes).desc;
    }
}
