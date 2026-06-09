#include "StatusBar.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/editor/EditorSettingsEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "types/RenderSettings.hpp"
#include "memory/GpuAllocationStats.hpp"
#include "threading/EditorTaskStats.hpp"
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
        settingsChangedToken = dispatcher.subscribe<events::editor::EditorSettingsChangedNotification>(
            [this](const events::editor::EditorSettingsChangedNotification& n) {
                debugSettings = n.settings.debug;
            });

        debugSettings = dispatcher.query(events::editor::GetEditorSettingsQuery{}).debug;

        // Track VSync (present mode) so the FPS can be capped to the presented rate.
        displaySettingsToken = dispatcher.subscribe<events::application::ApplyDisplaySettingsNotification>(
            [this](const events::application::ApplyDisplaySettingsNotification& n) {
                vsyncEnabled = (n.presentMode == types::PresentMode::Fifo);
            });
        try
        {
            auto renderSettings = dispatcher.query(events::scene::GetRenderSettingsQuery{});
            vsyncEnabled = (renderSettings.display.presentMode == types::PresentMode::Fifo);
        }
        catch (const std::exception&) {}
        try { refreshHz = dispatcher.query(events::application::GetMonitorRefreshRateQuery{}); }
        catch (const std::exception&) {}
    }

    StatusBar::~StatusBar()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (sceneLoadedToken.isValid()) dispatcher.unsubscribe(sceneLoadedToken);
        if (sceneClearedToken.isValid()) dispatcher.unsubscribe(sceneClearedToken);
        if (settingsChangedToken.isValid()) dispatcher.unsubscribe(settingsChangedToken);
        if (displaySettingsToken.isValid()) dispatcher.unsubscribe(displaySettingsToken);
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
            float frameMs = deltaTime * 1000.0f;

            // Viewport-only frame time: exclude the editor-only ImGuiDraw cost so
            // the displayed FPS approximates the standalone Runtime (VK-1367).
            float imguiDrawMs = threading::EditorTaskStats::imguiDrawDurationNs.load(std::memory_order_relaxed) / 1e6f;
            float viewportFrameMs = frameMs - imguiDrawMs;
            if (viewportFrameMs < 0.0f) viewportFrameMs = 0.0f;

            // VSync (Fifo) caps presentation to the refresh interval; the Runtime would
            // present at that rate too, so floor the viewport frame time to match (VK-1367).
            if (vsyncEnabled && refreshHz > 0)
            {
                float presentIntervalMs = 1000.0f / static_cast<float>(refreshHz);
                if (viewportFrameMs < presentIntervalMs) viewportFrameMs = presentIntervalMs;
            }

            float instantFps = (viewportFrameMs > 0.0f) ? (1000.0f / viewportFrameMs) : 0.0f;
            if (instantFps > 0.0f)
            {
                smoothedViewportFps = (smoothedViewportFps <= 0.0f)
                    ? instantFps
                    : smoothedViewportFps + fpsEmaAlpha * (instantFps - smoothedViewportFps);
            }
            float fps = smoothedViewportFps;

            // Refresh heavier stats periodically
            refreshTimer += deltaTime;
            if (refreshTimer >= refreshInterval)
            {
                refreshTimer = 0.0f;

                if (debugSettings.showDrawCalls)
                {
                    try
                    {
                        auto stats = events::EventDispatcher::instance().query(
                            events::render::GetCullingStatsQuery{});
                        cachedDrawCalls = stats.totalDrawCalls;
                        cachedDrawCallsByCategory = stats.drawCallsByCategory;
                    }
                    catch (const std::exception&)
                    {
                        cachedDrawCalls = 0;
                        cachedDrawCallsByCategory = {};
                    }
                }

                cachedVramMB = memory::GpuAllocationStats::deviceLocalUsedBytes.load() / (1024 * 1024);

                if (refreshHz == 0)
                {
                    try { refreshHz = events::EventDispatcher::instance().query(events::application::GetMonitorRefreshRateQuery{}); }
                    catch (const std::exception&) {}
                }
            }

            // Insert a separator before every item except the first visible one.
            bool firstItem = true;
            auto separator = [&firstItem]() {
                if (!firstItem)
                {
                    ImGui::SameLine(0.0f, 16.0f);
                    ImGui::TextDisabled("|");
                    ImGui::SameLine(0.0f, 16.0f);
                }
                firstItem = false;
            };

            if (debugSettings.showFPS)
            {
                separator();
                ImGui::Text("FPS: %.0f", fps);
            }
            if (debugSettings.showGPUTime)
            {
                separator();
                ImGui::Text("Frame: %.1f ms", viewportFrameMs);
            }
            if (debugSettings.showDrawCalls)
            {
                separator();
                ImGui::Text("Draw Calls: %u", cachedDrawCalls);
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Draw calls by category");
                    ImGui::Separator();
                    bool any = false;
                    for (size_t i = 0; i < render::FrameDrawStats::kCount; ++i)
                    {
                        if (cachedDrawCallsByCategory[i] == 0) continue;
                        any = true;
                        ImGui::Text("%-16s %u",
                            render::drawCategoryName(static_cast<render::DrawCategory>(i)),
                            cachedDrawCallsByCategory[i]);
                    }
                    if (!any) ImGui::TextDisabled("(none)");
                    ImGui::EndTooltip();
                }
            }
            separator();
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
