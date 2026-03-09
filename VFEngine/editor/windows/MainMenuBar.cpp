#include "MainMenuBar.hpp"
#include "lighting/IBLWindow.hpp"
#include "config/EditorCameraWindow.hpp"
#include "config/CullingStatsWindow.hpp"
#include "import/ImportModalDialog.hpp"
#include "config/PhysicsConfigWindow.hpp"
#include "config/AudioConfigWindow.hpp"
#include "config/RenderConfigWindow.hpp"
#include "config/ProjectSettingsWindow.hpp"
#include "terrain/TerrainCreationWindow.hpp"
#include "WaterEditorWindow.hpp"
#include "config/PostProcessConfigWindow.hpp"
#include "config/NavmeshWindow.hpp"
#include "lighting/LightBakeWindow.hpp"
#include "config/GIConfigWindow.hpp"
#include "AssetLifecycleWindow.hpp"
#include "WorldSectorWindow.hpp"
#include "VFXDebugWindow.hpp"
#include "AnimationDebugWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/project/ExportEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include <imgui.h>
#include <filesystem>

namespace windows
{
    MainMenuBar::MainMenuBar()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneLoadedToken = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification& n) {
                currentSceneName = std::filesystem::path(n.scenePath).stem().string();
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&) {
                currentSceneName.clear();
            });
    }

    MainMenuBar::~MainMenuBar()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (sceneLoadedToken.isValid())
            dispatcher.unsubscribe(sceneLoadedToken);
        if (sceneClearedToken.isValid())
            dispatcher.unsubscribe(sceneClearedToken);
    }

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
            ImGui::Separator();

            {
                bool canExport = events::EventDispatcher::instance().query(events::gameExport::CanExportQuery{});
                ImGui::BeginDisabled(!canExport);
                if (ImGui::MenuItem("Export Game..."))
                {
                    std::string outputDir = fileDialog.selectFolderDialog();
                    if (!outputDir.empty())
                    {
                        events::gameExport::ExportGameCommand cmd;
                        cmd.outputDirectory = outputDir;
                        events::EventDispatcher::instance().execute(cmd);
                    }
                }
                ImGui::EndDisabled();

                if (!canExport && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("Load a project before exporting");
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("World Sectors"))
            {
                if (worldSectorWindow)
                {
                    worldSectorWindow->show();
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit"))
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
            else if (ImGui::MenuItem("Light Bake"))
            {
                if (lightBakeWindow)
                {
                    lightBakeWindow->show();
                }
            }
            else if (ImGui::MenuItem("Global Illumination"))
            {
                if (giConfigWindow)
                {
                    giConfigWindow->show();
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
                bool newState = !showNavmeshDebug;
                events::render::SetShowNavmeshDebugCommand cmd;
                cmd.show = newState;
                dispatcher.execute(cmd);

                if (newState)
                {
                    bool hasNavmesh = dispatcher.query(events::navmesh::HasNavmeshQuery{});
                    if (hasNavmesh)
                    {
                        auto debugMesh = dispatcher.query(events::navmesh::GetNavmeshDebugMeshQuery{});
                        if (!debugMesh.vertices.empty() && !debugMesh.indices.empty())
                        {
                            events::render::UpdateNavmeshDebugMeshCommand updateCmd;
                            updateCmd.vertices = std::move(debugMesh.vertices);
                            updateCmd.indices = std::move(debugMesh.indices);
                            dispatcher.execute(updateCmd);
                        }
                    }
                }
                else
                {
                    events::render::ClearNavmeshDebugMeshCommand clearCmd;
                    dispatcher.execute(clearCmd);
                }
            }

            bool cullingVisible = cullingStatsWindow ? cullingStatsWindow->isVisible() : false;
            if (ImGui::MenuItem("Culling Stats", nullptr, cullingVisible))
            {
                if (cullingStatsWindow)
                {
                    cullingStatsWindow->toggle();
                }
            }

            if (ImGui::MenuItem("Asset Lifecycle"))
            {
                if (assetLifecycleWindow)
                {
                    assetLifecycleWindow->show();
                }
            }

            if (ImGui::MenuItem("VFX Stats"))
            {
                if (vfxDebugWindow)
                {
                    vfxDebugWindow->show();
                }
            }

            if (ImGui::MenuItem("Animation Stats"))
            {
                if (animationDebugWindow)
                {
                    animationDebugWindow->show();
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

        if (!currentSceneName.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("%s", currentSceneName.c_str());
        }
    }
}
