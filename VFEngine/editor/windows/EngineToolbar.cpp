#include "EngineToolbar.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include <imgui.h>
#include <imgui_internal.h>

namespace windows
{
    void EngineToolbar::draw(const ImGuiViewport* viewport)
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        if (ImGui::Begin("##EngineToolbar", nullptr, flags))
        {
            drawPlayControls();

            ImGui::SameLine(0.0f, 16.0f);
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine(0.0f, 16.0f);

            drawBuildActions();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EngineToolbar::drawPlayControls()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto currentMode = dispatcher.query(events::editor::GetEditorModeQuery{});
        bool isPlayMode = (currentMode == services::EditorMode::Play);
        bool isScriptsCompiled = dispatcher.query(events::scripting::IsScriptsCompiledQuery{});

        if (!isScriptsCompiled && !isPlayMode)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[!]");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Scripts not built.\nBuild Scripts before playing.");
            ImGui::SameLine();
        }

        if (!isPlayMode)
        {
            bool isSculptMode = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
            ImGui::BeginDisabled(isSculptMode);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            if (ImGui::Button("Play", ImVec2(buttonWidth, 0)))
            {
                events::editor::SetEditorModeCommand cmd;
                cmd.mode = services::EditorMode::Play;
                dispatcher.execute(cmd);
            }
            ImGui::PopStyleColor(3);

            ImGui::EndDisabled();

            if (isSculptMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Exit Sculpt Mode before entering Play Mode");
        }
        else
        {
            bool isPaused = dispatcher.query(events::editor::IsEditorPausedQuery{});

            if (!isPaused)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.6f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.7f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.5f, 0.0f, 1.0f));
                if (ImGui::Button("Pause", ImVec2(buttonWidth, 0)))
                {
                    events::editor::SetEditorPausedCommand cmd;
                    cmd.paused = true;
                    dispatcher.execute(cmd);
                }
                ImGui::PopStyleColor(3);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
                if (ImGui::Button("Resume", ImVec2(buttonWidth, 0)))
                {
                    events::editor::SetEditorPausedCommand cmd;
                    cmd.paused = false;
                    dispatcher.execute(cmd);
                }
                ImGui::PopStyleColor(3);
            }

            ImGui::SameLine(0.0f, 4.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop", ImVec2(buttonWidth, 0)))
            {
                events::editor::SetEditorModeCommand cmd;
                cmd.mode = services::EditorMode::Edit;
                dispatcher.execute(cmd);
            }
            ImGui::PopStyleColor(3);
        }
    }

    void EngineToolbar::drawBuildActions()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool isScriptsCompiled = dispatcher.query(events::scripting::IsScriptsCompiledQuery{});

        if (ImGui::Button("Build Scripts", ImVec2(0, 0)))
        {
            dispatcher.execute(events::scripting::BuildScriptsCommand{});
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compile game scripts");

        ImGui::SameLine();

        if (isScriptsCompiled)
        {
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "OK");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "!");
        }

        ImGui::SameLine(0.0f, 12.0f);

        if (ImGui::Button("Bake NavMesh", ImVec2(0, 0)))
        {
            dispatcher.execute(events::navmesh::BakeNavmeshCommand{});
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Generate navigation mesh for AI pathfinding");
    }
}
