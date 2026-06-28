#pragma once

#include "data/PrefabRigDescDTO.hpp"
#include "data/EntityHandle.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "PrefabRigValidation.hpp" // prefabrigval::PartRefStatus
#include "animator/SocketTypes.hpp"
#include "animator/IKTypes.hpp"
#include <string>
#include <vector>
#include <cstdint>

// VK-1443 — shared edit state for the PrefabPreviewWindow decomposition.
//
// PrefabPreviewWindow is being split into four sub-controllers (sandbox / hierarchy / authoring /
// undo). The fields below are touched by two or more of them, so they need a single shared view.
// To keep the refactor strictly behavior-preserving, the storage stays as PrefabPreviewWindow
// members and this struct holds *references* bound to them: the window's own retained methods keep
// using the bare field names (no rename, no risk of a substring slip), while the sub-controllers
// reach the very same storage through `ctx.field`. `instanceId` is the one value member — a copy of
// the window's getInstanceId() (== the window `this`), used for the per-window preview CQRS keys.
namespace windows
{
    // Hoisted out of PrefabPreviewWindow so both the window and PrefabRigEditContext can name it
    // (a nested type would force a circular include between the two headers). Exactly one gizmo is
    // active at a time so they never fight.
    enum class GizmoMode { Transform, BoneSocket, StaticSocket, IK };

    struct PrefabRigEditContext
    {
        services::PreviewInstanceId instanceId; // == the owning window's getInstanceId() (value copy)

        services::EntityHandle& sandboxRoot;
        services::PrefabRigDescDTO& rigDesc;
        std::vector<services::EntityHandle>& partEntities;
        uint32_t& sandboxEntityCount;

        services::EntityHandle& selectedSandboxEntity;
        int& selectedPart;
        int& selectedSocketIndex;
        int& selectedChainIndex;

        std::vector<animator::SocketDefinition>& editSockets;
        std::vector<animator::ik::IKChainConfig>& editChains;
        bool& chainsLoaded;

        GizmoMode& gizmoMode;
        bool& bonePickArmed;

        bool& previewInitialized;
        bool& previewBuilt;

        bool& prefabLoaded;
        bool& loadFailed;
        std::string& errorMessage;
        std::string& prefabName;
        std::string& prefabVersion;

        std::vector<prefabrigval::PartRefStatus>& partRefStatuses;
        int& missingRefCount;

        bool& dirty;
    };
}
