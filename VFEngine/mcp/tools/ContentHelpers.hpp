#pragma once

#include "print/LogEntry.hpp"

#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Read-side helpers shared by the tools and the MCP resources (VK-1652). Each is
// defined beside the tool it was extracted from, so both paths stay identical.
namespace mcp::tools
{
    // ------------------------------------------------------------------
    // Scene (SceneTools.cpp). Main thread only.
    // ------------------------------------------------------------------

    // The scene_get_hierarchy result {root, entityCount, returned, entities}.
    // maxDepth -1 = unlimited.
    nlohmann::json buildSceneHierarchy(int maxDepth, bool includeTransforms = true);

    // ------------------------------------------------------------------
    // Logs (LogTools.cpp). Any thread: only takes the console buffer mutex.
    // ------------------------------------------------------------------

    struct LogSelection
    {
        std::vector<util::LogEntry> entries;  // oldest first
        std::optional<uint64_t> lastSeq;
        bool truncated = false;
    };

    // With sinceSeq: matching entries after it, oldest first, up to `max`. Without:
    // the newest `max` matches. `containsLower` is a lower-cased substring filter
    // ("" = none). `max` must be >= 1.
    LogSelection collectLogs(std::optional<uint64_t> sinceSeq, util::LogLevel minLevel, std::size_t max,
                             const std::string& containsLower = {});

    // "trace" | "debug" | "info" | "warning" | "error".
    const char* logLevelName(util::LogLevel level);

    // ------------------------------------------------------------------
    // Scripts (ScriptTools.cpp)
    // ------------------------------------------------------------------

    // Main thread only. <asset root>/scripts; throws std::runtime_error when no
    // project is loaded.
    std::filesystem::path scriptsRootOnMain();

    enum class ScriptReadStatus
    {
        Ok,
        NotFound,
        Unreadable,  // file_size failed or larger than 1 MB
        OpenFailed
    };

    struct ScriptReadResult
    {
        ScriptReadStatus status = ScriptReadStatus::NotFound;
        std::string path;    // scripts-root-relative, '/' separated (Ok only)
        std::string source;  // (Ok only)
    };

    // Any thread (filesystem only). `relativePath` is UTF-8, relative to `root`
    // (lib/ allowed). Throws ArgError when it escapes `root` or is not a .mt file.
    ScriptReadResult readScript(const std::filesystem::path& root, const std::string& relativePath);

    // Any thread. Scripts-root-relative paths of game/**/*.mt, sorted, at most `cap`.
    std::vector<std::string> listGameScripts(const std::filesystem::path& root, std::size_t cap,
                                             bool* truncated = nullptr);

    // ------------------------------------------------------------------
    // Plugin components (PluginComponentTools.cpp). Main thread only.
    // ------------------------------------------------------------------

    // GetPluginComponentTypesQuery as a JSON array (component_list_types 'plugin').
    // Throws when no handler is registered.
    nlohmann::json pluginComponentTypes();
}
