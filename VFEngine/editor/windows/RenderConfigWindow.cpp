#include "RenderConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/RenderEvents.hpp"
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
        auto& dispatcher = events::EventDispatcher::instance();

        // Save settings to scene
        events::scene::SetRenderSettingsCommand sceneCmd;
        sceneCmd.settings = settings;
        dispatcher.execute(sceneCmd);

        // Apply shadow settings to the shadow system
        events::render::ApplyShadowSettingsCommand shadowCmd;
        shadowCmd.settings = settings;
        dispatcher.execute(shadowCmd);
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
                    // Update atlas config from quality
                    settings.shadows.atlas = types::ShadowAtlasConfig::fromQuality(settings.shadows.quality);
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Click 'Apply' to change quality.\nThis may cause a brief stutter while the shadow atlas is resized.");
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

                ImGui::Separator();
                ImGui::Text("Shadow Filtering");
                ImGui::Spacing();

                // PCF Kernel Size
                const char* kernelItems[] = {"None (Hard)", "3x3", "5x5", "7x7"};
                int kernelIdx = static_cast<int>(settings.shadows.pcfKernelSize);
                if (ImGui::Combo("PCF Kernel", &kernelIdx, kernelItems, 4))
                {
                    settings.shadows.pcfKernelSize = static_cast<types::PCFKernelSize>(kernelIdx);
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("PCF kernel size for soft shadow edges.\nLarger = softer but slower.");
                }

                // Soft Shadows Toggle
                if (ImGui::Checkbox("Soft Shadows", &settings.shadows.softShadowsEnabled))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Enable/disable PCF shadow filtering globally.");
                }

                ImGui::Separator();
                ImGui::Text("Shadow Darkness");
                ImGui::Spacing();

                // Shadow Intensity
                if (ImGui::SliderFloat("Shadow Intensity", &settings.shadows.shadowIntensity, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Controls how dark shadowed areas are.\n"
                                      "0.0 = Lighter shadows (ambient light in shadows)\n"
                                      "1.0 = Darker shadows (no ambient in shadows)");
                }
            }

            // Shadow Debug Visualization (visible regardless of shadow state)
            ImGui::Separator();
            ImGui::Text("Debug Visualization");
            ImGui::Spacing();

            auto& dispatcher = events::EventDispatcher::instance();
            bool showShadowDebug = dispatcher.query(events::render::GetShowShadowDebugQuery{});

            if (ImGui::Checkbox("Show Shadow Frustums", &showShadowDebug))
            {
                events::render::SetShowShadowDebugCommand cmd;
                cmd.show = showShadowDebug;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Visualize shadow map frustums:\n"
                                  "- Directional: Cascade boxes (red->green)\n"
                                  "- Spot: Perspective frustum (cyan)\n"
                                  "- Point: Sphere radius (magenta)");
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

        if (ImGui::Begin("Render Configuration", &visible))
        {
            // Shadow settings section
            drawShadowSection();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Action buttons at bottom
            if (ImGui::Button("Save to Scene", ImVec2(100, 0)))
            {
                saveToScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload", ImVec2(80, 0)))
            {
                loadFromScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(80, 0)))
            {
                applySettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults", ImVec2(100, 0)))
            {
                resetToDefaults();
            }

            if (isDirty)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Render settings are saved with the scene file.");
        }
        ImGui::End();
    }
}
