#pragma once

#include "asset/AssetRef.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <string_view>

// VK-1657: the asset-field helpers of the component bindings, hoisted from AnimatorBinding.cpp (VK-1656) so
// the VFX binding (VFXBinding.cpp) shares them. An asset field is exchanged as a UTF-8 path string: "" clears
// it, "guid:<hex>" names a registered asset (and is what a reference whose file cannot be found is reported
// as). Main thread: they read the project root (GetCurrentProjectQuery) and the asset database.
namespace mcp::tools
{
    // Where the file of an asset field may lie.
    enum class AssetScope
    {
        // Absolute, or relative to the project asset root (the Animator binding). Reported as the full path.
        Anywhere,
        // Inside the project asset root only (resolveInside, so ".." and links cannot escape it): an exported
        // game carries the project's assets, nothing else (the VFX binding). Reported project-relative.
        InsideProject
    };

    // A file the agent names: absolute, or relative to the project asset root (as given when no project is
    // loaded). Lexically normalised; nothing is checked on disk.
    std::filesystem::path agentAssetPath(const std::string& text);

    // An asset field: "" (or null) clears, "guid:<hex>" names a registered asset, anything else is the path
    // of an existing file with `extension`. The file is checked before AssetRef::fromPath, which would
    // register a missing path. A GUID whose file cannot be found stays assignable, as assetFieldJson reports
    // it; with InsideProject a GUID whose file lies outside the project is refused. Throws ArgError naming
    // `key`; InsideProject throws std::runtime_error when no project is loaded and a path must be checked.
    asset::AssetRef assetFieldFromJson(const nlohmann::json& value, const char* key, std::string_view extension,
                                       AssetScope scope = AssetScope::Anywhere);

    // The field as assetFieldFromJson reads it back: "" for none, "guid:<hex>" when the file cannot be
    // resolved, else the path (UTF-8, '/' separators; InsideProject: relative to the project asset root when
    // it lies inside it). Engine paths are narrow strings in the process code page, so a replayed report
    // (undo) names the same file.
    nlohmann::json assetFieldJson(const asset::AssetRef& ref, AssetScope scope = AssetScope::Anywhere);
}
