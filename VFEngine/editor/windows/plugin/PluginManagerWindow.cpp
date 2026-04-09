#include "PluginManagerWindow.hpp"
#include "core/PluginManager.hpp"
#include "api/PluginVersion.hpp"
#include <imgui.h>
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

        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 60.0f);
        if (ImGui::Button("Refresh"))
            needsRefresh = true;

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
        ImGui::TextDisabled("Changes to enabled state take effect on next editor launch");
    }

    void PluginManagerWindow::refresh()
    {
        entries.clear();

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

        // Status color
        ImVec4 statusColor;
        if (entry.isInitialized)
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
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 120.0f);
        ImGui::TextColored(statusColor, "[%s]", entry.status.c_str());

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
