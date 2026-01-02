#include "ScriptDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ScriptingEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <imgui.h>

namespace windows::details {

    bool ScriptDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Check if entity has ScriptComponent by checking GetScriptDataQuery
        events::scripting::GetScriptDataQuery scriptQuery;
        scriptQuery.entity = handle;
        auto scriptOpt = dispatcher.query(scriptQuery);

        // If no ScriptComponent, return false
        if (!scriptOpt.has_value())
            return false;

        // Get all script paths attached to this entity
        events::scripting::GetScriptPathsQuery pathsQuery;
        pathsQuery.entity = handle;
        auto scriptPaths = dispatcher.query(pathsQuery);

        ImGui::PushID("ScriptComponent");

        bool removeAllScripts = false;
        std::string scriptToRemove;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##ScriptHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Scripts (%zu)", scriptPaths.size());

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveAllScripts", ImVec2(18, 18)))
        {
            removeAllScripts = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            // Display each script
            for (size_t i = 0; i < scriptPaths.size(); ++i)
            {
                const auto& scriptPath = scriptPaths[i];
                ImGui::PushID(static_cast<int>(i));

                // Get script filename
                std::string filename = scriptPath;
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                {
                    filename = filename.substr(lastSlash + 1);
                }

                // Script entry with enabled checkbox and remove button
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

                ImGui::SameLine();
                ImGui::Text("%s", filename.c_str());
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", scriptPath.c_str());
                }

                ImGui::SameLine();
                EntityDetailsPanel::pushRemoveButtonStyle();
                if (ImGui::Button("x##RemoveScript", ImVec2(16, 16)))
                {
                    scriptToRemove = scriptPath;
                }
                EntityDetailsPanel::popRemoveButtonStyle();

                ImGui::PopID();
            }

            if (scriptPaths.empty())
            {
                ImGui::TextDisabled("No scripts attached");
            }

            ImGui::Spacing();

            // Add Script button
            if (ImGui::Button("Add Script"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"mType Script Files (*.mt)", L"*.mt"}});
                if (!path.empty())
                {
                    events::scripting::AttachScriptCommand attachCmd;
                    attachCmd.entity = handle;
                    attachCmd.data.scriptPath = path;
                    attachCmd.data.enabled = true;
                    dispatcher.execute(attachCmd);
                }
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        // Handle script removal (after UI rendering to avoid iterator invalidation)
        if (!scriptToRemove.empty())
        {
            events::scripting::DetachScriptCommand cmd;
            cmd.entity = handle;
            cmd.scriptPath = scriptToRemove;
            dispatcher.execute(cmd);
        }

        if (removeAllScripts)
        {
            // Detach all scripts from this entity
            for (const auto& scriptPath : scriptPaths)
            {
                events::scripting::DetachScriptCommand cmd;
                cmd.entity = handle;
                cmd.scriptPath = scriptPath;
                dispatcher.execute(cmd);
            }
            // If component was empty (no scripts), we need to remove it manually
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

        return true;
    }

}
