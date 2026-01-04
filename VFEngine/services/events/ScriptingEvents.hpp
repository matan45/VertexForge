#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/ScriptTypes.hpp"
#include <optional>
#include <vector>
#include <string>

namespace events::scripting {

    // ============================================
    // COMMANDS - Operations that modify script state
    // ============================================

    struct AttachScriptCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::ScriptData data;

        std::string_view getName() const override { return "AttachScript"; }
    };

    struct DetachScriptCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "DetachScript"; }
    };

    struct SetScriptEnabledCommand : ICommand<> {
        services::EntityHandle entity;
        std::string scriptPath;
        bool enabled;

        std::string_view getName() const override { return "SetScriptEnabled"; }
    };

    struct TriggerScriptStartCommand : ICommand<> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "TriggerScriptStart"; }
    };

    struct TriggerScriptDestroyCommand : ICommand<> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "TriggerScriptDestroy"; }
    };

    // ============================================
    // QUERIES - Read-only script information
    // ============================================

    struct HasScriptQuery : IQuery<bool> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "HasScript"; }
    };

    struct IsScriptEnabledQuery : IQuery<bool> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "IsScriptEnabled"; }
    };

    struct GetScriptDataQuery : IQuery<std::optional<services::ScriptComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetScriptData"; }
    };

    struct GetScriptPathsQuery : IQuery<std::vector<std::string>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetScriptPaths"; }
    };

    // ============================================
    // NOTIFICATIONS - Broadcast events about scripts
    // ============================================

    struct ScriptAttachedNotification : INotification {
        services::EntityHandle entity;
        std::string scriptPath;
        std::string className;

        std::string_view getName() const override { return "ScriptAttached"; }
    };

    struct ScriptDetachedNotification : INotification {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script was detached

        std::string_view getName() const override { return "ScriptDetached"; }
    };

    struct ScriptErrorNotification : INotification {
        services::EntityHandle entity;
        services::ScriptError error;

        std::string_view getName() const override { return "ScriptError"; }
    };

    struct ScriptStartedNotification : INotification {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script started

        std::string_view getName() const override { return "ScriptStarted"; }
    };

}
