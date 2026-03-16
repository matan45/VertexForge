#include "ScriptDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <imgui.h>

namespace windows::details
{
    bool ScriptDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scripting::GetScriptDataQuery scriptQuery;
        scriptQuery.entity = handle;
        auto scriptOpt = dispatcher.query(scriptQuery);

        if (!scriptOpt.has_value())
        {
            return false;
        }

        events::scripting::GetScriptPathsQuery pathsQuery;
        pathsQuery.entity = handle;
        auto scriptPaths = dispatcher.query(pathsQuery);

        ImGui::PushID("ScriptComponent");

        bool removeAll = false;
        std::string scriptToRemove;
        bool isOpen = drawHeader(scriptPaths.size(), removeAll);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            for (size_t i = 0; i < scriptPaths.size(); ++i)
            {
                std::string removed = drawScriptEntry(handle, scriptPaths[i], static_cast<int>(i));
                if (!removed.empty())
                {
                    scriptToRemove = removed;
                }
            }

            if (scriptPaths.empty())
            {
                ImGui::TextDisabled("No scripts attached");
            }

            ImGui::Spacing();
            drawAddScriptButton(handle);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        handleScriptRemoval(handle, scriptToRemove, removeAll, scriptPaths);

        return true;
    }

    bool ScriptDrawer::drawHeader(size_t scriptCount, bool& outRemoveAll)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##ScriptHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Scripts (%zu)", scriptCount);

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveAllScripts", ImVec2(18, 18)))
        {
            outRemoveAll = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    std::string ScriptDrawer::drawScriptEntry(services::EntityHandle handle,
                                              const std::string& scriptPath,
                                              int index)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        std::string removedScript;

        ImGui::PushID(index);

        // Extract filename from path
        std::string filename = scriptPath;
        auto lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos)
        {
            filename = filename.substr(lastSlash + 1);
        }

        // Enabled checkbox
        events::scripting::IsScriptEnabledQuery enabledQuery;
        enabledQuery.entity = handle;
        enabledQuery.scriptPath = scriptPath;
        bool isEnabled = dispatcher.query(enabledQuery);

        if (ImGui::Checkbox("##ScriptEnabled", &isEnabled))
        {
            events::scripting::SetScriptEnabledCommand cmd;
            cmd.entity = handle;
            cmd.scriptPath = scriptPath;
            cmd.enabled = isEnabled;
            dispatcher.execute(cmd);
        }

        // Filename with tooltip
        ImGui::SameLine();
        ImGui::Text("%s", filename.c_str());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", scriptPath.c_str());
        }

        // Input priority
        {
            auto enttEntity = services::internal::fromHandle(handle);
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::ScriptComponent>(enttEntity))
            {
                auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
                auto* entry = scriptComp.findByPath(scriptPath);
                if (entry)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(60.0f);
                    ImGui::InputInt("##Priority", &entry->inputPriority, 0, 0);
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Input Priority (higher = handles input first)");
                    }
                }
            }
        }

        // Remove button
        ImGui::SameLine();
        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveScript", ImVec2(16, 16)))
        {
            removedScript = scriptPath;
        }
        EntityDetailsPanel::popRemoveButtonStyle();

        ImGui::PopID();

        return removedScript;
    }

    void ScriptDrawer::drawAddScriptButton(services::EntityHandle handle)
    {
        if (!ImGui::Button("Add Script"))
        {
            return;
        }

        nfd::FileDialog fileDialog;
        std::string path = fileDialog.openFileDialog(
            {{L"mType Script Files (*.mt)", L"*.mt"}});

        if (!path.empty())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scripting::AttachScriptCommand attachCmd;
            attachCmd.entity = handle;
            attachCmd.data.scriptPath = path;
            attachCmd.data.enabled = true;
            dispatcher.execute(attachCmd);
        }
    }

    void ScriptDrawer::handleScriptRemoval(services::EntityHandle handle,
                                           const std::string& scriptToRemove,
                                           bool removeAll,
                                           const std::vector<std::string>& scriptPaths)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (!scriptToRemove.empty())
        {
            events::scripting::DetachScriptCommand cmd;
            cmd.entity = handle;
            cmd.scriptPath = scriptToRemove;
            dispatcher.execute(cmd);
        }

        if (removeAll)
        {
            for (const auto& scriptPath : scriptPaths)
            {
                events::scripting::DetachScriptCommand cmd;
                cmd.entity = handle;
                cmd.scriptPath = scriptPath;
                dispatcher.execute(cmd);
            }

            if (scriptPaths.empty())
            {
                auto enttEntity = services::internal::fromHandle(handle);
                auto& registry = scene::EntityRegistry::getRegistry();
                if (registry.all_of<components::ScriptComponent>(enttEntity))
                {
                    registry.remove<components::ScriptComponent>(enttEntity);
                }
            }
        }
    }
}
