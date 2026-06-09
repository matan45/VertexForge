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

    // Register a native function callable from mType scripts. The std::any wraps
    // either std::pair<MTypeNativeFn, void*> (plugin C ABI — see mType's
    // plugin/PluginHostApi.h; the handler wraps it in the host trampoline) or a
    // raw services::NativeFunction (mType NativeDelegate, engine-internal use).
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

    // === Debugger (VK-1371) ===
    // Start the embedded mType debug server so the VS Code extension can attach
    // and debug engine-run scripts. Default port matches the extension's attach
    // default (MYT-379).
    struct StartScriptDebuggerCommand : ICommand<> {
        int port = 5005;

        std::string_view getName() const override { return "StartScriptDebugger"; }
    };

    struct StopScriptDebuggerCommand : ICommand<> {
        std::string_view getName() const override { return "StopScriptDebugger"; }
    };

    struct IsScriptDebuggerActiveQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsScriptDebuggerActive"; }
    };

}
