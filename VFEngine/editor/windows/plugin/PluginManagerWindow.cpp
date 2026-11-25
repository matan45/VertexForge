#include "PluginManagerWindow.hpp"
#include "core/PluginManager.hpp"
#include "core/PluginScaffolder.hpp"
#include "api/PluginVersion.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "launch/UserStateLocation.hpp"
#include "launch/Utf8Path.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace windows
{
    namespace
    {
        // VK-1732: a Standalone plugin is built in the editor's own configuration (the ABI that is sure
        // to match the editor that loads it).
        const char* editorBuildConfiguration()
        {
#if defined(DEBUG)
            return "Debug";
#elif defined(VF_DEVELOPMENT)
            return "Development";
#else
            return "Release";
#endif
        }

        bool equalsIgnoringAsciiCase(const std::string& a, const std::string& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                char x = a[i];
                char y = b[i];
                if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
                if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
                if (x != y)
                    return false;
            }
            return true;
        }

        bool pathExists(const std::filesystem::path& path)
        {
            std::error_code ec;
            return !path.empty() && std::filesystem::exists(path, ec);
        }

        const char* rootTag(plugin::PluginRootKind root)
        {
            return root == plugin::PluginRootKind::Engine ? "[shipped]" : "[yours]";
        }
    }

    void PluginManagerWindow::draw()
    {
        if (!visible)
            return;

        if (needsRefresh)
        {
            refresh();
            needsRefresh = false;
        }

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Plugin Manager", &visible))
        {
            drawContent();
        }
        ImGui::End();
    }

    void PluginManagerWindow::drawContent()
    {
        if (needsRefresh)
        {
            refresh();
            needsRefresh = false;
        }

        const bool userMode = launch::processUserState().isUser();

        // Header
        ImGui::Text("Engine API Version: %u", plugin::VF_PLUGIN_API_VERSION);

        auto* pm = plugin::PluginManager::getActive();
        if (pm)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", pm->getPluginsDirectory().string().c_str());
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 165.0f);
        if (ImGui::Button("New Plugin..."))
        {
            resetNewPluginState();
            openNewPluginPopup = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(userMode
                ? "Scaffold a ready-to-build plugin project in your plugins folder"
                : "Scaffold a ready-to-build plugin project under plugins/");
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            needsRefresh = true;

        // VK-1732: an installed engine also loads the user's own plugins.
        if (pm && !pm->getUserPluginsDirectory().empty())
        {
            ImGui::TextDisabled("Your plugins: %s", launch::pathToUtf8(pm->getUserPluginsDirectory()).c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Loaded after the shipped plugins; a shipped plugin wins over yours with the same name");
        }

        drawNewPluginPopup();

        ImGui::Separator();
        ImGui::Spacing();

        if (entries.empty())
        {
            ImGui::TextDisabled("No plugins found");
        }
        else
        {
            for (auto& entry : entries)
            {
                drawPluginEntry(entry);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // VK-1365: persist per-scene overrides into the loaded scene's .vfSettings
        const bool hasScene = !currentScenePath.empty();
        if (!hasScene)
            ImGui::BeginDisabled();
        if (ImGui::Button("Save to Scene"))
            saveToScene();
        if (!hasScene)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(hasScene
                ? "Write the Scene overrides into the loaded scene's .vfSettings"
                : "No scene loaded — load or save a scene first");
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear All Overrides"))
        {
            for (auto& entry : entries)
                entry.sceneOverride = SceneOverride::Inherit;
            if (auto* pmMutable = plugin::PluginManager::getActiveMutable())
                pmMutable->resetActiveStatesToGlobal();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Revert all plugins to Inherit (use Save to Scene to persist)");

        ImGui::TextDisabled("Global toggle takes effect on next launch; Scene overrides apply live");
    }

    void PluginManagerWindow::refresh()
    {
        entries.clear();

        // VK-1365: current scene path + its plugin overrides
        auto& dispatcher = events::EventDispatcher::instance();
        currentScenePath = dispatcher.query(events::scene::GetCurrentScenePathQuery{});
        auto sceneOverrides = dispatcher.query(events::scene::GetScenePluginSettingsQuery{});

        auto* pm = plugin::PluginManager::getActive();
        if (!pm)
            return;

        // Build set of loaded plugin names
        std::unordered_map<std::string, const plugin::LoadedPlugin*> loadedByName;
        for (const auto& loaded : pm->getLoadedPlugins())
        {
            loadedByName[loaded.descriptor.name] = &loaded;
        }

        // VK-1732: every descriptor of both roots, with the state the loader gives it
        for (auto& discovered : pm->discoverPlugins())
        {
            PluginEntry entry;
            entry.plugin = std::move(discovered);
            const plugin::PluginDescriptor& descriptor = entry.plugin.descriptor;

            // A shadowed plugin never loads; the loaded plugin with its name is the winner.
            if (entry.plugin.shadowed)
            {
                entry.status = "Shadowed";
                entries.push_back(std::move(entry));
                continue;
            }

            auto it = loadedByName.find(descriptor.name);
            if (it != loadedByName.end())
            {
                entry.isLoaded = true;
                entry.isInitialized = it->second->initialized;
                entry.status = entry.isInitialized ? "Loaded" : "Load Failed";
            }
            else if (!entry.plugin.effectiveEnabled)
            {
                entry.status = "Disabled";
            }
            else if (descriptor.apiVersion != plugin::VF_PLUGIN_API_VERSION)
            {
                entry.status = "API Mismatch";
            }
            else
            {
                entry.status = "Not Loaded";
            }

            auto overrideIt = sceneOverrides.find(descriptor.name);
            if (overrideIt != sceneOverrides.end())
                entry.sceneOverride = overrideIt->second ? SceneOverride::On : SceneOverride::Off;

            entries.push_back(std::move(entry));
        }

        // Sort: loaded first, then by loadOrder (ties keep the discovery order)
        std::stable_sort(entries.begin(), entries.end(), [](const PluginEntry& a, const PluginEntry& b)
        {
            if (a.isLoaded != b.isLoaded)
                return a.isLoaded > b.isLoaded;
            return a.plugin.descriptor.loadOrder < b.plugin.descriptor.loadOrder;
        });
    }

    void PluginManagerWindow::drawPluginEntry(PluginEntry& entry)
    {
        const plugin::PluginDescriptor& descriptor = entry.plugin.descriptor;
        // Two entries share a name when one shadows the other: the descriptor path is unique.
        ImGui::PushID(launch::pathToUtf8(descriptor.descriptorPath).c_str());

        // VK-1365: reflect the live per-scene state on initialized plugins
        auto* pm = plugin::PluginManager::getActive();
        const bool shadowed = entry.plugin.shadowed;
        const bool sceneInactive = entry.isInitialized && pm &&
                                   !pm->isPluginActive(descriptor.name);

        // Status color
        ImVec4 statusColor;
        if (shadowed)
            statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // gray
        else if (sceneInactive)
            statusColor = ImVec4(0.9f, 0.6f, 0.2f, 1.0f); // orange
        else if (entry.isInitialized)
            statusColor = ImVec4(0.3f, 0.8f, 0.3f, 1.0f); // green
        else if (!entry.plugin.effectiveEnabled)
            statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // gray
        else
            statusColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f); // red

        // Header with status
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));

        std::string headerLabel = descriptor.name + " v" + descriptor.version;
        // VK-1732: with a user root, tell the shipped plugins from the user's own
        if (pm && !pm->getUserPluginsDirectory().empty())
            headerLabel += std::string(" ") + rootTag(entry.plugin.root);
        bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::PopStyleColor(3);

        // Status badge on the right
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 200.0f);
        ImGui::TextColored(statusColor, "[%s]", sceneInactive ? "Inactive" : entry.status.c_str());

        // Enable/disable checkbox (a shadowed plugin never loads, so it has nothing to toggle)
        ImGui::SameLine();
        bool enabled = entry.plugin.effectiveEnabled;
        if (shadowed)
            ImGui::BeginDisabled();
        if (ImGui::Checkbox("##enabled", &enabled))
        {
            auto* pmMutable = plugin::PluginManager::getActiveMutable();
            const std::string error = pmMutable
                ? pmMutable->setPluginEnabled(entry.plugin, enabled)
                : std::string("The plugin manager is not running");
            if (error.empty())
            {
                entry.error.clear();
                entry.plugin.effectiveEnabled = enabled;
                if (entry.plugin.root == plugin::PluginRootKind::Engine && pmMutable->usesEnabledOverrides())
                    entry.plugin.hasOverride = true;
                else
                    entry.plugin.descriptor.enabled = enabled;
                if (!enabled)
                    entry.status = "Disabled";
                else if (entry.isLoaded)
                    entry.status = entry.isInitialized ? "Loaded" : "Load Failed";
                else
                    entry.status = "Not Loaded";
            }
            else
            {
                entry.error = error;
            }
        }
        if (shadowed)
            ImGui::EndDisabled();
        if (shadowed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Never loads: the %s plugin with this name wins\n%s\nRename this plugin (its name, DLL and folder) to load it",
                              entry.plugin.shadowedByRoot == plugin::PluginRootKind::Engine ? "shipped" : "other",
                              launch::pathToUtf8(entry.plugin.shadowedBy).c_str());
        }
        else if (!shadowed && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Enable/disable this plugin (takes effect on next launch)");
        }

        // VK-1365: per-scene override combo
        ImGui::SameLine();
        drawSceneOverrideCombo(entry);

        // VK-1732: a shipped plugin's choice lives in the overrides file; Reset hands it back to the
        // shipped descriptor.
        if (pm && pm->usesEnabledOverrides() && entry.plugin.root == plugin::PluginRootKind::Engine &&
            entry.plugin.hasOverride && !shadowed)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset"))
            {
                auto* pmMutable = plugin::PluginManager::getActiveMutable();
                const std::string error = pmMutable
                    ? pmMutable->resetPluginEnabled(descriptor.name)
                    : std::string("The plugin manager is not running");
                if (error.empty())
                    needsRefresh = true; // re-reads the shipped flag
                else
                    entry.error = error;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Use the shipped setting again (%s; takes effect on next launch)",
                                  descriptor.enabled ? "enabled" : "disabled");
        }

        if (!entry.error.empty())
            ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "%s", entry.error.c_str());

        if (open)
        {
            ImGui::Indent(16.0f);

            if (!descriptor.author.empty())
                ImGui::Text("Author: %s", descriptor.author.c_str());

            if (!descriptor.description.empty())
            {
                ImGui::TextWrapped("Description: %s", descriptor.description.c_str());
            }

            ImGui::Text("API Version: %u", descriptor.apiVersion);
            ImGui::Text("Load Order: %d", descriptor.loadOrder);

            if (!descriptor.capabilities.empty())
            {
                std::string caps;
                for (const auto& cap : descriptor.capabilities)
                {
                    if (!caps.empty()) caps += ", ";
                    caps += cap;
                }
                ImGui::Text("Capabilities: %s", caps.c_str());
            }

            if (!descriptor.dependencies.empty())
            {
                std::string deps;
                for (const auto& dep : descriptor.dependencies)
                {
                    if (!deps.empty()) deps += ", ";
                    deps += dep;
                }
                ImGui::Text("Dependencies: %s", deps.c_str());
            }
            else
            {
                ImGui::TextDisabled("Dependencies: none");
            }

            ImGui::Text("Library: %s", descriptor.library.c_str());

            ImGui::Unindent(16.0f);
            ImGui::Spacing();
        }

        ImGui::PopID();
    }

    void PluginManagerWindow::drawSceneOverrideCombo(PluginEntry& entry)
    {
        static const char* overrideLabels[] = {"Inherit", "On", "Off"};
        int current = static_cast<int>(entry.sceneOverride);

        // Per-scene control only makes sense for plugins that are actually loaded;
        // a globally-disabled plugin's DLL is never loaded, so a stored override
        // is retained in the file but inert at runtime.
        const bool canOverride = entry.isInitialized;
        if (!canOverride)
            ImGui::BeginDisabled();

        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::Combo("##sceneOverride", &current, overrideLabels, 3))
        {
            entry.sceneOverride = static_cast<SceneOverride>(current);

            // Apply live so the editor reflects the choice immediately
            // (Inherit == global flag, which is enabled for any loaded plugin).
            if (auto* pmMutable = plugin::PluginManager::getActiveMutable())
            {
                bool effective = entry.sceneOverride != SceneOverride::Off;
                pmMutable->setPluginActive(entry.plugin.descriptor.name, effective);
            }
        }

        if (!canOverride)
            ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(canOverride
                ? "Active in this scene: Inherit follows the global flag;\nOn/Off override it for this scene (Save to Scene to persist)"
                : "Per-scene control needs the plugin loaded —\nenable it globally and relaunch the editor");
        }
    }

    void PluginManagerWindow::saveToScene()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::SetScenePluginSettingsCommand cmd;
        for (const auto& entry : entries)
        {
            // A shadowed entry shares its name with the plugin that loads: that one's choice counts.
            if (entry.plugin.shadowed)
                continue;
            if (entry.sceneOverride != SceneOverride::Inherit)
                cmd.settings[entry.plugin.descriptor.name] = (entry.sceneOverride == SceneOverride::On);
        }
        dispatcher.execute(cmd);

        std::string path = dispatcher.query(events::scene::GetCurrentScenePathQuery{});
        if (path.empty())
            return;

        events::scene::SaveSceneCommand saveCmd;
        saveCmd.filePath = path;
        dispatcher.execute(saveCmd);
    }

    // VK-1284: scaffolding modal — generates plugins/<Name>/ with the three
    // authored files (premake5.lua, <Name>.cpp, <Name>.vfplugin). The loader
    // only scans at startup, so the success page spells out the next steps.
    // VK-1732: an installed engine makes a Standalone project in the user's
    // plugins folder instead, built against the engine's sdk/.
    void PluginManagerWindow::drawNewPluginPopup()
    {
        // Index must match newPluginCaps; values are the descriptor strings
        // (plugin::capability constants).
        static const char* capabilityLabels[capabilityCount] = {
            "graphics", "input", "editor", "scripting", "physics",
            "terrain", "audio", "navmesh", "vfx", "import"
        };
        constexpr int editorCapIndex = 2;

        if (openNewPluginPopup)
        {
            ImGui::OpenPopup("New Plugin");
            openNewPluginPopup = false;
        }

        if (!ImGui::BeginPopupModal("New Plugin", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        if (newPluginSucceeded)
        {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Plugin created:");
            ImGui::TextUnformatted(newPluginResult.c_str());
            ImGui::Spacing();
            ImGui::TextUnformatted("Next steps:");
            if (newPluginStandalone)
            {
                ImGui::BulletText("In that folder, run: premake5 vs2022");
                ImGui::BulletText("Close the editor, then build %s.sln (%s|x64)", newPluginName,
                                  newPluginConfiguration.c_str());
                ImGui::BulletText("Start the editor again - it loads %s.dll from that folder", newPluginName);
                ImGui::TextDisabled("Needs Visual Studio 2026 (toolset v145), premake5 and the Vulkan SDK.");
            }
            else
            {
                ImGui::BulletText("Run: premake5 vs2022   (the project is auto-discovered)");
                ImGui::BulletText("Build the new project (same config as the Editor)");
                ImGui::BulletText("Restart the editor - plugins load at startup");
            }
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(100, 0)))
            {
                resetNewPluginState();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        auto* pm = plugin::PluginManager::getActive();
        const launch::UserStateLocation& userState = launch::processUserState();
        const bool userMode = userState.isUser();

        ImGui::Text("Name:");
        ImGui::SameLine(70.0f);
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("##newPluginName", newPluginName, sizeof(newPluginName));

        ImGui::Text("Author:");
        ImGui::SameLine(70.0f);
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("##newPluginAuthor", newPluginAuthor, sizeof(newPluginAuthor));

        // Live validation
        std::string nameError;
        if (newPluginName[0] != '\0')
        {
            const std::string name = newPluginName;
            if (!plugin::scaffold::isValidName(newPluginName))
                nameError = "Use a PascalCase identifier (letters/digits, starts uppercase)";
            else if (userMode)
            {
                // VK-1732: the new plugin must clash with nothing the editor loads or the exporter ships:
                // a same-named plugin would be shadowed, a same-named folder would collide in a game's
                // plugins/ folder.
                if (pathExists(userState.userPluginsDir / name))
                    nameError = launch::pathToUtf8(userState.userPluginsDir / name) + " already exists";
                else if (pathExists(userState.engineRoot / "plugins" / name) ||
                         (pm && pathExists(pm->getLoadConfig().engineRoot / name)))
                    nameError = "The engine ships a plugins/" + name + " folder";
                else
                {
                    for (const auto& entry : entries)
                    {
                        if (equalsIgnoringAsciiCase(entry.plugin.descriptor.name, name))
                        {
                            nameError = "A plugin named " + entry.plugin.descriptor.name + " already exists (" +
                                        launch::pathToUtf8(entry.plugin.descriptor.descriptorPath) + ")";
                            break;
                        }
                    }
                }
            }
            else if (pm && std::filesystem::exists(pm->getPluginsDirectory() / newPluginName))
                nameError = std::string("plugins/") + newPluginName + "/ already exists";
        }
        if (!nameError.empty())
            ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "%s", nameError.c_str());

        if (userMode)
            ImGui::TextDisabled("In %s", launch::pathToUtf8(userState.userPluginsDir).c_str());

        ImGui::Spacing();
        ImGui::TextUnformatted("Capabilities:");
        for (int i = 0; i < capabilityCount; ++i)
        {
            if (i % 5 != 0)
                ImGui::SameLine(70.0f + static_cast<float>(i % 5) * 90.0f);
            const bool forced = newPluginEditorWindow && i == editorCapIndex;
            if (forced)
            {
                newPluginCaps[i] = true;
                ImGui::BeginDisabled();
            }
            ImGui::Checkbox(capabilityLabels[i], &newPluginCaps[i]);
            if (forced)
                ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::Checkbox("Example component registration", &newPluginComponent);
        ImGui::Checkbox("Editor window example", &newPluginEditorWindow);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Adds an ImguiWindow panel (links imgui, requires the editor capability)");

        if (!newPluginResult.empty())
            ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "%s", newPluginResult.c_str());

        ImGui::Spacing();
        const bool canCreate = pm && newPluginName[0] != '\0' && nameError.empty();
        if (!canCreate)
            ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(100, 0)))
        {
            plugin::scaffold::Options opts;
            opts.name = newPluginName;
            opts.author = newPluginAuthor;
            opts.withExampleComponent = newPluginComponent;
            opts.withEditorWindow = newPluginEditorWindow;
            for (int i = 0; i < capabilityCount; ++i)
            {
                if (newPluginCaps[i])
                    opts.capabilities.emplace_back(capabilityLabels[i]);
            }

            std::filesystem::path pluginsDir;
            std::string error;
            if (userMode)
            {
                // VK-1732: the user's plugins folder; the project builds against the engine's sdk/.
                pluginsDir = userState.userPluginsDir;
                opts.flavor = plugin::scaffold::Flavor::Standalone;
                opts.sdkDir = userState.engineRoot / "sdk";
                opts.configuration = editorBuildConfiguration();
                std::error_code ec;
                if (!std::filesystem::is_directory(opts.sdkDir / "include" / "VFEngine" / "plugin", ec))
                    error = "The engine's plugin SDK is missing: " + launch::pathToUtf8(opts.sdkDir);
            }
            else
            {
                pluginsDir = pm->getPluginsDirectory();
            }

            if (error.empty())
                error = plugin::scaffold::createPlugin(pluginsDir, opts);
            if (error.empty())
            {
                newPluginSucceeded = true;
                newPluginResult = userMode ? launch::pathToUtf8(pluginsDir / opts.name)
                                           : (pluginsDir / opts.name).string();
                newPluginStandalone = userMode;
                newPluginConfiguration = opts.configuration;
                needsRefresh = true; // shows up in the list as "Not Loaded"
            }
            else
            {
                newPluginResult = error;
            }
        }
        if (!canCreate)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0)))
        {
            resetNewPluginState();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void PluginManagerWindow::resetNewPluginState()
    {
        newPluginSucceeded = false;
        newPluginName[0] = '\0';
        std::snprintf(newPluginAuthor, sizeof(newPluginAuthor), "VertexForge");
        for (bool& cap : newPluginCaps)
            cap = false;
        newPluginComponent = true;
        newPluginEditorWindow = false;
        newPluginResult.clear();
        newPluginStandalone = false;
        newPluginConfiguration.clear();
    }
}
