#include "MainMenuBar.hpp"
#include "IBLWindow.hpp"
#include "EditorCameraWindow.hpp"
#include "CullingStatsWindow.hpp"
#include "ImportModalDialog.hpp"
#include "PhysicsConfigWindow.hpp"
#include "AudioConfigWindow.hpp"
#include "RenderConfigWindow.hpp"
#include "ProjectSettingsWindow.hpp"
#include "TerrainCreationWindow.hpp"
#include "WaterEditorWindow.hpp"
#include "PostProcessConfigWindow.hpp"
#include "NavmeshWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/RenderEvents.hpp"
#include "events/ApplicationEvents.hpp"
#include "events/EditorModeEvents.hpp"
#include "events/ScriptingEvents.hpp"
#include "events/SculptModeEvents.hpp"
#include <imgui.h>

namespace windows
{
    void MainMenuBar::draw()
    {
        if (importDialog)
        {
            importDialog->draw();
        }

        if (ImGui::BeginMainMenuBar())
        {
            handleFileMenu();
            handleSettingsMenu();
            handleAddMenu();
            handleScriptsMenu();
            handleDebug();
            handlePlayControls();
            ImGui::EndMainMenuBar();
        }
    }

    void MainMenuBar::handleFileMenu()
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene"))
            {
                events::scene::NewSceneCommand cmd;
                events::EventDispatcher::instance().execute(cmd);
            }
            else if (ImGui::MenuItem("Load Scene"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"VF Scene Files (*.vfScene)", L"*.vfScene"}
                };

