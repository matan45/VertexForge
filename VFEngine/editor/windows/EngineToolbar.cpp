#include "EngineToolbar.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
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
            float windowWidth = ImGui::GetWindowWidth();
            // Estimate play controls width: edit mode Play(60)+Debug(60), play mode
            // Pause(60)+Stop(60) ~ 130; pad to keep the group roughly centered.
            float controlsWidth = 200.0f;
            float centerX = (windowWidth - controlsWidth) * 0.5f;
            if (centerX < 8.0f) centerX = 8.0f;

            ImGui::SetCursorPosX(centerX);
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
            bool playDisabled = isSculptMode || !isScriptsCompiled;
            ImGui::BeginDisabled(playDisabled);

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
            if (!isScriptsCompiled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Build Scripts before playing");

            // VK-1371: Play with the mType debugger attached. Same gate as Play.
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::BeginDisabled(playDisabled);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.7f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.55f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.35f, 0.6f, 1.0f));
            if (ImGui::Button("Debug", ImVec2(buttonWidth, 0)))
            {
                events::editor::SetEditorModeCommand cmd;
                cmd.mode = services::EditorMode::Play;
                cmd.withDebugger = true;
                dispatcher.execute(cmd);
            }
            ImGui::PopStyleColor(3);

            ImGui::EndDisabled();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Play with the mType debugger attached\n(VS Code: Attach to localhost:5005)");
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
    }
}
