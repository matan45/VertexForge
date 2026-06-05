#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <optional>

namespace events::ui {

    // ============================================
    // UI Scroll Commands
    // ============================================

    struct AddUIScrollComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIScrollComponent"; }
    };

    struct RemoveUIScrollComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIScrollComponent"; }
    };

    struct SetUIScrollDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIScrollData scrollData;

        std::string_view getName() const override { return "SetUIScrollData"; }
    };

    struct SetScrollOffsetCommand : ICommand<bool> {
        services::EntityHandle entity;
        glm::vec2 offset;

        std::string_view getName() const override { return "SetScrollOffset"; }
    };

    // ============================================
    // UI Scroll Queries
    // ============================================

    struct HasUIScrollComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIScrollComponent"; }
    };

    struct GetUIScrollDataQuery : IQuery<std::optional<services::UIScrollData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIScrollData"; }
    };

    struct GetScrollOffsetQuery : IQuery<std::optional<glm::vec2>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetScrollOffset"; }
    };

    // ============================================
    // UI Layout Group Commands
    // ============================================

    struct AddUILayoutGroupComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUILayoutGroupComponent"; }
    };

    struct RemoveUILayoutGroupComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUILayoutGroupComponent"; }
    };

    struct SetUILayoutGroupDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UILayoutGroupData layoutGroupData;

        std::string_view getName() const override { return "SetUILayoutGroupData"; }
    };

    // ============================================
    // UI Layout Group Queries
    // ============================================

    struct HasUILayoutGroupComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUILayoutGroupComponent"; }
    };

    struct GetUILayoutGroupDataQuery : IQuery<std::optional<services::UILayoutGroupData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUILayoutGroupData"; }
    };

}
