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
                        int maxHulls = static_cast<int>(meshConfig.maxConvexHulls);
                        if (ImGui::SliderInt("Max Hulls", &maxHulls, 1, 64))
                        {
                            meshConfig.maxConvexHulls = static_cast<uint32_t>(maxHulls);
                        }

                        int maxVerts = static_cast<int>(meshConfig.maxVerticesPerHull);
                        if (ImGui::SliderInt("Max Vertices Per Hull", &maxVerts, 8, 256))
                        {
                            meshConfig.maxVerticesPerHull = static_cast<uint32_t>(maxVerts);
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
