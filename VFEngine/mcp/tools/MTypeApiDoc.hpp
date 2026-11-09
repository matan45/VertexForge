#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The mType API documentation served as vf://docs/mtype-api (VK-1652): an embedded
// primer plus signatures parsed live from the project's scripts/lib, so the docs
// always match the API the project compiles against.
namespace mcp::tools::mtypedoc
{
    // One class or interface and its visible members, each a normalised one-line
    // signature ending in ';' (e.g. "public static function zero(): Vec3f;").
    struct ApiType
    {
        std::string name;
        std::string header;  // e.g. "public value class Vec3f"
        bool isInterface = false;
        std::vector<std::string> members;
    };

    // Pure. Replaces // and /* */ comments with spaces (line breaks kept); string
    // literals are left intact, so "http://x" is not a comment.
    std::string stripComments(std::string_view source);

    // Pure. Every class / interface not marked private or protected, with its
    // public fields, constants, constructors and functions (interface methods need
    // no modifier). Multi-line signatures are joined; bodies are skipped.
    std::vector<ApiType> parseApi(std::string_view source);

    // Pure. parseApi as markdown: one ```mtype block per type.
    std::string extractApi(std::string_view source);

    // The embedded primer (markdown).
    const std::string& primer();

    // Module ids (relative to scripts/lib, no extension) whose full signatures are
    // inlined in vf://docs/mtype-api.
    const std::vector<std::string>& coreModules();

    // Pure. True for ids like "engine/Physics" or "math/Quaternion": [A-Za-z0-9_/],
    // no leading, trailing or doubled '/'.
    bool isValidModuleId(std::string_view module);

    // Worker-safe (filesystem only). Primer + core module signatures + a catalogue
    // of every other lib module. Without <scriptsRoot>/lib the primer is returned
    // with a "signatures unavailable" note.
    std::string buildMTypeApiDoc(const std::filesystem::path& scriptsRoot);

    // Worker-safe. Signatures of one lib module. A trailing ".mt" is accepted.
    // Throws ArgError for a malformed id and ResourceNotFound for an unknown module.
    std::string buildModuleDoc(const std::filesystem::path& scriptsRoot, const std::string& module);
}