                std::string loadPath = fileDialog.openFileDialog(fileTypes);
                if (!loadPath.empty())
                {
                    events::scene::LoadSceneCommand cmd;
                    cmd.filePath = loadPath;
                    events::EventDispatcher::instance().execute(cmd);
                }
            }
            else if (ImGui::MenuItem("Save Scene"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"VF Scene Files (*.vfScene)", L"*.vfScene"}
                };

                std::string savePath = fileDialog.saveFileDialog(fileTypes, L"vfScene");
                if (!savePath.empty())
                {
                    events::scene::SaveSceneCommand cmd;
                    cmd.filePath = savePath;
                    events::EventDispatcher::instance().execute(cmd);
                }
            }
            else if (ImGui::MenuItem("Exit"))
            {
                events::application::CloseCommand cmd;
                events::EventDispatcher::instance().execute(cmd);
            }
            ImGui::EndMenu();
        }
    }

    void MainMenuBar::handleSettingsMenu()
    {
        if (ImGui::BeginMenu("Settings"))
        {
            if (ImGui::MenuItem("Project"))
            {
                if (projectSettingsWindow)
                {
                    projectSettingsWindow->show();
                }
            }
            else if (ImGui::MenuItem("Editor Camera"))
            {
                if (editorCameraWindow)
                {
                    editorCameraWindow->show();
                }
            }
            else if (ImGui::MenuItem("Physics Config"))
            {
                if (physicsConfigWindow)
                {
                    physicsConfigWindow->show();
                }
            }
            else if (ImGui::MenuItem("Audio Config"))
            {
                if (audioConfigWindow)
                {
                    audioConfigWindow->show();
                }
            }
            else if (ImGui::MenuItem("Render Config"))
            {
                if (renderConfigWindow)
                {
                    renderConfigWindow->show();
                }
            }
            ImGui::EndMenu();
        }
    }

    void MainMenuBar::handleAddMenu()
    {
        if (ImGui::BeginMenu("Add"))
        {
            if (ImGui::MenuItem("IBL"))
            {
                if (iblWindow)
                {
                    iblWindow->show();
                }
            }
            else if (ImGui::MenuItem("Terrain"))
            {
                if (terrainCreationWindow)
                {
                    terrainCreationWindow->show();
                }
            }
            else if (ImGui::MenuItem("Water"))
            {
                if (waterEditorWindow)
                {
                    waterEditorWindow->show();
                }
            }
            else if (ImGui::MenuItem("Post Process"))
            {
                if (postProcessConfigWindow)
                {
                    postProcessConfigWindow->show();
                }
            }
            else if (ImGui::MenuItem("Navigation"))
            {
                if (navmeshWindow)
                {
                    navmeshWindow->show();
                }
            }
            ImGui::EndMenu();
        }
    }

    void MainMenuBar::handleScriptsMenu()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool isCompiled = dispatcher.query(events::scripting::IsScriptsCompiledQuery{});

        if (ImGui::BeginMenu("Scripts"))
        {
            if (ImGui::MenuItem("Build Scripts"))
            {
                dispatcher.execute(events::scripting::BuildScriptsCommand{});
            }

            if (ImGui::MenuItem("Clean Scripts"))
            {
                dispatcher.execute(events::scripting::CleanScriptsCommand{});
            }

            ImGui::Separator();

            if (isCompiled)
            {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Built");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Status: Not Built");
            }

            ImGui::EndMenu();
        }
    }

    void MainMenuBar::handleDebug()
    {
        if (ImGui::BeginMenu("Debug"))
        {
            auto& dispatcher = events::EventDispatcher::instance();

            bool showBillboards = dispatcher.query(events::render::GetShowBillboardIconsQuery{});
            if (ImGui::MenuItem("Show Billboard Icons", nullptr, showBillboards))
            {
                events::render::SetShowBillboardIconsCommand cmd;
                cmd.show = !showBillboards;
                dispatcher.execute(cmd);
            }

            bool showDebugRendering = dispatcher.query(events::render::GetShowDebugRenderingQuery{});
            if (ImGui::MenuItem("Show Debug Rendering", nullptr, showDebugRendering))
            {
                events::render::SetShowDebugRenderingCommand cmd;
                cmd.show = !showDebugRendering;
                dispatcher.execute(cmd);
            }

            bool showPhysicsDebug = dispatcher.query(events::render::GetShowPhysicsDebugQuery{});
            if (ImGui::MenuItem("Show Physics Colliders", nullptr, showPhysicsDebug))
            {
                events::render::SetShowPhysicsDebugCommand cmd;
                cmd.show = !showPhysicsDebug;
                dispatcher.execute(cmd);
            }

            bool showNavmeshDebug = dispatcher.query(events::render::GetShowNavmeshDebugQuery{});
            if (ImGui::MenuItem("Show Navmesh", nullptr, showNavmeshDebug))
            {
                events::render::SetShowNavmeshDebugCommand cmd;
                cmd.show = !showNavmeshDebug;
                dispatcher.execute(cmd);
            }

            bool cullingVisible = cullingStatsWindow ? cullingStatsWindow->isVisible() : false;
            if (ImGui::MenuItem("Culling Stats", nullptr, cullingVisible))
            {
                if (cullingStatsWindow)
                {
                    cullingStatsWindow->toggle();
                }
            }

            ImGui::EndMenu();
        }
    }

    void MainMenuBar::handlePlayControls()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto currentMode = dispatcher.query(events::editor::GetEditorModeQuery{});
        bool isScriptsCompiled = dispatcher.query(events::scripting::IsScriptsCompiledQuery{});

        float menuBarWidth = ImGui::GetWindowWidth();
        float buttonWidth = 60.0f;
        float totalWidth = buttonWidth + 10.0f; // Button + spacing for indicator
        float centerX = (menuBarWidth - totalWidth) * 0.5f;
        ImGui::SetCursorPosX(centerX);

        if (!isScriptsCompiled && currentMode == services::EditorMode::Edit)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[!]");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Scripts not built.\nGo to Scripts > Build Scripts before playing.");
            }
            ImGui::SameLine();
        }

        if (currentMode == services::EditorMode::Edit)
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
            {
                ImGui::SetTooltip("Exit Sculpt Mode before entering Play Mode");
            }
        }
        else
        {
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
}
