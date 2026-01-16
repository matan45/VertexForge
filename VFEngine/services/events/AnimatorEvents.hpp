#pragma once

#include "EventTypes.hpp"
#include "../providers/IAnimatorProvider.hpp"
#include "../providers/IAnimationPreviewProvider.hpp"
#include "../providers/PreviewInstanceId.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/DTOs.hpp"
#include "animator/AnimatorTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <optional>

namespace services::events::animator
{
    // ============================================================
    // PREVIEW INSTANCE MANAGEMENT COMMANDS
    // ============================================================

    struct InitAnimatorPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "InitAnimatorPreview"; }
    };

    struct CleanUpAnimatorPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "CleanUpAnimatorPreview"; }
    };

    // ============================================================
    // ANIMATOR DATA MANAGEMENT COMMANDS
    // ============================================================

    struct LoadAnimatorDataCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        std::string path;
        std::string_view getName() const override { return "LoadAnimatorData"; }
    };

    struct SaveAnimatorDataCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        std::string path;
        std::string_view getName() const override { return "SaveAnimatorData"; }
    };

    struct CreateNewAnimatorCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        std::string name;
        std::string_view getName() const override { return "CreateNewAnimator"; }
    };

    struct GetAnimatorDataQuery : ::events::IQuery<const ::animator::AnimatorData*>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetAnimatorData"; }
    };

    // ============================================================
    // STATE MANAGEMENT COMMANDS
    // ============================================================

    struct AddStateCommand : ::events::ICommand<uint32_t>
    {
        PreviewInstanceId instanceId;
        std::string name;
        glm::vec2 position{0.0f, 0.0f};
        std::string_view getName() const override { return "AddState"; }
    };

    struct RemoveStateCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t stateId;
        std::string_view getName() const override { return "RemoveState"; }
    };

    struct UpdateStateCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t stateId;
        std::string name;
        std::string animationPath;
        float playbackSpeed = 1.0f;
        bool loop = true;
        std::string_view getName() const override { return "UpdateState"; }
    };

    struct SetStatePositionCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t stateId;
        glm::vec2 position;
        std::string_view getName() const override { return "SetStatePosition"; }
    };

    struct SetDefaultStateCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t stateId;
        std::string_view getName() const override { return "SetDefaultState"; }
    };

    // ============================================================
    // TRANSITION MANAGEMENT COMMANDS
    // ============================================================

    struct AddTransitionCommand : ::events::ICommand<uint32_t>
    {
        PreviewInstanceId instanceId;
        uint32_t sourceStateId;
        uint32_t targetStateId;
        std::string_view getName() const override { return "AddTransition"; }
    };

    struct RemoveTransitionCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t transitionId;
        std::string_view getName() const override { return "RemoveTransition"; }
    };

    struct UpdateTransitionCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t transitionId;
        float blendDuration = 0.25f;
        bool hasExitTime = false;
        float exitTime = 1.0f;
        int32_t priority = 0;
        std::string_view getName() const override { return "UpdateTransition"; }
    };

    // ============================================================
    // TRANSITION CONDITION COMMANDS
    // ============================================================

    struct AddTransitionConditionCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t transitionId;
        std::string parameterName;
        ::animator::ComparisonOperator op;
        ::animator::AnimatorParameterValue value;
        std::string_view getName() const override { return "AddTransitionCondition"; }
    };

    struct RemoveTransitionConditionCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t transitionId;
        size_t conditionIndex;
        std::string_view getName() const override { return "RemoveTransitionCondition"; }
    };

    struct UpdateTransitionConditionCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        uint32_t transitionId;
        size_t conditionIndex;
        std::string parameterName;
        ::animator::ComparisonOperator op;
        ::animator::AnimatorParameterValue value;
        std::string_view getName() const override { return "UpdateTransitionCondition"; }
    };

    // ============================================================
    // PARAMETER MANAGEMENT COMMANDS
    // ============================================================

    struct AddParameterCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        std::string name;
        ::animator::AnimatorParameterType type;
        ::animator::AnimatorParameterValue defaultValue;
        std::string_view getName() const override { return "AddParameter"; }
    };

    struct RemoveParameterCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        std::string name;
        std::string_view getName() const override { return "RemoveParameter"; }
    };

    struct UpdateParameterCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        std::string oldName;
        std::string newName;
        ::animator::AnimatorParameterType type;
        ::animator::AnimatorParameterValue defaultValue;
        std::string_view getName() const override { return "UpdateParameter"; }
    };

    // ============================================================
    // NODE GRAPH POSITION COMMANDS
    // ============================================================

    struct SetAnyStatePositionCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        glm::vec2 position;
        std::string_view getName() const override { return "SetAnyStatePosition"; }
    };

    struct SetEntryPositionCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        glm::vec2 position;
        std::string_view getName() const override { return "SetEntryPosition"; }
    };

    // ============================================================
    // PREVIEW PLAYBACK COMMANDS
    // ============================================================

    struct UpdateAnimatorPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        float deltaTime;
        std::string_view getName() const override { return "UpdateAnimatorPreview"; }
    };

    struct PlayAnimatorPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PlayAnimatorPreview"; }
    };

    struct PauseAnimatorPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PauseAnimatorPreview"; }
    };

    struct StopAnimatorPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "StopAnimatorPreview"; }
    };

    struct ResetAnimatorPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "ResetAnimatorPreview"; }
    };

    // ============================================================
    // PREVIEW PARAMETER COMMANDS
    // ============================================================

    struct SetAnimatorPreviewFloatCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string name;
        float value;
        std::string_view getName() const override { return "SetAnimatorPreviewFloat"; }
    };

    struct SetAnimatorPreviewIntCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string name;
        int32_t value;
        std::string_view getName() const override { return "SetAnimatorPreviewInt"; }
    };

    struct SetAnimatorPreviewBoolCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string name;
        bool value;
        std::string_view getName() const override { return "SetAnimatorPreviewBool"; }
    };

    struct SetAnimatorPreviewTriggerCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string name;
        std::string_view getName() const override { return "SetAnimatorPreviewTrigger"; }
    };

    struct ForceTransitionToCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        uint32_t stateId;
        float blendDuration = 0.25f;
        std::string_view getName() const override { return "ForceTransitionTo"; }
    };

    // ============================================================
    // PREVIEW QUERIES
    // ============================================================

    struct IsAnimatorPlayingQuery : ::events::IQuery<bool>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsAnimatorPlaying"; }
    };

    struct GetCurrentStateIdQuery : ::events::IQuery<uint32_t>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetCurrentStateId"; }
    };

    struct GetCurrentStateTimeQuery : ::events::IQuery<float>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetCurrentStateTime"; }
    };

    struct GetNormalizedStateTimeQuery : ::events::IQuery<float>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetNormalizedStateTime"; }
    };

    struct IsBlendingQuery : ::events::IQuery<bool>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsBlending"; }
    };

    struct GetBlendWeightQuery : ::events::IQuery<float>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetBlendWeight"; }
    };

    // ============================================================
    // PREVIEW RENDERING COMMANDS/QUERIES
    // ============================================================

    struct SetAnimatorPreviewRenderParamsCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        AnimatorPreviewRenderParams params;
        std::string_view getName() const override { return "SetAnimatorPreviewRenderParams"; }
    };

    struct UpdateAnimatorCameraCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        AnimatorPreviewCameraParams camera;
        std::string_view getName() const override { return "UpdateAnimatorCamera"; }
    };

    struct RenderAnimatorPreviewQuery : ::events::IQuery<ViewportTextureHandle>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "RenderAnimatorPreview"; }
    };

    struct GetAnimatorPreviewBonesQuery : ::events::IQuery<std::vector<EvaluatedBoneInfo>>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetAnimatorPreviewBones"; }
    };

    // ============================================================
    // NOTIFICATIONS
    // ============================================================

    struct AnimatorStateChangedNotification : ::events::INotification
    {
        PreviewInstanceId instanceId;
        uint32_t previousStateId;
        uint32_t currentStateId;
        std::string_view getName() const override { return "AnimatorStateChanged"; }
    };

    struct AnimatorDataModifiedNotification : ::events::INotification
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "AnimatorDataModified"; }
    };

    // ============================================================
    // RUNTIME ANIMATOR COMMANDS (Entity-based for scripting)
    // ============================================================

    struct SetEntityAnimatorFloatCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string parameterName;
        float value;
        std::string_view getName() const override { return "SetEntityAnimatorFloat"; }
    };

    struct SetEntityAnimatorIntCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string parameterName;
        int32_t value;
        std::string_view getName() const override { return "SetEntityAnimatorInt"; }
    };

    struct SetEntityAnimatorBoolCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string parameterName;
        bool value;
        std::string_view getName() const override { return "SetEntityAnimatorBool"; }
    };

    struct SetEntityAnimatorTriggerCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string parameterName;
        std::string_view getName() const override { return "SetEntityAnimatorTrigger"; }
    };

    struct PlayEntityAnimatorCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "PlayEntityAnimator"; }
    };

    struct PauseEntityAnimatorCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "PauseEntityAnimator"; }
    };

    struct StopEntityAnimatorCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "StopEntityAnimator"; }
    };

    struct ResetEntityAnimatorCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "ResetEntityAnimator"; }
    };

    struct ForceEntityTransitionToCommand : ::events::ICommand<bool>
    {
        ::services::EntityHandle entity;
        std::string stateName;
        float blendDuration = 0.25f;
        std::string_view getName() const override { return "ForceEntityTransitionTo"; }
    };

    // ============================================================
    // RUNTIME ANIMATOR QUERIES (Entity-based for scripting)
    // ============================================================

    struct GetEntityAnimatorFloatQuery : ::events::IQuery<float>
    {
        ::services::EntityHandle entity;
        std::string parameterName;
        std::string_view getName() const override { return "GetEntityAnimatorFloat"; }
    };

    struct GetEntityAnimatorIntQuery : ::events::IQuery<int32_t>
    {
        ::services::EntityHandle entity;
        std::string parameterName;
        std::string_view getName() const override { return "GetEntityAnimatorInt"; }
    };

    struct GetEntityAnimatorBoolQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string parameterName;
        std::string_view getName() const override { return "GetEntityAnimatorBool"; }
    };

    struct IsEntityAnimatorPlayingQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "IsEntityAnimatorPlaying"; }
    };

    struct IsEntityAnimatorBlendingQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "IsEntityAnimatorBlending"; }
    };

    struct GetEntityAnimatorCurrentStateQuery : ::events::IQuery<std::string>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "GetEntityAnimatorCurrentState"; }
    };

    struct GetEntityAnimatorNormalizedTimeQuery : ::events::IQuery<float>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "GetEntityAnimatorNormalizedTime"; }
    };

    struct HasEntityAnimatorQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "HasEntityAnimator"; }
    };
}
