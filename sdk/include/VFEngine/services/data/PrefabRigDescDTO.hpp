#pragma once

// VK-1433 — Prefab Rig Preview cross-boundary description.
//
// PrefabRigDescDTO is the Editor-safe parallel of controllers::PrefabRigDesc
// (graphics/controllers/preview/PrefabRigAssembly.hpp). The editor window builds
// this from a parsed .vfPrefab JSON tree and hands it across the service boundary;
// the PrefabRigPreviewAdapter (Core) rebuilds the real PrefabRigDesc from it before
// calling the controller. The window never sees PrefabRigAssembly.hpp (which pulls
// Graphics/animator-internal types) — it only sees this DTO plus the imgui void*.
//
// Field-for-field this mirrors PrefabRigDesc so the adapter conversion is a flat copy.
// Only types Editor may include are used: std::string/glm + animator::ik::IKChainConfig
// (utilities/animator, which Editor already includes for the IK panel).

#include "animator/IKTypes.hpp"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace services
{
    // One renderable part of the assembled rig (mirrors controllers::PrefabRigPart).
    struct PrefabRigPartDTO
    {
        std::string meshPath;           // .vfMesh (skeletal or static)
        std::string animatorPath;       // empty => static part (no animator)
        std::string retargetPath;       // empty => no retarget (.vfretarget map)

        int parentPartIndex = -1;       // -1 => root part
        std::string attachParentSocket; // bone/static socket name on the parent (empty for roots)

        // SocketAttachment-driven child transform. Translation is intentionally absent:
        // the child rides the socket origin (matches SocketAttachmentUpdater::applyModelOffset).
        glm::vec3 attachChildRotation{0.0f}; // degrees (Euler), as TransformComponent.rotation
        glm::vec3 attachChildScale{1.0f};

        // VK-1433 — the mesh node's accumulated LOCAL transform within the prefab (its own +
        // ancestor TransformComponents, math::composeMatrix / TransformComponent convention).
        // Applied to ROOT parts so the preview reflects the prefab's authored rotation / scale /
        // position exactly like the scene viewport. Socketed children ignore it (their world
        // comes from the parent's socket).
        glm::mat4 localTransform{1.0f};

        // Material references from the prefab's MaterialComponent (read by Layer B only).
        std::string defaultMaterialPath;                     // whole-part material
        std::map<std::string, std::string> subMeshMaterials; // submesh name -> material path override
    };

    // One IK chain plus its (editor-transient) target binding (mirrors controllers::PrefabRigIK).
    struct PrefabRigIKDTO
    {
        animator::ik::IKChainConfig chain; // chainName/tipBoneName/chainBoneNames/constraints/weight/enabled

        int bodyPartIndex = -1;       // which skeletal part owns (solves) the chain
        int targetPartIndex = -1;     // part whose socket drives the target (editor-transient)
        std::string targetSocketName; // grip socket name on the target part (editor-transient)
    };

    struct PrefabRigDescDTO
    {
        std::vector<PrefabRigPartDTO> parts;
        std::vector<PrefabRigIKDTO> ik;
    };
}
