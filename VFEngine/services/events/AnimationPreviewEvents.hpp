#pragma once

#include "EventTypes.hpp"
#include "../providers/IAnimationPreviewProvider.hpp"
#include "../providers/PreviewInstanceId.hpp"
#include "../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace services::events::animpreview
{
    struct InitAnimationPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "InitAnimationPreview"; }
    };

    struct CleanUpAnimationPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "CleanUpAnimationPreview"; }
    };

    struct LoadAnimationPreviewMeshCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        std::string meshPath;
        std::string_view getName() const override { return "LoadAnimationPreviewMesh"; }
    };

    struct LoadAnimationPreviewAnimationCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        std::string animationPath;
        std::string_view getName() const override { return "LoadAnimationPreviewAnimation"; }
    };

    struct PlayAnimationCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PlayAnimation"; }
    };

    struct PauseAnimationCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PauseAnimation"; }
    };

    struct StopAnimationCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "StopAnimation"; }
    };

    struct SetAnimationPlaybackTimeCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        float timeSeconds;
        std::string_view getName() const override { return "SetAnimationPlaybackTime"; }
    };

    struct SetAnimationLoopingCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        bool looping;
        std::string_view getName() const override { return "SetAnimationLooping"; }
    };

    struct SetAnimationPlaybackSpeedCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        float speed;
        std::string_view getName() const override { return "SetAnimationPlaybackSpeed"; }
    };

    struct UpdateAnimationPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        float deltaTime;
        std::string_view getName() const override { return "UpdateAnimationPreview"; }
    };

    struct SetAnimationPreviewParamsCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        AnimationPreviewParams params;
        std::string_view getName() const override { return "SetAnimationPreviewParams"; }
    };

    struct UpdateAnimationCameraCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        std::string_view getName() const override { return "UpdateAnimationCamera"; }
    };

    struct IsAnimationPlayingQuery : ::events::IQuery<bool>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsAnimationPlaying"; }
    };

    struct GetAnimationPlaybackTimeQuery : ::events::IQuery<float>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetAnimationPlaybackTime"; }
    };

    struct RenderAnimationPreviewQuery : ::events::IQuery<ViewportTextureHandle>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "RenderAnimationPreview"; }
    };

    struct GetAnimationPreviewEvaluatedBonesQuery : ::events::IQuery<std::vector<EvaluatedBoneInfo>>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetAnimationPreviewEvaluatedBones"; }
    };
}
