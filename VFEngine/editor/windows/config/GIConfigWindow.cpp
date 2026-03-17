#include "GIConfigWindow.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/GIEvents.hpp"
#include "print/Log.hpp"
#include <imgui.h>

namespace windows
{
    void GIConfigWindow::draw()
    {
        if (!visible) return;

        if (!settingsLoaded)
        {
            loadSettings();
        }

        ImGui::SetNextWindowSize(ImVec2(400, 550), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Global Illumination", &visible))
        {
            drawQualitySection();
            drawProbeSection();
            drawSSGISection();
            drawDebugSection();
            drawStatsSection();

            ImGui::Separator();
            if (ImGui::Button("Reset to Defaults"))
            {
                resetToDefaults();
            }

            if (isDirty)
            {
                ImGui::SameLine();
                if (ImGui::Button("Apply"))
                {
                    applySettings();
                }
            }
        }
        ImGui::End();
    }

    void GIConfigWindow::drawQualitySection()
    {
        if (ImGui::CollapsingHeader("Quality", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("Enabled", &settings.enabled))
            {
                isDirty = true;
            }

            const char* qualityNames[] = {"Off", "Medium (1 Cascade)",
                                           "High (3 Cascades)", "Ultra (4 Cascades)"};
            int qualityIdx = static_cast<int>(settings.quality);
            if (ImGui::Combo("Quality Preset", &qualityIdx, qualityNames, 4))
            {
                auto newQuality = static_cast<render::gi::GIQuality>(qualityIdx);
                settings = render::gi::GISettings::fromQuality(newQuality);
                isDirty = true;
            }
        }
    }

    void GIConfigWindow::drawProbeSection()
    {
        if (settings.quality < render::gi::GIQuality::Medium) return;

        if (ImGui::CollapsingHeader("Radiance Cascade Probes"))
        {
            if (ImGui::SliderFloat("Probe Spacing (m)", &settings.probeSpacing, 1.0f, 8.0f))
                isDirty = true;
            if (ImGui::SliderFloat("Cascade Multiplier", &settings.cascadeMultiplier, 1.5f, 4.0f))
                isDirty = true;

            int raysPerUpdate = static_cast<int>(settings.probeRaysPerUpdate);
            if (ImGui::SliderInt("Rays Per Probe", &raysPerUpdate, 16, 512))
            {
                settings.probeRaysPerUpdate = static_cast<uint32_t>(raysPerUpdate);
                isDirty = true;
            }

            if (ImGui::SliderFloat("Temporal Blend", &settings.temporalBlendFactor, 0.5f, 0.99f))
                isDirty = true;
            if (ImGui::SliderFloat("Update Rate", &settings.probeUpdateRate, 0.05f, 1.0f))
                isDirty = true;
            if (ImGui::SliderFloat("Max Probe Distance", &settings.maxProbeDistance, 50.0f, 500.0f))
                isDirty = true;
        }
    }

    void GIConfigWindow::drawSSGISection()
    {
        if (!settings.enabled) return;

        if (ImGui::CollapsingHeader("Screen-Space GI"))
        {
            if (ImGui::Checkbox("Enable SSGI", &settings.ssgiEnabled))
                isDirty = true;

            if (settings.ssgiEnabled)
            {
                if (ImGui::DragFloat("Intensity##ssgi", &settings.ssgiIntensity, 0.01f, 0.0f, 2.0f, "%.2f"))
                    isDirty = true;
                if (ImGui::DragFloat("Radius##ssgi", &settings.ssgiRadius, 0.1f, 0.5f, 10.0f, "%.1f"))
                    isDirty = true;
                if (ImGui::DragFloat("Max Distance##ssgi", &settings.ssgiMaxDistance, 1.0f, 10.0f, 500.0f, "%.0f"))
                    isDirty = true;
                if (ImGui::SliderInt("Sample Count##ssgi", &settings.ssgiSampleCount, 4, 16))
                    isDirty = true;
                if (ImGui::DragFloat("Temporal Blend##ssgi", &settings.ssgiTemporalBlend, 0.01f, 0.05f, 0.5f, "%.2f"))
                    isDirty = true;
                if (ImGui::Checkbox("Half Resolution##ssgi", &settings.ssgiHalfResolution))
                    isDirty = true;
            }
        }
    }

    void GIConfigWindow::drawDebugSection()
    {
        if (ImGui::CollapsingHeader("Debug Visualization"))
        {
            if (ImGui::Checkbox("Show Probes", &settings.showProbes))
                isDirty = true;
            if (ImGui::Checkbox("Show Cascade Bounds", &settings.showCascadeBounds))
                isDirty = true;
            if (ImGui::Checkbox("Show Probe Validity", &settings.showProbeValidity))
                isDirty = true;
        }
    }

    void GIConfigWindow::drawStatsSection()
    {
        if (ImGui::CollapsingHeader("Statistics"))
        {
            auto& dispatcher = ::events::EventDispatcher::instance();

            try
            {
                auto stats = dispatcher.query(events::render::gi::GetGIDebugStatsQuery{});
                ImGui::Text("Total Probes: %u", stats.totalProbes);
                ImGui::Text("Active Cascades: %u", stats.activeCascades);
                ImGui::Text("Probes Updated/Frame: %u", stats.probesUpdatedThisFrame);
            }
            catch (const std::exception& e)
            {
                vfLogWarning("GIConfigWindow: Failed to query GI stats: {}", e.what());
                ImGui::TextDisabled("Stats unavailable");
            }
            catch (...)
            {
                vfLogWarning("GIConfigWindow: Failed to query GI stats (unknown error)");
                ImGui::TextDisabled("Stats unavailable");
            }
        }
    }

    void GIConfigWindow::loadSettings()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        try
        {
            settings = dispatcher.query(events::render::gi::GetGISettingsQuery{});
        }
        catch (const std::exception& e)
        {
            vfLogWarning("GIConfigWindow: Failed to load GI settings: {}", e.what());
            settings = render::gi::GISettings{};
        }
        catch (...)
        {
            vfLogWarning("GIConfigWindow: Failed to load GI settings (unknown error)");
            settings = render::gi::GISettings{};
        }

        settingsLoaded = true;
        isDirty = false;
    }

    void GIConfigWindow::applySettings()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        events::render::gi::ApplyGISettingsCommand cmd;
        cmd.settings = settings;
        dispatcher.execute(cmd);

        isDirty = false;
    }

    void GIConfigWindow::resetToDefaults()
    {
        settings = render::gi::GISettings{};
        isDirty = true;
        applySettings();
    }

    void GIConfigWindow::show()
    {
        visible = true;
    }

    void GIConfigWindow::notifySceneLoaded()
    {
        settingsLoaded = false;
    }
}
