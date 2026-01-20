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

    // === Build Commands ===
    struct BuildScriptsCommand : ICommand<bool> {
        std::string_view getName() const override { return "BuildScripts"; }
    };

    struct CleanScriptsCommand : ICommand<bool> {
        std::string_view getName() const override { return "CleanScripts"; }
    };

    // ============================================
    // QUERIES - Read-only script information
    // ============================================

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

    // === Build Queries ===
    struct IsScriptsCompiledQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsScriptsCompiled"; }
    };

    // ============================================
    // PLAYBACK COMMANDS
    // ============================================

    struct PlayScriptCommand : ICommand<> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "PlayScript"; }
    };

    struct PauseScriptCommand : ICommand<> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "PauseScript"; }
    };

    struct StopScriptCommand : ICommand<> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "StopScript"; }
    };

    struct ResetScriptCommand : ICommand<> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "ResetScript"; }
    };

    struct SetScriptPlaybackParamsCommand : ICommand<> {
        services::EntityHandle entity;
        std::string scriptPath;
        services::ScriptPlaybackParams params;

        std::string_view getName() const override { return "SetScriptPlaybackParams"; }
    };

    // Batch operations
    struct PlayAllScriptsOnEntityCommand : ICommand<> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "PlayAllScriptsOnEntity"; }
    };

    struct PauseAllScriptsOnEntityCommand : ICommand<> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "PauseAllScriptsOnEntity"; }
    };

    struct StopAllScriptsOnEntityCommand : ICommand<> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "StopAllScriptsOnEntity"; }
    };

    // ============================================
    // PLAYBACK QUERIES
    // ============================================

    struct GetScriptPlaybackStateQuery : IQuery<services::ScriptPlaybackState> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "GetScriptPlaybackState"; }
    };

    struct IsScriptPlayingQuery : IQuery<bool> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "IsScriptPlaying"; }
    };

    struct GetScriptPlaybackParamsQuery : IQuery<services::ScriptPlaybackParams> {
        services::EntityHandle entity;
        std::string scriptPath;

        std::string_view getName() const override { return "GetScriptPlaybackParams"; }
    };

}
