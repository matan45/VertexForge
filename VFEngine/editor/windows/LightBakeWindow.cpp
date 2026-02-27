#include "LightBakeWindow.hpp"
#include "LightmapPreviewWindow.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "imgui.h"

namespace windows
{
    LightBakeWindow::LightBakeWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        bakeCompleteToken = dispatcher.subscribe<services::events::lightbake::BakeCompletedNotification>(
            [this](const services::events::lightbake::BakeCompletedNotification& notif) {
                baking.store(false);
                bakeProgress.store(1.0f);
                {
                    std::lock_guard<std::mutex> lock(resultMutex);
                    lastResult = notif.result;
                    hasResult = true;
                }
            });

        bakeFailedToken = dispatcher.subscribe<services::events::lightbake::BakeFailedNotification>(
            [this](const services::events::lightbake::BakeFailedNotification&) {
                baking.store(false);
                bakeProgress.store(0.0f);
            });
    }

    LightBakeWindow::~LightBakeWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unsubscribe(bakeCompleteToken);
        dispatcher.unsubscribe(bakeFailedToken);
    }

    void LightBakeWindow::show()
    {
        visible = true;
    }

    void LightBakeWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(420, 350), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Light Bake", &visible))
        {
            drawSettings();
            ImGui::Separator();
            drawBakeActions();
            ImGui::Separator();
            drawProgress();

            if (hasResult)
            {
                ImGui::Separator();
                drawResult();
            }
        }
        ImGui::End();
    }

    void LightBakeWindow::drawSettings()
    {
        ImGui::Text("Bake Settings");
        ImGui::Spacing();

        ImGui::DragFloat("Texels Per Unit", &texelsPerUnit, 0.5f, 1.0f, 128.0f);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Higher values produce sharper lightmaps but increase bake time and atlas size.");
        }

        const int atlasSizes[] = { 512, 1024, 2048, 4096, 8192 };
        const char* atlasSizeLabels[] = { "512", "1024", "2048", "4096", "8192" };
        int currentIdx = 3; // default 4096
        for (int i = 0; i < 5; i++)
        {
            if (atlasSizes[i] == maxAtlasSize)
            {
                currentIdx = i;
                break;
            }
        }
        if (ImGui::Combo("Max Atlas Size", &currentIdx, atlasSizeLabels, 5))
        {
            maxAtlasSize = atlasSizes[currentIdx];
        }
    }

    void LightBakeWindow::drawBakeActions()
    {
        bool isBaking = baking.load();

        if (isBaking)
        {
            if (ImGui::Button("Cancel Bake", ImVec2(-1.0f, 0.0f)))
            {
                services::events::lightbake::CancelBakeCommand cmd;
                events::EventDispatcher::instance().execute(cmd);
            }
        }
        else
        {
            if (ImGui::Button(hasResult ? "Re-Bake" : "Bake", ImVec2(-1.0f, 0.0f)))
            {
                // Open save dialog if no output path set
                std::string bakePath = outputPath;
                if (bakePath.empty())
                {
                    std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                        {L"VF Lightmap (*.vfLightmap)", L"*.vfLightmap"}
                    };
                    bakePath = fileDialog.saveFileDialog(fileTypes, L"vfLightmap");
                    if (!bakePath.empty())
                    {
                        outputPath = bakePath;
                    }
                }

                if (!bakePath.empty())
                {
                    services::events::lightbake::StartBakeCommand cmd;
                    cmd.config.texelsPerUnit = texelsPerUnit;
                    cmd.config.maxAtlasSize = static_cast<uint32_t>(maxAtlasSize);
                    cmd.config.outputPath = outputPath;
                    events::EventDispatcher::instance().execute(cmd);
                    baking.store(true);
                    bakeProgress.store(0.0f);
                    hasResult = false;
                }
            }

            if (ImGui::Button("Load Lightmap", ImVec2(-1.0f, 0.0f)))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"VF Lightmap (*.vfLightmap)", L"*.vfLightmap"}
                };
                std::string path = fileDialog.openFileDialog(fileTypes);
                if (!path.empty())
                {
                    services::events::lightbake::LoadLightmapCommand cmd;
                    cmd.lightmapPath = path;
                    cmd.texelsPerUnit = texelsPerUnit;
                    bool success = events::EventDispatcher::instance().execute(cmd);
                    if (success)
                    {
                        outputPath = path;
                    }
                }
            }

            if (hasResult && ImGui::Button("Clear Lightmap", ImVec2(-1.0f, 0.0f)))
            {
                services::events::lightbake::ClearLightmapCommand cmd;
                events::EventDispatcher::instance().execute(cmd);
                hasResult = false;
                bakeProgress.store(0.0f);
                lastResult = {};
            }
        }
    }

    void LightBakeWindow::drawProgress()
    {
        bool isBaking = baking.load();

        if (isBaking)
        {
            // Query live progress from service
            services::events::lightbake::GetBakeProgressQuery query;
            float progress = events::EventDispatcher::instance().query(query);
            bakeProgress.store(progress);

            ImGui::Text("Baking...");
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        }
        else
        {
            float progress = bakeProgress.load();
            if (progress > 0.0f)
            {
                ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
            }
        }
    }

    void LightBakeWindow::drawResult()
    {
        std::lock_guard<std::mutex> lock(resultMutex);

        if (lastResult.success)
        {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Bake Successful");
        }
        else
        {
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "Bake Failed");
        }

        ImGui::Text("Atlas: %ux%u", lastResult.atlasWidth, lastResult.atlasHeight);
        ImGui::Text("Lights Baked: %u", lastResult.bakedLightCount);
        ImGui::Text("Time: %.2fs", lastResult.bakeTimeSeconds);

        if (!lastResult.lightmapPath.empty())
        {
            ImGui::Text("Output: %s", lastResult.lightmapPath.c_str());

            if (lastResult.success && ImGui::Button("Preview Lightmap", ImVec2(-1.0f, 0.0f)))
            {
                auto previewWindow = std::make_shared<LightmapPreviewWindow>(lastResult.lightmapPath);
                controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
            }

            if (lastResult.success && ImGui::Button("Save to Scene", ImVec2(-1.0f, 0.0f)))
            {
                std::vector<std::pair<std::wstring, std::wstring>> sceneFileTypes = {
                    {L"VF Scene Files (*.vfScene)", L"*.vfScene"}
                };
                std::string scenePath = fileDialog.saveFileDialog(sceneFileTypes, L"vfScene");
                if (!scenePath.empty())
                {
                    events::scene::SaveSceneCommand cmd;
                    cmd.filePath = scenePath;
                    events::EventDispatcher::instance().execute(cmd);
                }
            }
        }
    }
}
