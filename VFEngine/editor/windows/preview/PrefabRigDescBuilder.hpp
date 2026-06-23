#pragma once

// VK-1433 — header-only prefab-tree -> PrefabRigDescDTO conversion.
//
// Kept header-only (like the content-browser type table) so the Tests project — which includes
// VFEngine/editor + VFEngine/services but does NOT link the Editor — can exercise it CPU-only,
// with no imgui / no JSON / no Graphics. The editor window owns the JSON->PrefabEntityNode parse
// (PrefabPreviewWindow.cpp) and then calls buildPrefabRigDescDTO() here.

#include "data/PrefabRigDescDTO.hpp"
#include "animator/IKTypes.hpp"
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
    }

    // Flattens the parsed prefab tree into a rig description. Each mesh-bearing node becomes a
    // part (skeletal if it has an animator). A node's SocketAttachmentComponent links it to a
    // parent part (parentEntityName -> node name) and supplies attachParentSocket + the child's
    // transform rotation/scale. IKTargetComponent chains become ik[] entries owned by their node's
    // part; the target binding is defaulted (first static part) and stays editor-transient.
    inline services::PrefabRigDescDTO buildPrefabRigDescDTO(const PrefabEntityNode& root)
    {
        services::PrefabRigDescDTO desc;

        std::vector<const PrefabEntityNode*> nodes;
        detail::flattenNodes(root, nodes);

        // Mesh-bearing node -> part index; name -> part index for parent resolution.
        std::vector<int> nodePartIndex(nodes.size(), -1);
        std::map<std::string, int> nameToPart;

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const PrefabEntityNode& n = *nodes[i];
            if (!n.hasMesh()) continue;

            services::PrefabRigPartDTO part;
            part.meshPath = n.meshPath;
            part.animatorPath = n.animatorPath;
            part.retargetPath = n.retargetPath;
            part.defaultMaterialPath = n.defaultMaterialPath;
            part.subMeshMaterials = n.subMeshMaterials;

            const int partIndex = static_cast<int>(desc.parts.size());
            nodePartIndex[i] = partIndex;
            nameToPart[n.name] = partIndex; // last node wins on duplicate names
            desc.parts.push_back(std::move(part));
        }

        // Resolve socket-attachment parent links + child transforms.
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const int partIndex = nodePartIndex[i];
            if (partIndex < 0) continue;

            const PrefabEntityNode& n = *nodes[i];
            if (!n.hasSocketAttachment) continue;

            auto it = nameToPart.find(n.attachParentEntityName);
            if (it == nameToPart.end()) continue; // parent has no mesh part -> leave as a root

            services::PrefabRigPartDTO& part = desc.parts[partIndex];
            part.parentPartIndex = it->second;
            part.attachParentSocket = n.attachSocketName;
            part.attachChildRotation = n.rotation; // Euler degrees, as TransformComponent.rotation
            part.attachChildScale = n.scale;
        }

        // IK chains. Each owning node's part is the body part; default the target binding to the
        // first STATIC part (a weapon/prop typically holds the grip socket the hand IK-s to). The
        // window lets the user override both the part and the socket.
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const int bodyPart = nodePartIndex[i];
            if (bodyPart < 0) continue;

            const PrefabEntityNode& n = *nodes[i];
            for (const auto& chainCfg : n.ikChains)
            {
                services::PrefabRigIKDTO ik;
                ik.chain = chainCfg;
                ik.bodyPartIndex = bodyPart;
                ik.targetPartIndex = -1;
                ik.targetSocketName.clear();

                for (size_t p = 0; p < desc.parts.size(); ++p)
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

        return desc;
    }
}
