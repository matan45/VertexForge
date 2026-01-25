#include "RenderConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>
#include <algorithm>

namespace windows
{
    void RenderConfigWindow::show()
    {
        visible = true;
        if (!settingsLoaded)
        {
            loadFromScene();
        }
    }

    void RenderConfigWindow::loadFromScene()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::GetRenderSettingsQuery query;
        settings = dispatcher.query(query);
        settingsLoaded = true;
        isDirty = false;
    }

    void RenderConfigWindow::saveToScene()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::SetRenderSettingsCommand cmd;
        cmd.settings = settings;
        dispatcher.execute(cmd);
        isDirty = false;
    }

    void RenderConfigWindow::resetToDefaults()
    {
        settings = types::RenderSettings::createDefault();
        isDirty = true;
    }

    void RenderConfigWindow::applySettings()
    {
    }

    void RenderConfigWindow::drawShadowSection()
    {
        if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Shadows", &settings.shadows.enabled))
            {
                isDirty = true;
            }

            if (settings.shadows.enabled)
            {
                ImGui::Spacing();

                // Shadow Quality
                const char* qualityItems[] = {"Off", "Low (512)", "Medium (1024)", "High (2048)", "Ultra (4096)"};
                int currentQuality = static_cast<int>(settings.shadows.quality);
                if (ImGui::Combo("Quality", &currentQuality, qualityItems, 5))
                {
                    settings.shadows.quality = static_cast<types::ShadowQuality>(currentQuality);
                    isDirty = true;
                }

                ImGui::Separator();
                ImGui::Text("Directional Light (CSM)");
                ImGui::Spacing();

                // Cascade Count
                int cascades = settings.shadows.cascadeCount;
                if (ImGui::SliderInt("Cascade Count", &cascades, 1, 4))
                {
                    settings.shadows.cascadeCount = static_cast<uint8_t>(cascades);
                    isDirty = true;
                }

                // Cascade Split Mode
                const char* splitModes[] = {"Linear", "Logarithmic", "Practical"};
                int currentMode = static_cast<int>(settings.shadows.cascadeSplitMode);
                if (ImGui::Combo("Split Mode", &currentMode, splitModes, 3))
                {
                    settings.shadows.cascadeSplitMode = static_cast<types::CascadeSplitMode>(currentMode);
                    isDirty = true;
                }

                ImGui::Separator();
                ImGui::Text("Bias Settings");
                ImGui::Spacing();

                if (ImGui::DragFloat("Shadow Bias", &settings.shadows.shadowBias, 0.0001f, 0.0f, 0.1f, "%.4f"))
                {
                    isDirty = true;
                }
                if (ImGui::DragFloat("Normal Bias", &settings.shadows.normalBias, 0.001f, 0.0f, 1.0f, "%.3f"))
                {
                    isDirty = true;
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void RenderConfigWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);

        std::string title = "Render Configuration";
        if (isDirty)
        {
            title += " (Modified)";
        }
        title += "###RenderConfig";

        if (ImGui::Begin(title.c_str(), &visible))
        {
            // Shadow settings section
            drawShadowSection();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Action buttons
            if (ImGui::Button("Save to Scene"))
            {
                saveToScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload"))
            {
                loadFromScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply"))
            {
                applySettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults"))
            {
                resetToDefaults();
            }
        }
        ImGui::End();
    }
}
