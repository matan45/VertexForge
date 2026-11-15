#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// VK-1682: the Editor.exe command line.
//
//   Editor.exe [project.vfproj] [--launch-status <file>] [--mcp] [--mcp-port <n>] [--mcp-token <t>]
//              [--console]
//
// The first positional argument is the project path; flags may appear in any order. Parsing is
// strict: an unknown flag, a missing value, an invalid port, a repeated flag or a second
// positional argument is an error, and Editor.exe exits with EditorExitCode::BadArguments.
//
// VK-1683: --console opens a console window of the editor's own and mirrors the redirected
// output into it (launch/EditorConsole.hpp), for a launcher that captures the output in a log.
namespace launch
{
    // VK-1683: the launch-contract revision this editor implements. engine.json carries it as
    // `launchContract` (tools/engine_version.lua reads this line), so a launcher passes only the
    // flags this editor accepts. Bump it whenever a flag is added: an older editor rejects an
    // unknown flag (strict parsing, exit 2).
    //   1 = the VK-1682 contract + --console (VK-1683)
    inline constexpr int editorLaunchContract = 1;

    inline constexpr uint16_t minMcpPort = 1024;
    inline constexpr uint16_t maxMcpPort = 65535;

    // "7878" -> 7878. Empty optional for non-numeric or out-of-range [minMcpPort, maxMcpPort].
    std::optional<uint16_t> parseMcpPort(std::string_view text) noexcept;

    struct EditorArguments
    {
        std::string projectPath;              // empty = start without a project
        std::string launchStatusPath;         // --launch-status <file>; empty = no status file
        bool mcpEnable = false;               // --mcp: enable with the preference (or default) port
        std::optional<uint16_t> mcpPort;      // --mcp-port <n>: forces enabled
        std::optional<std::string> mcpToken;  // --mcp-token <t>: forces enabled, bearer token
        bool console = false;                 // --console: open a console window of its own and mirror
                                              // the redirected output into it (VK-1683)
    };

    struct EditorArgumentsResult
    {
        EditorArguments args;              // filled as far as parsing got, even when errors exist
        std::vector<std::string> errors;   // one sentence per problem, without a program prefix

        bool ok() const noexcept { return errors.empty(); }
    };

    // `arguments` excludes argv[0]. launchStatusPath is kept even when other arguments are bad,
    // so the failure can still be reported through the status file.
    EditorArgumentsResult parseEditorArguments(std::span<const std::string_view> arguments);

    std::string_view editorUsage() noexcept;
}
