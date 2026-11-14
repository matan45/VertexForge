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
//
// The first positional argument is the project path; flags may appear in any order. Parsing is
// strict: an unknown flag, a missing value, an invalid port, a repeated flag or a second
// positional argument is an error, and Editor.exe exits with EditorExitCode::BadArguments.
namespace launch
{
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
