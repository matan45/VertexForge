#pragma once

#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
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
}
