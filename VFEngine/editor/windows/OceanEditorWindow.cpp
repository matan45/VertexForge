#include "OceanEditorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/WaterEvents.hpp"
#include <imgui.h>

namespace windows
{
    void OceanEditorWindow::show()
    {
        visible = true;
        refreshState();
    }

    void OceanEditorWindow::refreshState()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::water::IsOceanFFTEnabledQuery enabledQuery;
        oceanActive = dispatcher.query(enabledQuery);

        events::water::GetOceanFFTConfigQuery configQuery;
        config = dispatcher.query(configQuery);
        configDirty = false;
    }

    void OceanEditorWindow::draw()
    {
        if (!visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(380, 450), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Ocean FFT Settings", &visible))
        {
            // Enable/disable toggle
            if (ImGui::Checkbox("Enable Ocean FFT", &config.enabled))
            {
                events::water::SetOceanFFTEnabledCommand cmd;
                cmd.enabled = config.enabled;
                events::EventDispatcher::instance().execute(cmd);
                oceanActive = config.enabled;
            }
            ImGui::TextDisabled("Replaces Gerstner waves with FFT-based ocean simulation");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (oceanActive)
                drawConfigSection();
            else
                ImGui::TextDisabled("Enable Ocean FFT to configure parameters");
        }
        ImGui::End();
    }

    void OceanEditorWindow::drawConfigSection()
    {
        if (ImGui::CollapsingHeader("Spectrum", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("FFT Resolution");
            ImGui::PushItemWidth(-1);
            static const uint32_t resolutions[] = {64, 128, 256, 512};
            int resIndex = 2; // default 256
            for (int i = 0; i < 4; ++i)
            {
                if (resolutions[i] == config.resolution) { resIndex = i; break; }
            }
            if (ImGui::Combo("##Resolution", &resIndex, "64\0128\0256\0512\0"))
            {
                config.resolution = resolutions[resIndex];
                configDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Patch Size (world units)");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##PatchSize", &config.patchSize, 1.0f, 10.0f, 2000.0f, "%.0f"))
                configDirty = true;
            ImGui::PopItemWidth();

            ImGui::Text("Amplitude");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##Amplitude", &config.amplitude, 0.00001f, 0.00001f, 0.01f, "%.5f"))
                configDirty = true;
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Wind Speed (m/s)");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##WindSpeed", &config.windSpeed, 0.1f, 0.1f, 100.0f, "%.1f"))
                configDirty = true;
            ImGui::PopItemWidth();

            ImGui::Text("Wind Direction");
            ImGui::PushItemWidth(-1);
            if (ImGui::SliderFloat("##WindDir", &config.windDirection, 0.0f, 360.0f, "%.0f deg"))
                configDirty = true;
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Displacement", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Choppiness");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##Choppiness", &config.choppiness, 0.01f, 0.0f, 5.0f, "%.2f"))
                configDirty = true;
            ImGui::PopItemWidth();

            ImGui::Text("Gravity");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##Gravity", &config.gravity, 0.01f, 1.0f, 20.0f, "%.2f"))
                configDirty = true;
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Foam", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Foam Threshold");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##FoamThreshold", &config.foamThreshold, 0.01f, -1.0f, 2.0f, "%.2f"))
                configDirty = true;
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Lower = more foam");

            ImGui::Unindent();
        }

        ImGui::Spacing();

        if (ImGui::Button("Apply", ImVec2(100, 0)))
            applyConfig();

        if (configDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(80, 0)))
            refreshState();
    }

    void OceanEditorWindow::applyConfig()
    {
        events::water::SetOceanFFTConfigCommand cmd;
        cmd.config = config;
        events::EventDispatcher::instance().execute(cmd);
        configDirty = false;
    }
}
