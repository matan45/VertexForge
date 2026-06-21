#include "MainMenuBar.hpp"
#include "config/EditorSettingsWindow.hpp"
#include "lighting/IBLWindow.hpp"
#include "config/EditorCameraWindow.hpp"
#include "config/CullingStatsWindow.hpp"
#include "import/ImportModalDialog.hpp"
#include "config/PhysicsConfigWindow.hpp"
#include "config/AudioConfigWindow.hpp"
#include "audio/AudioMixerWindow.hpp"
#include "config/RenderConfigWindow.hpp"
#include "config/ProjectSettingsWindow.hpp"
#include "terrain/TerrainCreationWindow.hpp"
#include "ocean/OceanEditorWindow.hpp"
#include "config/PostProcessConfigWindow.hpp"
#include "config/NavmeshWindow.hpp"
#include "config/GIConfigWindow.hpp"
#include "config/VolumetricFogConfigWindow.hpp"
#include "config/AtmosphereConfigWindow.hpp"
#include "config/CloudConfigWindow.hpp"
#include "weather/WeatherEditorWindow.hpp"
#include "config/EnvironmentWindow.hpp"
#include "config/LightStreamingDebugWindow.hpp"
#include "asset/AssetLifecycleWindow.hpp"
#include "world/WorldSectorWindow.hpp"
#include "vfx/VFXDebugWindow.hpp"
#include "plugin/PluginManagerWindow.hpp"
#include "imguiHandler/PluginWindowRegistry.hpp"
#include "debug/TaskGraphWindow.hpp"
#include "config/InputActionMappingWindow.hpp"
#include "animation/AnimationDebugWindow.hpp"
#include "procedural/HeightmapGeneratorWindow.hpp"
#include "imageprocessing/BackgroundRemovalWindow.hpp"
#include "animation/RetargetingEditorWindow.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "theme/ThemeEditorWindow.hpp"
#include "debug/MemoryDiagnosticsWindow.hpp"
#include "debug/RTTDebugWindow.hpp"
#include "config/EditorPreferencesWindow.hpp"
#include "export/ExportGameWindow.hpp"
#include "../handlers/EditorLayoutManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
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
            handleEditMenu();
            handleAddMenu();
            handleToolsMenu();
            handlePluginsMenu();
            handleScriptsMenu();
            handleDebug();
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
                if (ImGui::MenuItem("Export Game...") && exportGameWindow)
                {
                    exportGameWindow->show();
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

    void MainMenuBar::handleEditMenu()
    {
        if (!ImGui::BeginMenu("Edit")) return;
        if (ImGui::MenuItem("Preferences") && editorPreferencesWindow) editorPreferencesWindow->show();
        if (ImGui::MenuItem("Settings") && editorSettingsWindow) editorSettingsWindow->show();
        ImGui::EndMenu();
    }

    void MainMenuBar::handleSettingsMenu()
    {
        if (!ImGui::BeginMenu("Settings")) return;
        if (ImGui::MenuItem("Project") && editorSettingsWindow)
            editorSettingsWindow->show(EditorSettingsWindow::Project);
        if (ImGui::MenuItem("Editor Camera") && editorSettingsWindow)
            editorSettingsWindow->show(EditorSettingsWindow::EditorCamera);
        if (ImGui::MenuItem("Physics Config") && editorSettingsWindow)
            editorSettingsWindow->show(EditorSettingsWindow::Physics);
        if (ImGui::MenuItem("Audio Config") && editorSettingsWindow)
            editorSettingsWindow->show(EditorSettingsWindow::AudioConfig);
        if (ImGui::MenuItem("Audio Mixer") && editorSettingsWindow)
            editorSettingsWindow->show(EditorSettingsWindow::AudioMixer);
        if (ImGui::MenuItem("Render Config") && editorSettingsWindow)
            editorSettingsWindow->show(EditorSettingsWindow::Rendering);
        if (ImGui::MenuItem("Input Action Mapping") && editorSettingsWindow)
            editorSettingsWindow->show(EditorSettingsWindow::InputMapping);
        if (ImGui::MenuItem("Plugin Manager") && editorSettingsWindow)
            editorSettingsWindow->show(EditorSettingsWindow::Plugins);
        ImGui::EndMenu();
    }

    void MainMenuBar::handleAddMenu()
    {
        if (!ImGui::BeginMenu("Add")) return;
        if (ImGui::MenuItem("IBL") && iblWindow) iblWindow->show();
        else if (ImGui::MenuItem("Terrain") && terrainCreationWindow) terrainCreationWindow->show();
        else if (ImGui::MenuItem("Ocean") && oceanEditorWindow) oceanEditorWindow->show();
        else if (ImGui::MenuItem("Post Process") && postProcessConfigWindow) postProcessConfigWindow->show();
        else if (ImGui::MenuItem("Navigation") && navmeshWindow) navmeshWindow->show();
        else if (ImGui::MenuItem("Environment") && environmentWindow) environmentWindow->show();
        else if (ImGui::MenuItem("Global Illumination") && giConfigWindow) giConfigWindow->show();
        ImGui::EndMenu();
    }

    void MainMenuBar::handleToolsMenu()
    {
        if (!ImGui::BeginMenu("Tools")) return;
        if (ImGui::MenuItem("Generate Heightmap") && heightmapGeneratorWindow) heightmapGeneratorWindow->show();
        if (ImGui::MenuItem("Remove Background") && backgroundRemovalWindow) backgroundRemovalWindow->show();
        if (ImGui::MenuItem("Animation Retargeting"))
            controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<RetargetingEditorWindow>(""));
        if (ImGui::MenuItem("UI Theme Editor") && themeEditorWindow) themeEditorWindow->show();
        ImGui::EndMenu();
    }

    void MainMenuBar::handlePluginsMenu()
    {
        auto& entries = controllers::imguiHandler::PluginWindowRegistry::getEntries();
        if (entries.empty()) return;

        if (!ImGui::BeginMenu("Plugins")) return;

        const std::string* lastPlugin = nullptr;
        for (auto& entry : entries)
        {
            // Group windows under their plugin's name when more than one plugin registers
            if (!lastPlugin || *lastPlugin != entry.pluginName)
            {
                if (lastPlugin) ImGui::Separator();
                ImGui::SeparatorText(entry.pluginName.c_str());
                lastPlugin = &entry.pluginName;
            }
            ImGui::MenuItem(entry.title.c_str(), nullptr, &entry.visible);
        }
        ImGui::EndMenu();
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

            bool showWireframe = dispatcher.query(events::render::GetShowWireframeQuery{});
            if (ImGui::MenuItem("Show Wireframe", nullptr, showWireframe))
            {
                events::render::SetShowWireframeCommand cmd;
                cmd.show = !showWireframe;
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

            // Debug force-off override for the plugin world mask (fog of war etc.).
            // Unchecked by default — the mask is controlled by the owning plugin;
            // checking this kills it engine-side without touching the plugin.
            bool worldMaskEnabled = dispatcher.query(events::render::GetWorldMaskDebugEnabledQuery{});
            if (ImGui::MenuItem("Disable World Mask", nullptr, !worldMaskEnabled))
            {
                events::render::SetWorldMaskDebugEnabledCommand cmd;
                cmd.enabled = !worldMaskEnabled;
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

            if (ImGui::MenuItem("Asset Lifecycle") && assetLifecycleWindow) assetLifecycleWindow->show();
            if (ImGui::MenuItem("VFX Stats") && vfxDebugWindow) vfxDebugWindow->show();
            if (ImGui::MenuItem("Animation Stats") && animationDebugWindow) animationDebugWindow->show();
            if (ImGui::MenuItem("Light Streaming") && lightStreamingDebugWindow) lightStreamingDebugWindow->show();
            if (ImGui::MenuItem("Task Graph") && taskGraphWindow) taskGraphWindow->show();
            if (ImGui::MenuItem("Memory Diagnostics") && memoryDiagnosticsWindow) memoryDiagnosticsWindow->show();
            if (ImGui::MenuItem("RTT Debug") && rttDebugWindow) rttDebugWindow->show();

            ImGui::EndMenu();
        }
    }

}
