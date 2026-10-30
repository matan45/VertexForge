#pragma once

#include <optional>
#include <string>
#include <string_view>

// VK-1660: recognises an mType behavior-tree ScriptTask from its source text, without compiling it.
// Shared by the BT authoring strict check (Services) and the MCP bt_list_node_types reply.
namespace behaviortree
{
    struct ScriptTaskInfo
    {
        std::string className; // the @Script class ScriptingAdapter instantiates for the file
        // Optional hooks the class declares. The tree calls onStart once, when the task first runs for an
        // entity; onAbort() when the task is interrupted; onEnd(bool success) when tick finished it.
        bool hasOnStart = false;
        bool hasOnAbort = false;
        bool hasOnEnd = false;
        std::string summary; // the file's leading // comment block, markers stripped, lines joined by '\n'
    };

    // The file's first `@Script [public] class Name` declaration - the class ScriptingAdapter::loadScript
    // instantiates - when that class declares `function tick(float ...)` directly in its body; nullopt
    // otherwise (a later @Script class with tick does not count: the loader never instantiates it).
    // Comments and string literals, including $"..." interpolation, are ignored. Methods a base class
    // declares are not seen.
    std::optional<ScriptTaskInfo> scanScriptTask(std::string_view source);
}
