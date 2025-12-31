#include "MainImguiWindow.hpp"
#include "../camera/EditorCamera.hpp"
#include "files/FileUtils.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/RenderEvents.hpp"
#include "events/ResourceEvents.hpp"
#include "events/ApplicationEvents.hpp"
#include "events/EditorModeEvents.hpp"
#include "string/StringUtil.hpp"
#include "Import.hpp"
#include "config/Config.hpp"
#include <imgui.h>
#include <thread>

namespace windows
{
    MainImguiWindow::MainImguiWindow()
    {
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoBackground;
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        windowFlags = window_flags;

        subscribeToEvents();
    }

    MainImguiWindow::~MainImguiWindow()
    {
        events::EventDispatcher::instance().unsubscribe(sceneClearedToken);
    }

    void MainImguiWindow::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });
    }

    void MainImguiWindow::onSceneCleared()
    {
        // Release IBL preview texture if it exists
        if (iblPreviewHandle.isValid())
        {
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = iblPreviewHandle.imguiDescriptorSet;
            events::EventDispatcher::instance().execute(releaseCmd);
            iblPreviewHandle = services::EditorTextureHandle{};
        }

        // Clear IBL file selection
        selectedIBLFile = "";
    }

    void MainImguiWindow::draw()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("Vulkan Engine", nullptr, windowFlags))
        {
            ImGui::PopStyleVar(1);

            ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0.0f, 0.0f),
                             ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_None);

            menuBar();
            if (showIBLWindow)
            {
                iblWindow();
            }
            if (showEditorCameraWindow)
            {
                editorCameraWindow();
            }
            if (showCullingStatsWindow)
            {
                cullingStatsWindow();
            }
        }
        ImGui::End();
    }

    void MainImguiWindow::menuBar()
    {
        if (openModal)
        {
            ImGui::OpenPopup("Import Files");
        }

        importModel();

        if (ImGui::BeginMainMenuBar())
        {
            handleFileMenu();
            handleSettingsMenu();
            handleAddMenu();
            handleDebug();
            handlePlayControls();
            ImGui::EndMainMenuBar();
        }
    }

    void MainImguiWindow::handlePlayControls()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto currentMode = dispatcher.query(events::editor::GetEditorModeQuery{});

        // Calculate center position
        float menuBarWidth = ImGui::GetWindowWidth();
        float buttonWidth = 60.0f;
        float centerX = (menuBarWidth - buttonWidth) * 0.5f;
        ImGui::SetCursorPosX(centerX);

        if (currentMode == services::EditorMode::Edit)
        {
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

    void MainImguiWindow::importModel()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Check if the popup modal is open
        if (ImGui::BeginPopupModal("Import Files", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            std::vector<services::ImportFileRequest> requests;
            requests.reserve(files.size());
            isFlip.resize(files.size(), false);

            ImGui::Text("Files Dropped:");
            for (size_t i = 0; i < files.size(); i++)
            {
                ImGui::PushID(files[i].c_str());
                ImGui::BulletText("%s", files[i].c_str());

                if (files::FileUtils::isHDRFile(files[i]))
                {
                    bool flip = isFlip[i];
                    ImGui::Checkbox("Flip Vertically", &flip);
                    isFlip[i] = flip;
                }

                services::ImportFileRequest request;
                request.path = files[i];
                request.flipVertically = isFlip[i];
                requests.push_back(request);

                ImGui::PopID();
            }

            if (ImGui::Button("Continue"))
            {
                // Convert to Import controller format
                std::vector<importConfig::ImportFiles> importFiles;
                std::vector<std::string> filePaths;
                for (const auto& req : requests)
                {
                    importConfig::ImportConfig config;
                    config.isImageFlipVertically = req.flipVertically;
                    importFiles.emplace_back(req.path, config);
                    filePaths.push_back(req.path);
                }

                events::resource::ImportStartedNotification startNotif;
                startNotif.files = filePaths;
                dispatcher.publish(startNotif);

                // Run import in background thread to not block UI
                std::thread([importFiles = std::move(importFiles)]()
                {
                    auto& dispatcher = events::EventDispatcher::instance();

                    auto progressCallback = [&dispatcher](std::string_view currentFile,
                                                          uint32_t /*fileIndex*/,
                                                          uint32_t /*totalFiles*/,
                                                          float fileProgress)
                    {
                        events::resource::ImportProgressNotification progressNotif;
                        progressNotif.currentFile = std::string(currentFile);
                        progressNotif.progress = fileProgress;
                        dispatcher.publish(progressNotif);
                    };

                    auto result = controllers::Import::importFiles(importFiles, progressCallback);

                    events::resource::ImportCompletedNotification completeNotif;
                    for (const auto& fileResult : result.fileResults)
                    {
                        services::ImportResult res;
                        res.sourcePath = fileResult.sourcePath;
                        res.success = fileResult.success;
                        res.errorMessage = fileResult.errorMessage;
                        completeNotif.results.push_back(res);
                    }
                    dispatcher.publish(completeNotif);
                }).detach();

                ImGui::CloseCurrentPopup();
                openModal = false;
            }

            ImGui::SameLine();
            ImGui::SetCursorPosX(
                ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x *
                2);

            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
                openModal = false;
            }

            ImGui::EndPopup();
        }
    }

    void MainImguiWindow::handleFileMenu()
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
                    // Use async loading to keep UI responsive
                    events::scene::LoadSceneAsyncCommand cmd;
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

    void MainImguiWindow::handleSettingsMenu()
    {
        if (ImGui::BeginMenu("Settings"))
        {
            if (ImGui::MenuItem("Import"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"Model Files (*.obj;*.fbx;*.dae;*.gltf)", L"*.obj;*.fbx;*.dae;*.gltf"},
                    {L"Image Files (*.png;*.jpg;*.jpeg;*.bmp)", L"*.png;*.jpg;*.jpeg;*.bmp"},
                    {L"Hdr Files (*.exr;*.hdr)", L"*.exr;*.hdr"},
                    {L"Audio Files (*.wav;*.ogg:*.mp3)", L"*.wav;*.ogg;*.mp3"}
                };

                files = fileDialog.multiSelectFileDialog(fileTypes);
                if (!files.empty())
                {
                    openModal = true;
                }
            }
            else if (ImGui::MenuItem("Editor Camera"))
            {
                showEditorCameraWindow = true;
            }
            else if (ImGui::MenuItem("Layout Style"))
            {
            }
            ImGui::EndMenu();
        }
    }

    void MainImguiWindow::handleAddMenu()
    {
        if (ImGui::BeginMenu("Add"))
        {
            if (ImGui::MenuItem("IBL"))
            {
                showIBLWindow = true;
            }
            else if (ImGui::MenuItem("Terrain"))
            {
            }
            ImGui::EndMenu();
        }
    }

    void MainImguiWindow::handleDebug()
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

            if (ImGui::MenuItem("Culling Stats", nullptr, showCullingStatsWindow))
            {
                showCullingStatsWindow = !showCullingStatsWindow;
            }

            ImGui::EndMenu();
        }
    }

    void MainImguiWindow::iblWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IBL", &showIBLWindow))
        {
            ImGui::Text("IBL Window");

            events::scene::GetRootEntityQuery rootQuery;
            auto rootHandle = dispatcher.query(rootQuery);

            events::scene::GetIBLDataQuery iblQuery;
            iblQuery.entity = rootHandle;
            auto iblData = dispatcher.query(iblQuery);
            if (iblData.has_value() && !iblData->fileName.empty())
            {
                // Update local state from scene if different
                std::string currentPath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
                if (currentPath != iblData->fileName)
                {
                    selectedIBLFile = iblData->fileName;
                }
            }

            if (ImGui::Button("Select"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"Hdr Files (*.vfHdr)", L"*.vfHdr"}
                };

                selectedIBLFile = fileDialog.openFileDialog(fileTypes);
                std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());

                // Update IBL component on root entity via event system
                events::scene::SetIBLDataCommand iblCmd;
                iblCmd.entity = rootHandle;
                iblCmd.iblData.fileName = filePath;
                dispatcher.execute(iblCmd);
            }

            ImGui::SameLine();
            std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
            ImGui::Text("%s", filePath.c_str());

            const bool hasFile = !filePath.empty();

            if (!hasFile) ImGui::BeginDisabled();

            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
                events::render::SetIBLCommand cmd;
                cmd.hdrPath = filePath;
                dispatcher.execute(cmd);
            }
            if (!hasFile) ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::SetCursorPosX(
                ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x *
                2);

            if (ImGui::Button("Remove", ImVec2(120, 0)))
            {
                selectedIBLFile = "";

                // Release editor preview texture first
                if (iblPreviewHandle.isValid())
                {
                    events::render::ReleaseEditorTextureCommand releaseCmd;
                    releaseCmd.handle = iblPreviewHandle.imguiDescriptorSet;
                    dispatcher.execute(releaseCmd);
                    iblPreviewHandle = services::EditorTextureHandle{};
                }

                // Then remove IBL from renderer
                events::render::RemoveIBLCommand removeIblCmd;
                dispatcher.execute(removeIblCmd);

                // Remove IBL component from root via event system
                events::scene::GetRootEntityQuery rootQuery;
                auto rootHandle = dispatcher.query(rootQuery);

                events::scene::RemoveIBLComponentCommand removeCmd;
                removeCmd.entity = rootHandle;
                dispatcher.execute(removeCmd);
            }

            // Display preview image AFTER all buttons are processed
            // This ensures the descriptor set is valid if we display it
            if (iblPreviewHandle.isValid())
            {
                ImGui::Image(iblPreviewHandle.imguiDescriptorSet, ImVec2(200, 200));
            }
        }
        ImGui::End();
    }

    void MainImguiWindow::editorCameraWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Editor Camera Settings", &showEditorCameraWindow))
        {
            if (!editorCameraRef)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Editor Camera not available");
                ImGui::End();
                return;
            }

            ImGui::Text("Transform");
            ImGui::Separator();

            // Position
            ImGui::Text("Position");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat3("##Position", &editorCameraRef->position.x, 0.1f))
            {
                editorCameraRef->updateViewMatrix();
            }
            ImGui::PopItemWidth();

            // Rotation
            ImGui::Text("Rotation (degrees)");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat3("##Rotation", &editorCameraRef->rotation.x, 0.5f))
            {
                // Clamp pitch
                editorCameraRef->rotation.x = std::clamp(editorCameraRef->rotation.x, -89.0f, 89.0f);
                editorCameraRef->updateViewMatrix();
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Projection");
            ImGui::Separator();

            // Field of View
            ImGui::Text("Field of View");
            ImGui::PushItemWidth(-1);
            if (ImGui::SliderFloat("##FOV", &editorCameraRef->fieldOfView, 30.0f, 120.0f, "%.1f"))
            {
                editorCameraRef->updateProjectionMatrix();
            }
            ImGui::PopItemWidth();

            // Near Plane
            ImGui::Text("Near Plane");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##NearPlane", &editorCameraRef->nearPlane, 0.01f, 0.001f, 10.0f, "%.3f"))
            {
                editorCameraRef->updateProjectionMatrix();
            }
            ImGui::PopItemWidth();

            // Far Plane
            ImGui::Text("Far Plane");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##FarPlane", &editorCameraRef->farPlane, 1.0f, 10.0f, 100000.0f, "%.1f"))
            {
                editorCameraRef->updateProjectionMatrix();
            }
            ImGui::PopItemWidth();

            // Aspect Ratio (read-only, set by viewport)
            ImGui::Text("Aspect Ratio");
            ImGui::PushItemWidth(-1);
            ImGui::BeginDisabled();
            ImGui::InputFloat("##AspectRatio", &editorCameraRef->aspectRatio, 0, 0, "%.3f");
            ImGui::EndDisabled();
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Navigation");
            ImGui::Separator();

            // Move Speed
            ImGui::Text("Move Speed");
            ImGui::PushItemWidth(-1);
            ImGui::SliderFloat("##MoveSpeed", &editorCameraRef->moveSpeed, 0.5f, 50.0f, "%.1f");
            ImGui::PopItemWidth();

            // Mouse Sensitivity
            ImGui::Text("Mouse Sensitivity");
            ImGui::PushItemWidth(-1);
            ImGui::SliderFloat("##MouseSensitivity", &editorCameraRef->mouseSensitivity, 0.01f, 0.5f, "%.3f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            if (ImGui::Button("Reset to Default", ImVec2(-1, 0)))
            {
                editorCameraRef->position = glm::vec3(0.0f, 2.0f, 5.0f);
                editorCameraRef->rotation = glm::vec3(0.0f);
                editorCameraRef->fieldOfView = 60.0f;
                editorCameraRef->nearPlane = 0.1f;
                editorCameraRef->farPlane = 1000.0f;
                editorCameraRef->moveSpeed = 5.0f;
                editorCameraRef->mouseSensitivity = 0.1f;
                editorCameraRef->updateViewMatrix();
                editorCameraRef->updateProjectionMatrix();
            }
        }
        ImGui::End();
    }

    void MainImguiWindow::cullingStatsWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Culling Stats", &showCullingStatsWindow))
        {
            events::render::GetCullingStatsQuery query;
            auto stats = events::EventDispatcher::instance().query(query);

            ImGui::Text("Active Camera: %u", stats.activeCameraId);
            ImGui::Separator();

            // GPU-Driven Rendering Statistics
            if (ImGui::CollapsingHeader("GPU-Driven Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                const auto& gpu = stats.gpuDriven;

                // Status indicators
                ImGui::Text("Status:");
                ImGui::SameLine();
                ImGui::TextColored(gpu.enabled ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                                   gpu.enabled ? "ENABLED" : "DISABLED");

                if (gpu.enabled)
                {
                    ImGui::Text("Features:");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.frustumCullingEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "Frustum");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.occlusionCullingEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "| Hi-Z Occlusion");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.lodSelectionEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "| LOD");

                    ImGui::Separator();

                    // Object counts
                    ImGui::Text("Objects: %u total", gpu.totalObjects);
                    if (gpu.totalObjects > 0)
                    {
                        ImGui::Text("  Culled by Frustum:   %u", gpu.culledByFrustum);
                        ImGui::Text("  Culled by Occlusion: %u", gpu.culledByOcclusion);
                        ImGui::Text("  Visible:             %u", gpu.visibleObjects);

                        // Culling efficiency
                        uint32_t totalCulled = gpu.culledByFrustum + gpu.culledByOcclusion;
                        float cullRate = static_cast<float>(totalCulled) / static_cast<float>(gpu.totalObjects);
                        ImGui::Text("Cull Rate:");
                        ImGui::SameLine();
                        ImGui::ProgressBar(cullRate, ImVec2(150, 0),
                                           (std::to_string(static_cast<int>(cullRate * 100)) + "%").c_str());
                    }

                    ImGui::Separator();

                    // LOD distribution
                    ImGui::Text("LOD Distribution:");
                    uint32_t totalLOD = gpu.objectsLOD0 + gpu.objectsLOD1 + gpu.objectsLOD2 + gpu.objectsLOD3;
                    if (totalLOD > 0)
                    {
                        ImGui::Text("  LOD0 (High):   %u", gpu.objectsLOD0);
                        ImGui::Text("  LOD1 (Medium): %u", gpu.objectsLOD1);
                        ImGui::Text("  LOD2 (Low):    %u", gpu.objectsLOD2);
                        ImGui::Text("  LOD3 (Lowest): %u", gpu.objectsLOD3);
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "  No LOD data available");
                    }

                    ImGui::Separator();

                    // Merged buffer stats
                    ImGui::Text("Merged Buffer:");
                    ImGui::Text("  Vertices:  %u", gpu.mergedVertexCount);
                    ImGui::Text("  Indices:   %u", gpu.mergedIndexCount);
                    ImGui::Text("  Meshes:    %u", gpu.registeredMeshCount);
                    ImGui::Text("  Textures:  %u", gpu.registeredTextureCount);

                    if (gpu.hiZMipLevels > 0)
                    {
                        ImGui::Text("Hi-Z Mip Levels: %u", gpu.hiZMipLevels);
                    }

                    ImGui::Separator();

                    // Batch rendering stats
                    ImGui::Text("Indirect Batches:");
                    ImGui::Text("  Batches:           %u", gpu.batchCount);
                    ImGui::Text("  Commands/Batch:    %u", gpu.commandsPerBatch);
                    ImGui::Text("  Total Capacity:    %u objects", gpu.totalCapacity);
                    ImGui::Text("  Registered:        %u objects", gpu.totalObjects);
                    ImGui::Text("  Draw Calls:        %u", gpu.drawCalls);

                    if (gpu.totalCapacity > 0)
                    {
                        float utilization = static_cast<float>(gpu.totalObjects) / static_cast<float>(gpu.totalCapacity);
                        char percentStr[32];
                        if (utilization < 0.0001f && gpu.totalObjects > 0) {
                            snprintf(percentStr, sizeof(percentStr), "<0.01%% (%u)", gpu.totalObjects);
                        } else {
                            snprintf(percentStr, sizeof(percentStr), "%.4f%%", utilization * 100.0f);
                        }
                        ImGui::Text("Capacity Used:");
                        ImGui::SameLine();
                        ImGui::ProgressBar(utilization, ImVec2(150, 0), percentStr);
                    }

                    ImGui::Separator();

                    // Memory usage with capacity info
                    ImGui::Text("GPU Memory (Used / Allocated):");
                    auto formatMemory = [](uint64_t bytes) -> std::string {
                        if (bytes >= 1024 * 1024 * 1024) {
                            return std::to_string(bytes / (1024 * 1024 * 1024)) + "." +
                                   std::to_string((bytes / (1024 * 1024 * 100)) % 10) + " GB";
                        } else if (bytes >= 1024 * 1024) {
                            return std::to_string(bytes / (1024 * 1024)) + "." +
                                   std::to_string((bytes / (1024 * 100)) % 10) + " MB";
                        } else if (bytes >= 1024) {
                            return std::to_string(bytes / 1024) + " KB";
                        }
                        return std::to_string(bytes) + " B";
                    };

                    // Calculate used memory based on totalObjects
                    uint64_t usedDrawCmd = gpu.totalObjects * 20;  // sizeof(DrawIndexedIndirectCommand)
                    uint64_t usedPerDraw = gpu.totalObjects * 224; // sizeof(PerDrawData)
                    uint64_t usedTotal = usedDrawCmd + gpu.drawCountBufferSize + usedPerDraw;

                    ImGui::Text("  Draw Commands:  %s / %s",
                                formatMemory(usedDrawCmd).c_str(),
                                formatMemory(gpu.drawCommandBufferSize).c_str());
                    ImGui::Text("  Draw Counts:    %s (fixed)",
                                formatMemory(gpu.drawCountBufferSize).c_str());
                    ImGui::Text("  Per-Draw Data:  %s / %s",
                                formatMemory(usedPerDraw).c_str(),
                                formatMemory(gpu.perDrawDataBufferSize).c_str());
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                                       "  Total:          %s / %s",
                                       formatMemory(usedTotal).c_str(),
                                       formatMemory(gpu.totalMemoryUsage).c_str());
                }

                ImGui::Unindent();
            }
            ImGui::Separator();

            // BVH Statistics
            if (ImGui::CollapsingHeader("BVH Statistics (CPU)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                ImGui::Text("Static BVH:  %zu entities, %zu nodes",
                            stats.staticBvhEntityCount, stats.staticBvhNodeCount);
                ImGui::Text("Dynamic BVH: %zu entities, %zu nodes",
                            stats.dynamicBvhEntityCount, stats.dynamicBvhNodeCount);
                ImGui::Text("Total:       %zu entities",
                            stats.staticBvhEntityCount + stats.dynamicBvhEntityCount);
                ImGui::Unindent();
            }
            ImGui::Separator();

            if (stats.cameraStats.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No cameras registered");
            }
            else
            {
                for (const auto& cam : stats.cameraStats)
                {
                    ImGui::PushID(static_cast<int>(cam.cameraId));

                    std::string header = "Camera " + std::to_string(cam.cameraId);
                    if (cam.isActive)
                    {
                        header += " [ACTIVE]";
                    }

                    if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Indent();

                        ImGui::Text("Status:");
                        ImGui::SameLine();
                        ImGui::TextColored(cam.frustumReady ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                                           cam.frustumReady ? "Frustum Ready" : "Frustum Not Ready");

                        ImGui::SameLine();
                        ImGui::TextColored(cam.bvhBuilt ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0.5f, 0, 1),
                                           cam.bvhBuilt ? "| BVH Built" : "| BVH Not Built");

                        ImGui::Text("Occlusion:");
                        ImGui::SameLine();
                        if (!cam.occlusionEnabled)
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Disabled");
                        }
                        else if (!cam.occlusionInitialized)
                        {
                            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Not Initialized");
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Active");
                        }

                        ImGui::Separator();

                        ImGui::Text("Total Mesh Entities: %u", cam.totalMeshEntities);

                        if (cam.occlusionInitialized && cam.occlusionEnabled)
                        {
                            ImGui::Text("After Frustum Cull:  %u", cam.visibleAfterFrustumCull);
                            ImGui::Text("After Occlusion:     %u", cam.visibleAfterOcclusionCull);
                            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1), "Occluded: %u", cam.occludedCount);

                            if (cam.totalMeshEntities > 0)
                            {
                                float occlusionRate = static_cast<float>(cam.occludedCount) / static_cast<float>(cam.
                                    totalMeshEntities);
                                ImGui::Text("Occlusion Rate:");
                                ImGui::SameLine();
                                ImGui::ProgressBar(occlusionRate, ImVec2(-1, 0),
                                                   (std::to_string(static_cast<int>(occlusionRate * 100)) + "%").
                                                   c_str());
                            }
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Occlusion stats not available");
                        }

                        ImGui::Unindent();
                    }

                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }
}
