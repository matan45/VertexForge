#include "LightStreamingDebugWindow.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/LightStreamingEvents.hpp"
#include "print/Log.hpp"
#include <imgui.h>

namespace windows
{
    void LightStreamingDebugWindow::draw()
    {
        if (!visible) return;

        if (!configLoaded)
        {
            loadConfig();
        }

        // Refresh stats every frame
        auto& dispatcher = ::events::EventDispatcher::instance();
        try
        {
            stats = dispatcher.query(events::render::lightstreaming::GetLightStreamingStatsQuery{});
        }
        catch (const std::exception& e)
        {
            vfLogWarning("LightStreamingDebugWindow: Failed to query stats: {}", e.what());
        }
        catch (...) {}

        ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Light Streaming", &visible))
        {
            drawStatsSection();
            drawPoolVisualization();
            drawConfigSection();
        }
        ImGui::End();
    }

    void LightStreamingDebugWindow::drawStatsSection()
    {
        if (ImGui::CollapsingHeader("Live Stats", ImGuiTreeNodeFlags_DefaultOpen))
        {
            uint32_t totalRegistered = stats.registeredPointLights + stats.registeredSpotLights;
            uint32_t totalActive = stats.activePointLights + stats.activeSpotLights;

            ImGui::Text("Total Registered: %u  Active: %u  Excluded: %u",
                        totalRegistered, totalActive, stats.excludedByBudget);
            ImGui::Separator();

            // Point lights
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "Point Lights");
            ImGui::Text("  Registered: %u / %u", stats.registeredPointLights, config.maxPointLights);
            ImGui::Text("  Active:     %u", stats.activePointLights);

            float pointUtil = stats.pointPoolUtilization;
            ImVec4 pointColor = pointUtil > 0.9f ? ImVec4(1, 0.3f, 0.3f, 1)
                              : pointUtil > 0.7f ? ImVec4(1, 0.8f, 0.2f, 1)
                                                 : ImVec4(0.3f, 1, 0.3f, 1);
            ImGui::TextColored(pointColor, "  Pool: %.1f%%", pointUtil * 100.0f);
            ImGui::SameLine();
            ImGui::Text("  Frag: %.1f%%", stats.pointFragmentation * 100.0f);

            ImGui::Spacing();

            // Spot lights
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Spot Lights");
            ImGui::Text("  Registered: %u / %u", stats.registeredSpotLights, config.maxSpotLights);
            ImGui::Text("  Active:     %u", stats.activeSpotLights);

            float spotUtil = stats.spotPoolUtilization;
            ImVec4 spotColor = spotUtil > 0.9f ? ImVec4(1, 0.3f, 0.3f, 1)
                             : spotUtil > 0.7f ? ImVec4(1, 0.8f, 0.2f, 1)
                                               : ImVec4(0.3f, 1, 0.3f, 1);
            ImGui::TextColored(spotColor, "  Pool: %.1f%%", spotUtil * 100.0f);
            ImGui::SameLine();
            ImGui::Text("  Frag: %.1f%%", stats.spotFragmentation * 100.0f);
        }
    }

    void LightStreamingDebugWindow::drawPoolVisualization()
    {
        if (ImGui::CollapsingHeader("Pool Usage"))
        {
            float width = ImGui::GetContentRegionAvail().x;

            // Point light pool bar
            ImGui::Text("Point Pool");
            float pointUsed = static_cast<float>(stats.activePointLights);
            float pointMax = static_cast<float>(config.maxPointLights);
            if (pointMax > 0)
            {
                char overlay[64];
                snprintf(overlay, sizeof(overlay), "%u / %u", stats.activePointLights, config.maxPointLights);
                ImGui::ProgressBar(pointUsed / pointMax, ImVec2(width, 20), overlay);
            }

            // Spot light pool bar
            ImGui::Text("Spot Pool");
            float spotUsed = static_cast<float>(stats.activeSpotLights);
            float spotMax = static_cast<float>(config.maxSpotLights);
            if (spotMax > 0)
            {
                char overlay[64];
                snprintf(overlay, sizeof(overlay), "%u / %u", stats.activeSpotLights, config.maxSpotLights);
                ImGui::ProgressBar(spotUsed / spotMax, ImVec2(width, 20), overlay);
            }

            // Budget exclusion indicator
            if (stats.excludedByBudget > 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                   "%u lights excluded by budget", stats.excludedByBudget);
            }
        }
    }

    void LightStreamingDebugWindow::drawConfigSection()
    {
        if (ImGui::CollapsingHeader("Configuration"))
        {
            ImGui::Text("Budget Limits");
            int maxPoint = static_cast<int>(config.maxPointLights);
            if (ImGui::SliderInt("Max Point Lights", &maxPoint, 64, 4096))
            {
                config.maxPointLights = static_cast<uint32_t>(maxPoint);
                configDirty = true;
            }

            int maxSpot = static_cast<int>(config.maxSpotLights);
            if (ImGui::SliderInt("Max Spot Lights", &maxSpot, 32, 2048))
            {
                config.maxSpotLights = static_cast<uint32_t>(maxSpot);
                configDirty = true;
            }

            ImGui::Spacing();
            ImGui::Text("Priority Weights");
            if (ImGui::SliderFloat("Distance", &config.distanceWeight, 0.0f, 5.0f))
                configDirty = true;
            if (ImGui::SliderFloat("Intensity", &config.intensityWeight, 0.0f, 5.0f))
                configDirty = true;
            if (ImGui::SliderFloat("Radius", &config.radiusWeight, 0.0f, 5.0f))
                configDirty = true;
            if (ImGui::SliderFloat("Shadow Bonus", &config.shadowWeight, 0.0f, 10.0f))
                configDirty = true;
            if (ImGui::SliderFloat("Static Bonus", &config.staticBonus, 0.0f, 2.0f))
                configDirty = true;

            ImGui::Spacing();
            if (ImGui::SliderFloat("Hysteresis Margin", &config.hysteresisMargin, 0.0f, 0.5f))
                configDirty = true;
            if (ImGui::SliderFloat("Static Hysteresis x", &config.staticHysteresisMultiplier, 1.0f, 5.0f))
                configDirty = true;

            ImGui::Spacing();
            if (configDirty)
            {
                if (ImGui::Button("Apply"))
                {
                    applyConfig();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset"))
                {
                    config = render::lighting::LightStreamingConfig{};
                    applyConfig();
                }
            }
        }
    }

    void LightStreamingDebugWindow::loadConfig()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        try
        {
            config = dispatcher.query(events::render::lightstreaming::GetLightStreamingConfigQuery{});
        }
        catch (const std::exception& e)
        {
            vfLogWarning("LightStreamingDebugWindow: Failed to load config: {}", e.what());
            config = render::lighting::LightStreamingConfig{};
        }
        catch (...)
        {
            config = render::lighting::LightStreamingConfig{};
        }
        configLoaded = true;
        configDirty = false;
    }

    void LightStreamingDebugWindow::applyConfig()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        events::render::lightstreaming::SetLightStreamingConfigCommand cmd;
        cmd.config = config;
        dispatcher.execute(cmd);

        configDirty = false;
    }

    void LightStreamingDebugWindow::show()
    {
        visible = true;
    }

    void LightStreamingDebugWindow::notifySceneLoaded()
    {
        configLoaded = false;
    }
}
