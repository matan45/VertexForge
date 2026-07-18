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

        // The live component entry backing this row. Shared by the priority field on this line and
        // the tick-governor block below it, so the row is only looked up once.
        components::ScriptEntry* entry = nullptr;
        {
            auto enttEntity = services::internal::fromHandle(handle);
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::ScriptComponent>(enttEntity))
            {
                auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
                entry = scriptComp.findByPath(scriptPath);
            }
        }

        // Input priority
        if (entry)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::InputInt("##Priority", &entry->inputPriority, 0, 0))
            {
                // Propagate to running instance if loaded
                if (entry->instanceId != 0)
                {
                    events::scripting::SetInstancePriorityCommand cmd;
                    cmd.instanceId = entry->instanceId;
                    cmd.priority = entry->inputPriority;
                    dispatcher.execute(cmd);
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Input Priority (higher = handles input first)");
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

        // VK-1536 tick governor. Collapsed by default — this is opt-in tuning, not everyday state,
        // and the defaults (interval 0) mean the script ticks every frame exactly as before.
        // No scriptListDirty needed for any of these: the cached update list is sorted on
        // inputPriority alone, which these fields do not affect.
        if (entry)
        {
            ImGui::Indent(10.0f);
            if (ImGui::TreeNodeEx("Tick Governor", ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::DragFloat("Update Interval (s)", &entry->updateInterval, 0.005f, 0.0f, 10.0f, "%.3f"))
                {
                    entry->updateInterval = entry->updateInterval < 0.0f ? 0.0f : entry->updateInterval;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Seconds between onUpdate calls. 0 = every frame (default).\n\n"
                        "The script is handed the ACCUMULATED deltaTime and MUST integrate\n"
                        "against it. Do NOT throttle a script that polls input each frame\n"
                        "(Input::isKeyDown) — it will miss presses shorter than the interval.");
                }

                ImGui::SetNextItemWidth(120.0f);
                ImGui::DragFloat("Significance", &entry->tickSignificance, 0.05f, 0.01f, 100.0f, "%.2f");
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Weight for distance rate-scaling (score = significance / distance^2).\n"
                        "Higher = keeps ticking at its full rate further from the camera.\n"
                        "Only used when distance scaling is enabled; ignored if Interval is 0.");
                }

                ImGui::Checkbox("Pin Full Rate", &entry->pinFullRate);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Exempt this script from DISTANCE scaling; the Update Interval above\n"
                        "still applies. Use for scripts whose pacing must not vary with camera\n"
                        "distance (movement, root motion, timers).");
                }
                ImGui::TreePop();
            }
            ImGui::Unindent(10.0f);
        }

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
