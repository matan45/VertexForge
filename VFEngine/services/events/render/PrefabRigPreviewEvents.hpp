#pragma once

// VK-1433 — CQRS events for the Prefab Rig Preview (Layer C boundary).
// Mirrors the AnimationPreview/MeshPreview event shape: per-instance commands keyed by
// PreviewInstanceId, render via a query returning ViewportTextureHandle.

#include "../EventTypes.hpp"
#include "../../providers/render/IPrefabRigPreviewProvider.hpp"
#include "../../providers/render/IMeshPreviewProvider.hpp" // PreviewEnvironmentParams
#include "../../providers/PreviewInstanceId.hpp"
#include "../../data/DTOs.hpp"          // ViewportTextureHandle
#include "../../data/PrefabRigDescDTO.hpp"
#include "animator/SocketTypes.hpp"
#include "animator/IKTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace services::events::prefabrigpreview
{
    struct InitPrefabRigPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "InitPrefabRigPreview"; }
    };

    struct BuildPrefabRigPreviewCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        PrefabRigDescDTO desc;
        std::string_view getName() const override { return "BuildPrefabRigPreview"; }
    };

    struct CleanUpPrefabRigPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "CleanUpPrefabRigPreview"; }
    };

    struct UpdatePrefabRigPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        float deltaTime = 0.0f;
        std::string_view getName() const override { return "UpdatePrefabRigPreview"; }
    };

    struct UpdatePrefabRigCameraCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 cameraPos{0.0f};
        std::string_view getName() const override { return "UpdatePrefabRigCamera"; }
    };

    struct SetPrefabRigEnvironmentCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        PreviewEnvironmentParams params;
        std::string_view getName() const override { return "SetPrefabRigEnvironment"; }
    };

    struct SetPrefabRigRootMatrixCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        glm::mat4 model{1.0f};
        std::string_view getName() const override { return "SetPrefabRigRootMatrix"; }
    };

    struct SetPrefabRigStateCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string stateName;
        float blendDuration = 0.25f;
        std::string_view getName() const override { return "SetPrefabRigState"; }
    };

    struct SetPrefabRigBoolCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string name;
        bool value = false;
        std::string_view getName() const override { return "SetPrefabRigBool"; }
    };

    struct SetPrefabRigFloatCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string name;
        float value = 0.0f;
        std::string_view getName() const override { return "SetPrefabRigFloat"; }
    };

    struct SetPrefabRigIntCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string name;
        int32_t value = 0;
        std::string_view getName() const override { return "SetPrefabRigInt"; }
    };

    struct SetPrefabRigTriggerCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string name;
        std::string_view getName() const override { return "SetPrefabRigTrigger"; }
    };

    struct PlayPrefabRigCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PlayPrefabRig"; }
    };

    struct PausePrefabRigCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PausePrefabRig"; }
    };

    struct SetPrefabRigSocketsCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::vector<animator::SocketDefinition> sockets;
        std::string_view getName() const override { return "SetPrefabRigSockets"; }
    };

    struct SetPrefabRigChainsCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::vector<animator::ik::IKChainConfig> chains;
        std::string_view getName() const override { return "SetPrefabRigChains"; }
    };

    // VK-1433 — frame-by-frame scrub (skeletal part, editor-transient).
    struct StepPrefabRigFrameCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        int frames = 0; // +/- step count
        std::string_view getName() const override { return "StepPrefabRigFrame"; }
    };

    struct SetPrefabRigNormalizedTimeCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        float t = 0.0f; // normalized [0,1]
        std::string_view getName() const override { return "SetPrefabRigNormalizedTime"; }
    };

    // VK-1433 — transform gizmo (editor-transient, never serialized).
    struct SetPrefabRigPartPreviewTransformCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        glm::mat4 transform{1.0f};
        std::string_view getName() const override { return "SetPrefabRigPartPreviewTransform"; }
    };

    struct ResetPrefabRigPreviewTransformsCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "ResetPrefabRigPreviewTransforms"; }
    };

    // ---- Queries ----

    struct RenderPrefabRigPreviewQuery : ::events::IQuery<ViewportTextureHandle>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "RenderPrefabRigPreview"; }
    };

    struct IsPrefabRigPreviewBuiltQuery : ::events::IQuery<bool>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsPrefabRigPreviewBuilt"; }
    };

    struct GetPrefabRigPartCountQuery : ::events::IQuery<size_t>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetPrefabRigPartCount"; }
    };

    struct IsPrefabRigPausedQuery : ::events::IQuery<bool>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsPrefabRigPaused"; }
    };

    struct GetPrefabRigStatesQuery : ::events::IQuery<std::vector<PrefabRigStateInfo>>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string_view getName() const override { return "GetPrefabRigStates"; }
    };

    struct GetPrefabRigSocketsQuery : ::events::IQuery<std::vector<animator::SocketDefinition>>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string_view getName() const override { return "GetPrefabRigSockets"; }
    };

    struct GetPrefabRigChainsQuery : ::events::IQuery<std::vector<animator::ik::IKChainConfig>>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetPrefabRigChains"; }
    };

    // VK-1433 — current normalized time [0,1) of a skeletal part's base state (scrub readout).
    struct GetPrefabRigNormalizedTimeQuery : ::events::IQuery<float>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string_view getName() const override { return "GetPrefabRigNormalizedTime"; }
    };

    // VK-1433 — live composed world matrix of a part (anchors the transform + socket gizmos).
    struct GetPrefabRigPartWorldQuery : ::events::IQuery<glm::mat4>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string_view getName() const override { return "GetPrefabRigPartWorld"; }
    };

    // VK-1433 Phase 1b — world-space joints of a skeletal part for editor bone-picking.
    struct GetPrefabRigJointWorldsQuery : ::events::IQuery<std::vector<PrefabRigJoint>>
    {
        PreviewInstanceId instanceId;
        size_t part = 0;
        std::string_view getName() const override { return "GetPrefabRigJointWorlds"; }
    };
}
