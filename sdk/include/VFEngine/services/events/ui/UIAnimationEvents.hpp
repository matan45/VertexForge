#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI Animation Commands
    // ============================================

    struct AddUIAnimationComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIAnimationComponent"; }
    };

    struct RemoveUIAnimationComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIAnimationComponent"; }
    };

    struct SetUIAnimationDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIAnimationData animationData;

        std::string_view getName() const override { return "SetUIAnimationData"; }
    };

    struct PlayUIAnimationCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "PlayUIAnimation"; }
    };

    struct StopUIAnimationCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "StopUIAnimation"; }
    };

    struct PauseUIAnimationCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "PauseUIAnimation"; }
    };

    struct ResumeUIAnimationCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "ResumeUIAnimation"; }
    };

    // ============================================
    // UI Animation Queries
    // ============================================

    struct HasUIAnimationComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIAnimationComponent"; }
    };

    struct GetUIAnimationDataQuery : IQuery<std::optional<services::UIAnimationData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIAnimationData"; }
    };

    struct IsUIAnimationPlayingQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsUIAnimationPlaying"; }
    };

    // ============================================
    // UI Animation Notifications
    // ============================================

    struct UIAnimationStartedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIAnimationStarted"; }
    };

    struct UIAnimationCompletedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIAnimationCompleted"; }
    };

}
