#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/AnimatorDebugTypes.hpp"
#include <string>

namespace services::events::animator
{
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

    struct SetEntityRootMotionCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        bool enabled;
        std::string_view getName() const override { return "SetEntityRootMotion"; }
    };

    struct GetEntityRootMotionQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "GetEntityRootMotion"; }
    };

    // Layer management events
    struct SetAnimationLayerWeightCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        uint32_t layerIndex;
        float weight;
        std::string_view getName() const override { return "SetAnimationLayerWeight"; }
    };

    struct GetAnimationLayerWeightQuery : ::events::IQuery<float>
    {
        ::services::EntityHandle entity;
        uint32_t layerIndex;
        std::string_view getName() const override { return "GetAnimationLayerWeight"; }
    };

    struct GetAnimationLayerCountQuery : ::events::IQuery<uint32_t>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "GetAnimationLayerCount"; }
    };

    struct GetAnimationLayerNameQuery : ::events::IQuery<std::string>
    {
        ::services::EntityHandle entity;
        uint32_t layerIndex;
        std::string_view getName() const override { return "GetAnimationLayerName"; }
    };

    struct GetAnimatorRuntimeDebugDataQuery : ::events::IQuery<::services::AnimatorRuntimeDebugData>
    {
        ::services::EntityHandle entity;
        uint32_t layerIndex = 0;
        std::string_view getName() const override { return "GetAnimatorRuntimeDebugData"; }
    };
}
