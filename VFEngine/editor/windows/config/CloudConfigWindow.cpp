#include "CloudConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/CloudEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include <imgui.h>

namespace windows
{
    void CloudConfigWindow::loadSettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        settings = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
        settingsLoaded = true;
        isDirty = false;
    }

    void CloudConfigWindow::applySettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::cloud::ApplyCloudSettingsCommand cmd;
        cmd.settings = settings;
        dispatcher.execute(cmd);

        // Sync to scene render settings
        auto renderSettings = dispatcher.query(events::scene::GetRenderSettingsQuery{});
        renderSettings.cloud = settings;
        events::scene::SetRenderSettingsCommand sceneCmd;
        sceneCmd.settings = renderSettings;
        dispatcher.execute(sceneCmd);

        isDirty = false;
    }

    void CloudConfigWindow::drawLayerSection()
    {
        if (ImGui::CollapsingHeader("Cloud Layer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            isDirty |= ImGui::DragFloat("Min Altitude (m)", &settings.cloudMinAltitude, 50.0f, 100.0f, 10000.0f);
            isDirty |= ImGui::DragFloat("Max Altitude (m)", &settings.cloudMaxAltitude, 50.0f, 500.0f, 15000.0f);

            if (settings.cloudMinAltitude >= settings.cloudMaxAltitude)
                settings.cloudMaxAltitude = settings.cloudMinAltitude + 500.0f;
        }
    }

    void CloudConfigWindow::drawDensitySection()
    {
        if (ImGui::CollapsingHeader("Density & Coverage", ImGuiTreeNodeFlags_DefaultOpen))
        {
            isDirty |= ImGui::SliderFloat("Global Density", &settings.globalDensity, 0.0f, 1.0f);
            isDirty |= ImGui::SliderFloat("Global Coverage", &settings.globalCoverage, 0.0f, 1.0f);
            isDirty |= ImGui::SliderFloat("Cloud Type", &settings.cloudType, 0.0f, 1.0f, "%.2f (Stratus-Cumulus)");
            isDirty |= ImGui::ColorEdit3("Cloud Color", &settings.cloudColorTint.x);
        }
    }

    void CloudConfigWindow::drawNoiseSection()
    {
        if (ImGui::CollapsingHeader("Noise Shaping"))
        {
            isDirty |= ImGui::DragFloat("Shape Scale", &settings.shapeScale, 0.00001f, 0.0001f, 0.01f, "%.5f");
            isDirty |= ImGui::DragFloat("Detail Scale", &settings.detailScale, 0.0001f, 0.001f, 0.1f, "%.4f");
            isDirty |= ImGui::SliderFloat("Erosion Strength", &settings.erosionStrength, 0.0f, 1.0f);
            isDirty |= ImGui::SliderFloat("Curl Strength", &settings.curlStrength, 0.0f, 1.0f);
        }
    }

    void CloudConfigWindow::drawWindSection()
    {
        if (ImGui::CollapsingHeader("Wind"))
        {
            isDirty |= ImGui::DragFloat("Wind Speed (m/s)", &settings.windSpeed, 0.5f, 0.0f, 100.0f);
            isDirty |= ImGui::SliderFloat("Wind Direction", &settings.windDirectionDeg, 0.0f, 360.0f, "%.0f deg");
        }
    }

    void CloudConfigWindow::drawLightingSection()
    {
        if (ImGui::CollapsingHeader("Lighting"))
        {
            isDirty |= ImGui::SliderFloat("Light Absorption", &settings.lightAbsorption, 0.0f, 2.0f);
            isDirty |= ImGui::SliderFloat("Phase Forward (g1)", &settings.phaseForward, 0.0f, 0.999f);
            isDirty |= ImGui::SliderFloat("Phase Backward (g2)", &settings.phaseBackward, -0.999f, 0.0f);
            isDirty |= ImGui::SliderFloat("Phase Blend", &settings.phaseBlend, 0.0f, 1.0f);
            isDirty |= ImGui::SliderFloat("Ambient Intensity", &settings.ambientIntensity, 0.0f, 2.0f);

            ImGui::Separator();
            isDirty |= ImGui::SliderFloat("Silver Lining Intensity", &settings.silverLiningIntensity, 0.0f, 2.0f);
            isDirty |= ImGui::SliderFloat("Silver Lining Spread", &settings.silverLiningSpread, 1.0f, 20.0f);
            isDirty |= ImGui::SliderFloat("Multi-Scatter Boost", &settings.multiScatterBoost, 0.0f, 2.0f);
        }
    }

    void CloudConfigWindow::drawPerformanceSection()
    {
        if (ImGui::CollapsingHeader("Performance"))
        {
            isDirty |= ImGui::SliderFloat("Temporal Blend", &settings.temporalBlendFactor, 0.0f, 0.99f);

            int maxSteps = static_cast<int>(settings.maxMarchSteps);
            if (ImGui::SliderInt("Max March Steps", &maxSteps, 16, 256))
            {
                settings.maxMarchSteps = static_cast<uint32_t>(maxSteps);
                isDirty = true;
            }

            int lightSteps = static_cast<int>(settings.lightMarchSteps);
            if (ImGui::SliderInt("Light March Steps", &lightSteps, 2, 16))
            {
                settings.lightMarchSteps = static_cast<uint32_t>(lightSteps);
                isDirty = true;
            }
        }
    }

    void CloudConfigWindow::draw()
    {
        if (!visible) return;

        if (!settingsLoaded)
            loadSettings();

        ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Cloud Configuration", &visible))
        {
            if (ImGui::Checkbox("Enable Clouds", &settings.enabled))
                isDirty = true;

            if (settings.enabled)
            {
                drawLayerSection();
                drawDensitySection();
                drawNoiseSection();
                drawWindSection();
                drawLightingSection();
                drawPerformanceSection();
            }

            ImGui::Separator();

            if (ImGui::Button("Apply"))
                applySettings();
            ImGui::SameLine();
            if (ImGui::Button("Reload"))
                loadSettings();
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults"))
            {
                settings = render::cloud::CloudSettings{};
                isDirty = true;
            }

            if (isDirty)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
            }
        }
        ImGui::End();
    }
}
