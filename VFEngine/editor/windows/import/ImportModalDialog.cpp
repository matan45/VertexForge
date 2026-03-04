#include "ImportModalDialog.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ResourceEvents.hpp"
#include "files/FileUtils.hpp"
#include "Import.hpp"
#include "config/Config.hpp"
#include <imgui.h>
#include <thread>

namespace windows
{
    void ImportModalDialog::openImportDialog()
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"Model Files (*.obj;*.fbx;*.dae;*.gltf)", L"*.obj;*.fbx;*.dae;*.gltf"},
            {L"Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.tga)", L"*.png;*.jpg;*.jpeg;*.bmp;*.tga"},
            {L"Hdr Files (*.exr;*.hdr)", L"*.exr;*.hdr"},
            {L"Audio Files (*.wav;*.ogg;*.mp3)", L"*.wav;*.ogg;*.mp3"},
            {L"Font Files (*.ttf;*.otf)", L"*.ttf;*.otf"}
        };

        files = fileDialog.multiSelectFileDialog(fileTypes);
        if (!files.empty())
        {
            openModal = true;
        }
    }

    void ImportModalDialog::draw()
    {
        if (openModal)
        {
            ImGui::OpenPopup("Import Files");
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Check if the popup modal is open
        if (ImGui::BeginPopupModal("Import Files", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            std::vector<services::ImportFileRequest> requests;
            requests.reserve(files.size());
            isFlip.resize(files.size(), false);
            meshConfigs.resize(files.size());

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

                if (files::FileUtils::isMeshFile(files[i]))
                {
                    ImGui::Indent();
                    auto& meshConfig = meshConfigs[i];

                    ImGui::Checkbox("Generate Convex Decomposition", &meshConfig.generateConvexDecomposition);

                    if (meshConfig.generateConvexDecomposition)
                    {
                        ImGui::Separator();

                        // Preset selector
                        const char* presetNames[] = {"Fast", "Balanced", "Quality", "Custom"};
                        int presetIndex = static_cast<int>(meshConfig.vhacdPreset);
                        if (ImGui::Combo("Quality Preset", &presetIndex, presetNames, IM_ARRAYSIZE(presetNames)))
                        {
                            meshConfig.vhacdPreset = static_cast<importConfig::VHACDPreset>(presetIndex);
                        }

                        // Show preset description
                        switch (meshConfig.vhacdPreset)
                        {
                            case importConfig::VHACDPreset::Fast:
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                                   "Fast processing, suitable for prototyping");
                                break;
                            case importConfig::VHACDPreset::Balanced:
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                                   "Good balance of speed and quality (recommended)");
                                break;
                            case importConfig::VHACDPreset::Quality:
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                                   "High accuracy, slower processing");
                                break;
                            case importConfig::VHACDPreset::Custom:
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                                   "Manual parameter control");
                                break;
                        }

                        // Custom parameters (only editable when preset is Custom)
                        bool isCustom = (meshConfig.vhacdPreset == importConfig::VHACDPreset::Custom);

                        if (!isCustom)
                        {
                            ImGui::BeginDisabled();
                        }

                        // Basic parameters
                        int maxHulls = static_cast<int>(meshConfig.maxConvexHulls);
                        if (ImGui::SliderInt("Max Hulls", &maxHulls, 1, 64))
                        {
                            meshConfig.maxConvexHulls = static_cast<uint32_t>(maxHulls);
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Maximum number of convex hulls to generate");

                        int maxVerts = static_cast<int>(meshConfig.maxVerticesPerHull);
                        if (ImGui::SliderInt("Max Vertices Per Hull", &maxVerts, 8, 256))
                        {
                            meshConfig.maxVerticesPerHull = static_cast<uint32_t>(maxVerts);
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Maximum vertices per hull (Jolt Physics limit: 256)");

                        // Advanced parameters in collapsible section
                        if (ImGui::TreeNode("Advanced Parameters"))
                        {
                            int resolution = static_cast<int>(meshConfig.vhacdResolution);
                            if (ImGui::SliderInt("Resolution", &resolution, 10000, 500000))
                            {
                                meshConfig.vhacdResolution = static_cast<uint32_t>(resolution);
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Voxel resolution. Higher = more accurate but slower");

                            float minVolErr = meshConfig.minVolumePercentError;
                            if (ImGui::SliderFloat("Min Volume Error %%", &minVolErr, 0.1f, 10.0f, "%.1f"))
                            {
                                meshConfig.minVolumePercentError = minVolErr;
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Stop subdividing when hull is within this %% of mesh volume");

                            int recursionDepth = static_cast<int>(meshConfig.maxRecursionDepth);
                            if (ImGui::SliderInt("Max Recursion Depth", &recursionDepth, 4, 16))
                            {
                                meshConfig.maxRecursionDepth = static_cast<uint32_t>(recursionDepth);
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Maximum recursion depth for hull splitting");

                            ImGui::Checkbox("Shrink Wrap", &meshConfig.shrinkWrap);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Snap hull vertices to original mesh surface");

                            ImGui::TreePop();
                        }

                        if (!isCustom)
                        {
                            ImGui::EndDisabled();
                        }
                    }
                    ImGui::Unindent();
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
                for (size_t i = 0; i < requests.size(); ++i)
                {
                    const auto& req = requests[i];
                    importConfig::ImportConfig config;
                    config.isImageFlipVertically = req.flipVertically;
                    config.meshConfig = meshConfigs[i];
                    importFiles.emplace_back(req.path, config);
                    filePaths.push_back(req.path);
                }

                events::resource::ImportStartedNotification startNotif;
                startNotif.files = filePaths;
                dispatcher.publish(startNotif);

                // Reset cancellation flag before starting import
                controllers::Import::resetCancellation();

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
                ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x * 2);

            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
                openModal = false;
            }

            ImGui::EndPopup();
        }
    }
}
