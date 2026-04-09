#include "StatusBar.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "memory/GpuAllocationStats.hpp"
#include <imgui.h>
#include <filesystem>

namespace windows
{
    StatusBar::StatusBar()
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

    StatusBar::~StatusBar()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (sceneLoadedToken.isValid()) dispatcher.unsubscribe(sceneLoadedToken);
        if (sceneClearedToken.isValid()) dispatcher.unsubscribe(sceneClearedToken);
    }

    void StatusBar::draw(const ImGuiViewport* viewport)
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

        float y = viewport->WorkPos.y + viewport->WorkSize.y - height;
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, y));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        if (ImGui::Begin("##StatusBar", nullptr, flags))
        {
            float deltaTime = ImGui::GetIO().DeltaTime;
            float fps = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;
            float frameMs = deltaTime * 1000.0f;

            // Refresh heavier stats periodically
            refreshTimer += deltaTime;
            if (refreshTimer >= refreshInterval)
            {
                refreshTimer = 0.0f;

                try
                {
                    auto stats = events::EventDispatcher::instance().query(
                        events::render::GetCullingStatsQuery{});
                    cachedDrawCalls = stats.gpuDriven.visibleObjects;
                }
                catch (const std::exception&)
                {
                    cachedDrawCalls = 0;
                }

                cachedVramMB = memory::GpuAllocationStats::deviceLocalUsedBytes.load() / (1024 * 1024);
            }

            ImGui::Text("FPS: %.0f", fps);
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("Frame: %.1f ms", frameMs);
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("Draw Calls: %u", cachedDrawCalls);
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("VRAM: %llu MB", static_cast<unsigned long long>(cachedVramMB));

            // Scene name on the right
            std::string sceneLabel = currentSceneName.empty() ? "No Scene" : "Scene: " + currentSceneName;
            float textWidth = ImGui::CalcTextSize(sceneLabel.c_str()).x;
            float rightX = ImGui::GetWindowContentRegionMax().x - textWidth;
            ImGui::SameLine(rightX);
            if (currentSceneName.empty())
                ImGui::TextDisabled("%s", sceneLabel.c_str());
            else
                ImGui::Text("%s", sceneLabel.c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
}
