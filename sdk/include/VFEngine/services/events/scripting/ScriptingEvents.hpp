#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/ScriptTypes.hpp"
#include <optional>
#include <vector>
#include <string>
#include <any>

namespace events::scripting {

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

    struct BuildScriptsCommand : ICommand<bool> {
        std::string_view getName() const override { return "BuildScripts"; }
    };

    struct CleanScriptsCommand : ICommand<bool> {
        std::string_view getName() const override { return "CleanScripts"; }
    };

    struct IsScriptEnabledQuery : IQuery<bool> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "IsScriptEnabled"; }
    };

    struct GetScriptDataQuery : IQuery<std::optional<services::ScriptData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetScriptData"; }
    };

    struct GetScriptPathsQuery : IQuery<std::vector<std::string>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetScriptPaths"; }
    };

    struct IsScriptsCompiledQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsScriptsCompiled"; }
    };

    // Register a native function callable from mType scripts.
    // The function must be std::any wrapping services::NativeFunction (mType's
    // environment::registry::NativeDelegate — a {void* userData, function
    // pointer} pair; see sdk/deps/mType/environment/registry/NativeDelegate.hpp).
    struct RegisterNativeScriptFunctionCommand : ICommand<> {
        std::string functionName;
        std::any function;

        std::string_view getName() const override { return "RegisterNativeScriptFunction"; }
    };

    // Remove a previously registered native function (plugin unload cleanup —
    // the delegate's function pointer dies with the plugin DLL).
    struct UnregisterNativeScriptFunctionCommand : ICommand<> {
        std::string functionName;

        std::string_view getName() const override { return "UnregisterNativeScriptFunction"; }
    };

    struct SetInstancePriorityCommand : ICommand<> {
        uint64_t instanceId;
        int priority;

        std::string_view getName() const override { return "SetInstancePriority"; }
    };

}
