#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/ScriptTypes.hpp"
#include <optional>
#include <vector>
#include <string>
#include <any>

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
        std::string scriptPath;  // Which script to detach

        std::string_view getName() const override { return "DetachScript"; }
    };

    struct SetScriptEnabledCommand : ICommand<> {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script to enable/disable
        bool enabled;

        std::string_view getName() const override { return "SetScriptEnabled"; }
    };

    struct SetScriptPropertyCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script's property to set
        std::string propertyName;
        std::any value;

        std::string_view getName() const override { return "SetScriptProperty"; }
    };

    struct CallScriptMethodCommand : ICommand<std::optional<std::any>> {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script's method to call
        std::string methodName;
        std::vector<std::any> args;

        std::string_view getName() const override { return "CallScriptMethod"; }
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
        std::string scriptPath;  // Which script to check for

        std::string_view getName() const override { return "HasScript"; }
    };

    struct IsScriptEnabledQuery : IQuery<bool> {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script to check

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

    struct GetScriptPropertiesQuery : IQuery<std::vector<services::ScriptPropertyInfo>> {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script's properties to get

        std::string_view getName() const override { return "GetScriptProperties"; }
    };

    struct GetScriptPropertyQuery : IQuery<std::optional<std::any>> {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script's property to get
        std::string propertyName;

        std::string_view getName() const override { return "GetScriptProperty"; }
    };

    struct GetScriptMethodsQuery : IQuery<std::vector<services::ScriptMethodInfo>> {
        services::EntityHandle entity;
        std::string scriptPath;  // Which script's methods to get

        std::string_view getName() const override { return "GetScriptMethods"; }
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
