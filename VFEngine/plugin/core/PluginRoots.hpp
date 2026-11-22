#pragma once
#include "../api/PluginDescriptor.hpp"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// VK-1732: the folders plugins load from, and the Plugin Manager's choices for the shipped ones.
//
// An installed engine loads the shipped plugins (<engine>/plugins, scanned first) and the user's own
// (Documents\VertexForge\Plugins). The MSI rewrites the shipped descriptors on every upgrade, so the
// user's enabled/disabled choices for them live apart, in the per-user state folder
// (%USERPROFILE%\.vertexforge\editor\plugin-overrides.json):
//
//   {"version": 1, "enabled": {"PSDImport": true}}
//
// The dev tree has one root and no overrides file: a toggle edits the .vfplugin in place, as before.
//
// Header-only on purpose: the Tests project includes VFEngine/plugin without linking the Plugin module.
namespace plugin
{
    enum class PluginRootKind
    {
        Engine,  // the plugins shipped with the engine (the dev tree's plugins/)
        User     // the user's own plugins (an installed engine only)
    };

    // A descriptor found in one of the roots, with the state the loader gives it.
    struct DiscoveredPlugin
    {
        PluginDescriptor descriptor;                  // as on disk: descriptor.enabled is the file's own flag
        PluginRootKind root = PluginRootKind::Engine;
        bool effectiveEnabled = true;                 // the file's flag, or the override of a shipped plugin
        bool hasOverride = false;                     // a shipped plugin the overrides file names
        bool shadowed = false;                        // an earlier plugin with this name wins; never loaded
        std::filesystem::path shadowedBy;             // the winner's .vfplugin
        PluginRootKind shadowedByRoot = PluginRootKind::Engine;
    };

    inline constexpr std::int64_t pluginOverridesFormatVersion = 1;

    enum class PluginOverridesStatus
    {
        Absent,   // no file: every shipped plugin follows its descriptor
        Ok,
        Invalid,  // unusable: ignored, and the Plugin Manager refuses to overwrite it
        Newer     // a newer format: ignored, and never written
    };

    struct PluginOverrides
    {
        PluginOverridesStatus status = PluginOverridesStatus::Absent;
        std::map<std::string, bool> enabled;  // plugin name -> enabled (Ok only)
        std::string problem;                  // Invalid / Newer: why, for the log and the Plugin Manager

        // The file may be rewritten with a change (its content would not be lost).
        bool writable() const
        {
            return status == PluginOverridesStatus::Absent || status == PluginOverridesStatus::Ok;
        }
    };

    // nullopt: there is no file. A UTF-8 BOM is accepted. Never throws.
    inline PluginOverrides parsePluginOverrides(std::optional<std::string_view> text)
    {
        PluginOverrides result;
        if (!text)
        {
            return result;
        }

        const auto invalid = [&result](std::string problem)
        {
            result.status = PluginOverridesStatus::Invalid;
            result.enabled.clear();
            result.problem = std::move(problem);
            return result;
        };

        std::string_view body = *text;
        constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";
        if (body.substr(0, utf8Bom.size()) == utf8Bom)
        {
            body.remove_prefix(utf8Bom.size());
        }

        try
        {
            const nlohmann::json document = nlohmann::json::parse(body, nullptr, false);
            if (document.is_discarded() || !document.is_object())
            {
                return invalid("it is not a JSON object");
            }

            const auto version = document.find("version");
            if (version == document.end() || !version->is_number_integer())
            {
                return invalid("it has no integer \"version\"");
            }
            // nlohmann reads a non-negative integer as unsigned and a negative one as signed.
            if (!version->is_number_unsigned() || version->get<std::uint64_t>() < 1)
            {
                return invalid("its \"version\" is not a format version");
            }
            if (version->get<std::uint64_t>() > static_cast<std::uint64_t>(pluginOverridesFormatVersion))
            {
                result.status = PluginOverridesStatus::Newer;
                result.problem = "it was written by a newer editor (format version " +
                                 std::to_string(version->get<std::uint64_t>()) + ")";
                return result;
            }

            const auto enabled = document.find("enabled");
            if (enabled != document.end())
            {
                if (!enabled->is_object())
                {
                    return invalid("its \"enabled\" is not an object");
                }
                for (auto it = enabled->begin(); it != enabled->end(); ++it)
                {
                    if (!it->is_boolean())
                    {
                        return invalid("its \"enabled\" value for \"" + it.key() + "\" is not true or false");
                    }
                    result.enabled[it.key()] = it->get<bool>();
                }
            }
            result.status = PluginOverridesStatus::Ok;
            return result;
        }
        catch (const std::exception& e)
        {
            return invalid(std::string("it could not be read (") + e.what() + ")");
        }
    }

    // The file's text: "version" first, then the names in sorted order, 4-space indented, a final newline.
    // Throws nlohmann::json::type_error only for a name that is not valid UTF-8.
    inline std::string serializePluginOverrides(const std::map<std::string, bool>& enabled)
    {
        nlohmann::ordered_json members = nlohmann::ordered_json::object();
        for (const auto& [name, value] : enabled)
        {
            members[name] = value;
        }
        nlohmann::ordered_json document = nlohmann::ordered_json::object();
        document["version"] = pluginOverridesFormatVersion;
        document["enabled"] = std::move(members);
        return document.dump(4) + "\n";
    }

    // The state the loader gives a plugin. An override applies to shipped (Engine-root) plugins only: the
    // user's own plugins keep their choice in their own descriptor. An Invalid or Newer file counts as none.
    inline bool effectiveEnabled(PluginRootKind root, const std::string& name, bool descriptorEnabled,
                                 const PluginOverrides* overrides)
    {
        if (root != PluginRootKind::Engine || !overrides || overrides->status != PluginOverridesStatus::Ok)
        {
            return descriptorEnabled;
        }
        const auto it = overrides->enabled.find(name);
        return it != overrides->enabled.end() ? it->second : descriptorEnabled;
    }

    struct PluginRootCandidate
    {
        std::string name;
        PluginRootKind root = PluginRootKind::Engine;
    };

    // Two plugins with one name cannot both load (scene overrides, dependencies and the exporter key them
    // by name). The winner is the first Engine-root candidate with that name in list order, else the first
    // User-root one, so a shipped plugin always wins over a user's plugin with its name, whatever the list
    // order. Returns, per candidate, the index of the candidate that wins over it, or nullopt when it wins.
    // Names compare exactly, as the loader compares them.
    inline std::vector<std::optional<std::size_t>> findShadowed(const std::vector<PluginRootCandidate>& candidates)
    {
        std::unordered_map<std::string, std::size_t> winners;
        for (const PluginRootKind pass : {PluginRootKind::Engine, PluginRootKind::User})
        {
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                if (candidates[i].root == pass)
                {
                    winners.try_emplace(candidates[i].name, i);
                }
            }
        }

        std::vector<std::optional<std::size_t>> shadowedBy(candidates.size());
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const std::size_t winner = winners.at(candidates[i].name);
            if (winner != i)
            {
                shadowedBy[i] = winner;
            }
        }
        return shadowedBy;
    }
}
