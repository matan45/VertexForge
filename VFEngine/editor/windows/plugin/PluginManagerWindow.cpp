#include "PluginManagerWindow.hpp"
#include "core/PluginManager.hpp"
#include "core/PluginScaffolder.hpp"
#include "api/PluginVersion.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

namespace windows
{
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
            ImGui::SetTooltip("Scaffold a ready-to-build plugin project under plugins/");

        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            needsRefresh = true;

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

        auto pluginsDir = pm->getPluginsDirectory();
        if (pluginsDir.empty() || !std::filesystem::exists(pluginsDir))
            return;

        // Build set of loaded plugin names
        std::unordered_map<std::string, const plugin::LoadedPlugin*> loadedByName;
        for (const auto& loaded : pm->getLoadedPlugins())
        {
            loadedByName[loaded.descriptor.name] = &loaded;
        }

        // Scan all .vfplugin files
        for (const auto& file : std::filesystem::recursive_directory_iterator(pluginsDir))
        {
            if (!file.is_regular_file() || file.path().extension() != ".vfplugin")
                continue;

            auto desc = plugin::PluginDescriptor::loadFromFile(file.path());
            if (!desc.has_value())
                continue;

            PluginEntry entry;
            entry.descriptor = std::move(*desc);

            auto it = loadedByName.find(entry.descriptor.name);
            if (it != loadedByName.end())
            {
                entry.isLoaded = true;
                entry.isInitialized = it->second->initialized;
                entry.status = entry.isInitialized ? "Loaded" : "Load Failed";
            }
            else if (!entry.descriptor.enabled)
            {
                entry.status = "Disabled";
            }
            else if (entry.descriptor.apiVersion != plugin::VF_PLUGIN_API_VERSION)
            {
                entry.status = "API Mismatch";
            }
            else
            {
                entry.status = "Not Loaded";
            }

            auto overrideIt = sceneOverrides.find(entry.descriptor.name);
            if (overrideIt != sceneOverrides.end())
                entry.sceneOverride = overrideIt->second ? SceneOverride::On : SceneOverride::Off;

            entries.push_back(std::move(entry));
        }

        // Sort: loaded first, then by loadOrder
        std::sort(entries.begin(), entries.end(), [](const PluginEntry& a, const PluginEntry& b)
        {
            if (a.isLoaded != b.isLoaded)
                return a.isLoaded > b.isLoaded;
            return a.descriptor.loadOrder < b.descriptor.loadOrder;
        });
    }

    void PluginManagerWindow::drawPluginEntry(PluginEntry& entry)
    {
        ImGui::PushID(entry.descriptor.name.c_str());

        // VK-1365: reflect the live per-scene state on initialized plugins
        auto* pm = plugin::PluginManager::getActive();
        const bool sceneInactive = entry.isInitialized && pm &&
                                   !pm->isPluginActive(entry.descriptor.name);

        // Status color
        ImVec4 statusColor;
        if (sceneInactive)
            statusColor = ImVec4(0.9f, 0.6f, 0.2f, 1.0f); // orange
        else if (entry.isInitialized)
            statusColor = ImVec4(0.3f, 0.8f, 0.3f, 1.0f); // green
        else if (!entry.descriptor.enabled)
            statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // gray
        else
            statusColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f); // red

        // Header with status
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));

        std::string headerLabel = entry.descriptor.name + " v" + entry.descriptor.version;
        bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::PopStyleColor(3);

        // Status badge on the right
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 200.0f);
        ImGui::TextColored(statusColor, "[%s]", sceneInactive ? "Inactive" : entry.status.c_str());

        // Enable/disable checkbox
        ImGui::SameLine();
        bool enabled = entry.descriptor.enabled;
        if (ImGui::Checkbox("##enabled", &enabled))
        {
            writeEnabledState(entry, enabled);
            entry.descriptor.enabled = enabled;
            if (!enabled)
                entry.status = "Disabled";
            else if (entry.isLoaded)
                entry.status = entry.isInitialized ? "Loaded" : "Load Failed";
            else
                entry.status = "Not Loaded";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable/disable this plugin (takes effect on next launch)");

        // VK-1365: per-scene override combo
        ImGui::SameLine();
        drawSceneOverrideCombo(entry);

        if (open)
        {
            ImGui::Indent(16.0f);

            if (!entry.descriptor.author.empty())
                ImGui::Text("Author: %s", entry.descriptor.author.c_str());

            if (!entry.descriptor.description.empty())
            {
                ImGui::TextWrapped("Description: %s", entry.descriptor.description.c_str());
            }

            ImGui::Text("API Version: %u", entry.descriptor.apiVersion);
            ImGui::Text("Load Order: %d", entry.descriptor.loadOrder);

            if (!entry.descriptor.capabilities.empty())
            {
                std::string caps;
                for (const auto& cap : entry.descriptor.capabilities)
                {
                    if (!caps.empty()) caps += ", ";
                    caps += cap;
                }
                ImGui::Text("Capabilities: %s", caps.c_str());
            }

            if (!entry.descriptor.dependencies.empty())
            {
                std::string deps;
                for (const auto& dep : entry.descriptor.dependencies)
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

            ImGui::Text("Library: %s", entry.descriptor.library.c_str());

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
                pmMutable->setPluginActive(entry.descriptor.name, effective);
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
            if (entry.sceneOverride != SceneOverride::Inherit)
                cmd.settings[entry.descriptor.name] = (entry.sceneOverride == SceneOverride::On);
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
            ImGui::BulletText("Run: premake5 vs2022   (the project is auto-discovered)");
            ImGui::BulletText("Build the new project (same config as the Editor)");
            ImGui::BulletText("Restart the editor - plugins load at startup");
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
            if (!plugin::scaffold::isValidName(newPluginName))
                nameError = "Use a PascalCase identifier (letters/digits, starts uppercase)";
            else if (pm && std::filesystem::exists(pm->getPluginsDirectory() / newPluginName))
                nameError = std::string("plugins/") + newPluginName + "/ already exists";
        }
        if (!nameError.empty())
            ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "%s", nameError.c_str());

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

            const auto pluginsDir = pm->getPluginsDirectory();
            std::string error = plugin::scaffold::createPlugin(pluginsDir, opts);
            if (error.empty())
            {
                newPluginSucceeded = true;
                newPluginResult = (pluginsDir / opts.name).string();
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
    }

    void PluginManagerWindow::writeEnabledState(PluginEntry& entry, bool enabled)
    {
        auto vfpluginPath = entry.descriptor.descriptorPath;

        if (vfpluginPath.empty() || !std::filesystem::exists(vfpluginPath))
            return;

        // Read, modify, write
        std::ifstream inFile(vfpluginPath);
        if (!inFile.is_open())
            return;

        nlohmann::json json;
        try
        {
            inFile >> json;
        }
        catch (...)
        {
            return;
        }
        inFile.close();

        json["enabled"] = enabled;

        // Atomic write: write to temp file, then rename over original
        auto tempPath = vfpluginPath;
        tempPath += ".tmp";

        std::ofstream outFile(tempPath);
        if (!outFile.is_open())
            return;

        outFile << json.dump(4);
        outFile.close();

        std::error_code ec;
        std::filesystem::rename(tempPath, vfpluginPath, ec);
        if (ec)
        {
            // Rename failed — fall back to direct overwrite
            std::filesystem::remove(tempPath, ec);
            std::ofstream fallback(vfpluginPath);
            if (fallback.is_open())
                fallback << json.dump(4);
        }
    }
}
