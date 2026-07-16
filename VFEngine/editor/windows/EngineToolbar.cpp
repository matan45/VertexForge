#include "EngineToolbar.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/EditorKeybindingEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/editor/EditorSettingsEvents.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "print/Log.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

namespace windows
{
    void EngineToolbar::draw(const ImGuiViewport* viewport)
    {
        if (!actionsRegistered)
        {
            registerHotkeys();
            actionsRegistered = true;
        }

        // Consume Play/Pause/Stop/Step hotkeys once per frame on the main thread.
        handleHotkeys();

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        if (ImGui::Begin("##EngineToolbar", nullptr, flags))
        {
            float windowWidth = ImGui::GetWindowWidth();
            // Estimate play controls plus the global-audio toggle and keep the group
            // roughly centered. Play mode can grow further when Step/time scale appear.
            float controlsWidth = 240.0f;
            float centerX = (windowWidth - controlsWidth) * 0.5f;
            if (centerX < 8.0f) centerX = 8.0f;

            ImGui::SetCursorPosX(centerX);
            drawPlayControls();

            ImGui::SameLine(0.0f, 8.0f);
            drawAudioMuteToggle();

            ImGui::SameLine(0.0f, 16.0f);
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine(0.0f, 16.0f);

            drawBuildActions();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EngineToolbar::drawAudioMuteToggle()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::audio::IsBusMutedQuery query;
        query.busName = "Master";
        const bool muted = dispatcher.query(query);

        if (muted)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        }

        const char* label = muted
            ? ICON_FA_VOLUME_XMARK "##GlobalAudioMute"
            : ICON_FA_VOLUME_HIGH "##GlobalAudioMute";
        if (ImGui::Button(label, ImVec2(30.0f, 0.0f)))
        {
            events::editor::SetEditorAudioMutedCommand command;
            command.muted = !muted;
            dispatcher.execute(command);
        }

        if (muted)
        {
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(muted
                ? "Unmute all editor audio (Play mode and previews)"
                : "Mute all editor audio (Play mode and previews)");
        }
    }

    void EngineToolbar::registerHotkeys()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Register a single-key action and warn on any binding
        // conflict instead of silently overriding another action's shortcut.
        auto reg = [&](const std::string& name, const std::string& category,
                       const std::string& display, int key, bool ctrl, bool shift)
        {
            services::InputBinding b;
            b.type = services::BindingType::Key;
            b.code = key;
            b.requireCtrl = ctrl;
            b.requireShift = shift;

            events::editor::GetKeybindingConflictsQuery conflictQuery;
            conflictQuery.actionName = name;
            conflictQuery.binding = b;
            auto conflicts = dispatcher.query(conflictQuery);
            for (const auto& c : conflicts)
                vfLogWarning("Editor hotkey '{}' conflicts with existing action '{}'", name, c.conflictingAction);

            events::editor::RegisterEditorActionCommand cmd;
            cmd.actionName = name;
            cmd.category = category;
            cmd.displayName = display;
            cmd.defaultBindings = {b};
            dispatcher.execute(cmd);
        };

        reg("Editor.Undo", "Edit", "Undo", ImGuiKey_Z, true, false);
        reg("Editor.Redo", "Edit", "Redo", ImGuiKey_Y, true, false);
        reg("Editor.Play", "Play Controls", "Play", ImGuiKey_F5, false, false);
        reg("Editor.PauseResume", "Play Controls", "Pause / Resume", ImGuiKey_F6, false, false);
        reg("Editor.Stop", "Play Controls", "Stop", ImGuiKey_F5, false, true);
        reg("Editor.Step", "Play Controls", "Step Frame", ImGuiKey_F10, false, false);
    }

    void EngineToolbar::handleHotkeys()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto isPressed = [&](const std::string& action)
        {
            events::editor::IsEditorActionPressedQuery q;
            q.actionName = action;
            return dispatcher.query(q);
        };

        auto currentMode = dispatcher.query(events::editor::GetEditorModeQuery{});
        bool isPlayMode = (currentMode == services::EditorMode::Play);

        // Undo/redo are editor-global, not tied to whichever docked window has
        // focus. Let active text widgets retain their native editing history.
        if (!ImGui::GetIO().WantTextInput)
        {
            if (isPressed("Editor.Undo"))
            {
                dispatcher.execute(events::undoredo::UndoCommand{});
            }
            else if (isPressed("Editor.Redo"))
            {
                dispatcher.execute(events::undoredo::RedoCommand{});
            }
        }

        // Play: block only on sculpt mode here. The scripts-compiled gate now lives
        // in the SetEditorModeCommand handler (Phase 4a), which auto-builds when
        // needed and refuses Play only if that build fails — so F5 benefits from
        // auto-build instead of being silently swallowed when scripts are stale.
        if (!isPlayMode && isPressed("Editor.Play"))
        {
            bool isSculptMode = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
            if (!isSculptMode)
            {
                events::editor::SetEditorModeCommand cmd;
                cmd.mode = services::EditorMode::Play;
                dispatcher.execute(cmd);
            }
        }

        // Stop: only meaningful while playing.
        if (isPlayMode && isPressed("Editor.Stop"))
        {
            events::editor::SetEditorModeCommand cmd;
            cmd.mode = services::EditorMode::Edit;
            dispatcher.execute(cmd);
        }

        // Pause / Resume: toggle, only while playing.
        if (isPlayMode && isPressed("Editor.PauseResume"))
        {
            bool isPaused = dispatcher.query(events::editor::IsEditorPausedQuery{});
            events::editor::SetEditorPausedCommand cmd;
            cmd.paused = !isPaused;
            dispatcher.execute(cmd);
        }

        // Step: advance one gameplay frame, only meaningful while playing and paused
        // (mirrors EditorModeServiceImpl::stepFrame()'s own guard).
        if (isPlayMode && isPressed("Editor.Step"))
        {
            bool isPaused = dispatcher.query(events::editor::IsEditorPausedQuery{});
            if (isPaused)
            {
                dispatcher.execute(events::editor::StepFrameCommand{});
            }
        }
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

            // Step (one gameplay frame) - only meaningful while paused.
            if (isPaused)
            {
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button("Step", ImVec2(buttonWidth, 0)))
                {
                    dispatcher.execute(events::editor::StepFrameCommand{});
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Advance one gameplay frame");
            }

            // Status label so the running/paused state is obvious from the toolbar too.
            ImGui::SameLine(0.0f, 12.0f);
            if (isPaused)
                ImGui::TextColored(ImVec4(0.90f, 0.71f, 0.24f, 1.0f), "PAUSED");
            else
                ImGui::TextColored(ImVec4(0.31f, 0.78f, 0.47f, 1.0f), "PLAYING");

            // Time-scale control (slow-mo / fast-forward) for gameplay only.
            ImGui::SameLine(0.0f, 16.0f);
            float scale = dispatcher.query(events::editor::GetTimeScaleQuery{});
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::SliderFloat("##timescale", &scale, 0.1f, 4.0f, "%.2fx"))
            {
                events::editor::SetTimeScaleCommand cmd;
                cmd.scale = scale;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Gameplay time scale (slow-mo / fast-forward)");

            // Quick presets.
            auto setScale = [&dispatcher](float s) {
                events::editor::SetTimeScaleCommand cmd;
                cmd.scale = s;
                dispatcher.execute(cmd);
            };
            ImGui::SameLine(0.0f, 6.0f);
            if (ImGui::SmallButton("0.25x")) setScale(0.25f);
            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::SmallButton("1x")) setScale(1.0f);
            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::SmallButton("2x")) setScale(2.0f);
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
