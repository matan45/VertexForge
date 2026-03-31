#include "PostProcessConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>

namespace windows
{
    void PostProcessConfigWindow::show()
    {
        visible = true;
        if (!settingsLoaded)
        {
            loadSettings();
        }
    }

    void PostProcessConfigWindow::notifySceneLoaded()
    {
        settingsLoaded = false;
        if (visible)
        {
            loadSettings();
        }
    }

    void PostProcessConfigWindow::loadSettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
        settingsLoaded = true;
        isDirty = false;
    }

    void PostProcessConfigWindow::applySettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::postprocess::ApplyPostProcessSettingsCommand settingsCmd;
        settingsCmd.settings = settings;
        dispatcher.execute(settingsCmd);

        auto renderSettings = dispatcher.query(events::scene::GetRenderSettingsQuery{});
        renderSettings.postProcess = settings;
        events::scene::SetRenderSettingsCommand sceneCmd;
        sceneCmd.settings = renderSettings;
        dispatcher.execute(sceneCmd);

        isDirty = false;
    }

    void PostProcessConfigWindow::resetToDefaults()
    {
        settings = postprocess::PostProcessSettings::createDefault();
        isDirty = true;
    }

    void PostProcessConfigWindow::drawToneMappingSection()
    {
        if (ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Tone Mapping", &settings.toneMapping.enabled))
            {
                isDirty = true;
            }

            if (settings.toneMapping.enabled)
            {
                ImGui::Spacing();

                const char* modeItems[] = {"ACES", "Reinhard", "Uncharted2", "Linear", "Gran Turismo", "AgX", "Khronos PBR Neutral"};
                int currentMode = static_cast<int>(settings.toneMapping.mode);
                if (ImGui::Combo("Mode", &currentMode, modeItems, 7))
                {
                    settings.toneMapping.mode = static_cast<postprocess::ToneMappingMode>(currentMode);
                    isDirty = true;
                }

                if (ImGui::Checkbox("Auto Exposure", &settings.autoExposure.enabled))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Automatically adjusts exposure based on scene luminance.\nSimulates human eye adaptation.");
                }

                if (settings.autoExposure.enabled)
                {
                    ImGui::Indent(10.0f);

                    if (ImGui::DragFloat("Min Exposure", &settings.autoExposure.minExposure, 0.01f, 0.01f, 1.0f, "%.2f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Minimum allowed exposure value.\nPrevents the scene from becoming too dark.");
                    }

                    if (ImGui::DragFloat("Max Exposure##ae", &settings.autoExposure.maxExposure, 0.1f, 1.0f, 20.0f, "%.1f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Maximum allowed exposure value.\nPrevents the scene from becoming too bright.");
                    }

                    if (ImGui::DragFloat("Adapt Speed Up", &settings.autoExposure.adaptSpeedUp, 0.1f, 0.1f, 10.0f, "%.1f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Speed of adaptation when scene gets brighter (EV/sec).");
                    }

                    if (ImGui::DragFloat("Adapt Speed Down", &settings.autoExposure.adaptSpeedDown, 0.1f, 0.1f, 10.0f, "%.1f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Speed of adaptation when scene gets darker (EV/sec).");
                    }

                    if (ImGui::DragFloat("Exposure Compensation", &settings.autoExposure.exposureCompensation, 0.1f, -5.0f, 5.0f, "%.1f EV"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Artistic offset in EV stops.\nPositive = brighter, Negative = darker.");
                    }

                    if (ImGui::DragFloat("Low Percentile", &settings.autoExposure.lowPercentile, 0.01f, 0.0f, 0.5f, "%.2f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Excludes the darkest pixels from the average.\nHigher values ignore more shadow areas.");
                    }

                    if (ImGui::DragFloat("High Percentile", &settings.autoExposure.highPercentile, 0.01f, 0.5f, 1.0f, "%.2f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Excludes the brightest pixels from the average.\nLower values ignore more highlight areas (sun, specular).");
                    }

                    ImGui::Unindent(10.0f);
                }
                else
                {
                    if (ImGui::DragFloat("Exposure", &settings.toneMapping.exposure, 0.01f, 0.01f, 10.0f, "%.2f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Controls scene brightness before tone mapping.");
                    }
                }

                if (ImGui::DragFloat("Contrast", &settings.toneMapping.contrast, 0.01f, 0.5f, 2.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Adjusts contrast around mid-gray.\n1.0 = neutral, >1.0 = more contrast, <1.0 = less contrast.");
                }

                if (ImGui::DragFloat("Gamma", &settings.toneMapping.gamma, 0.01f, 1.0f, 3.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Display gamma correction.\n2.2 is standard for most monitors.");
                }

                if (ImGui::DragFloat("Toe", &settings.toneMapping.toe, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Shadow curve strength.\n0.0 = neutral, higher = darker shadows with more contrast.");
                }

                if (ImGui::DragFloat("Shoulder", &settings.toneMapping.shoulder, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Highlight curve strength.\n0.0 = neutral, higher = softer highlight rolloff.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawTAASection()
    {
        if (ImGui::CollapsingHeader("TAA (Temporal Anti-Aliasing)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable TAA", &settings.taa.enabled))
            {
                isDirty = true;
            }

            if (settings.taa.enabled)
            {
                ImGui::Spacing();

                if (ImGui::DragFloat("Blend Factor", &settings.taa.blendFactor, 0.01f, 0.01f, 0.5f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("How much of the current frame to blend in.\nLower = more temporal smoothing, higher = more responsive.");
                }

                if (ImGui::DragFloat("Sharpen Strength", &settings.taa.sharpenStrength, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Contrast-adaptive sharpening intensity.\nCounteracts TAA blur.");
                }

                if (ImGui::Checkbox("Variance Clipping", &settings.taa.useVarianceClipping))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Use variance-based clipping instead of min/max AABB.\nMore robust against ghosting artifacts.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawUpscaleSection()
    {
        if (ImGui::CollapsingHeader("Upscaling (DLSS / FSR 2)"))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Upscaling", &settings.upscale.enabled))
                isDirty = true;

            if (settings.upscale.enabled)
            {
                ImGui::Spacing();

                const char* modeNames[] = {"Off", "DLSS", "FSR 2 (DirectSR)", "Auto"};
                int currentMode = static_cast<int>(settings.upscale.mode);
                if (ImGui::Combo("Mode", &currentMode, modeNames, IM_ARRAYSIZE(modeNames)))
                {
                    settings.upscale.mode = static_cast<postprocess::UpscaleMode>(currentMode);
                    isDirty = true;
                }

                const char* qualityNames[] = {"Native (DLAA)", "Quality (1.5x)", "Balanced (1.7x)",
                                               "Performance (2.0x)", "Ultra Performance (3.0x)"};
                int currentQuality = static_cast<int>(settings.upscale.quality);
                if (ImGui::Combo("Quality", &currentQuality, qualityNames, IM_ARRAYSIZE(qualityNames)))
                {
                    settings.upscale.quality = static_cast<postprocess::UpscaleQuality>(currentQuality);
                    isDirty = true;
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Status");

                auto& dispatcher = events::EventDispatcher::instance();
                auto status = dispatcher.query(events::postprocess::GetUpscaleStatusQuery{});

                ImGui::Text("Streamline: %s", status.streamlineAvailable ? "Available" : "Not Available");
                ImGui::Text("DLSS: %s", status.dlssSupported ? "Supported" : "Not Supported");
                ImGui::Text("DirectSR: %s", status.directSRSupported ? "Supported" : "Not Supported");

                const char* activeModeStr = "Off";
                switch (status.activeMode)
                {
                case postprocess::UpscaleMode::DLSS: activeModeStr = "DLSS"; break;
                case postprocess::UpscaleMode::FSR2: activeModeStr = "FSR 2 (DirectSR)"; break;
                default: break;
                }
                ImGui::Text("Active: %s", activeModeStr);

                if (status.renderWidth > 0 && status.displayWidth > 0)
                {
                    ImGui::Text("Render: %ux%u -> Display: %ux%u",
                                status.renderWidth, status.renderHeight,
                                status.displayWidth, status.displayHeight);
                }

                if (settings.upscale.enabled && !status.streamlineAvailable)
                {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.1f, 1.0f),
                        "Streamline SDK not available. Build Streamline first.");
                }
            }

            if (settings.upscale.enabled && settings.taa.enabled)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.1f, 1.0f),
                    "Note: TAA is automatically disabled when upscaling is active.");
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawBloomSection()
    {
        if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Bloom", &settings.bloom.enabled))
            {
                isDirty = true;
            }

            if (settings.bloom.enabled)
            {
                ImGui::Spacing();

                if (ImGui::DragFloat("Threshold", &settings.bloom.threshold, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Brightness threshold for bloom extraction.\nLower values = more bloom (LDR scenes need ~0.5-0.8).");
                }

                if (ImGui::DragFloat("Intensity##bloom", &settings.bloom.intensity, 0.01f, 0.0f, 2.0f, "%.2f"))
                {
                    isDirty = true;
                }

                if (ImGui::DragFloat("Radius##bloom", &settings.bloom.radius, 0.01f, 0.0f, 2.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Controls how far bloom spreads from bright areas.");
                }

                int passes = static_cast<int>(settings.bloom.passes);
                if (ImGui::SliderInt("Passes", &passes, 1, 10))
                {
                    settings.bloom.passes = static_cast<uint32_t>(passes);
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Number of blur passes.\nMore passes = smoother and wider bloom.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawVignetteSection()
    {
        if (ImGui::CollapsingHeader("Vignette", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Vignette", &settings.vignette.enabled))
            {
                isDirty = true;
            }

            if (settings.vignette.enabled)
            {
                ImGui::Spacing();

                if (ImGui::SliderFloat("Intensity##vignette", &settings.vignette.intensity, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }

                if (ImGui::SliderFloat("Radius##vignette", &settings.vignette.radius, 0.0f, 1.5f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Controls where the vignette starts from the center.");
                }

                if (ImGui::SliderFloat("Softness", &settings.vignette.softness, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Controls the smoothness of the vignette falloff.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawChromaticAberrationSection()
    {
        if (ImGui::CollapsingHeader("Chromatic Aberration", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Chromatic Aberration", &settings.chromaticAberration.enabled))
            {
                isDirty = true;
            }

            if (settings.chromaticAberration.enabled)
            {
                ImGui::Spacing();

                if (ImGui::DragFloat("Intensity##ca", &settings.chromaticAberration.intensity, 0.001f, 0.0f, 0.05f, "%.4f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Controls the amount of color channel separation.\nHigher values create a stronger lens distortion effect.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawFilmGrainSection()
    {
        if (ImGui::CollapsingHeader("Film Grain", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Film Grain", &settings.filmGrain.enabled))
            {
                isDirty = true;
            }

            if (settings.filmGrain.enabled)
            {
                ImGui::Spacing();

                if (ImGui::DragFloat("Intensity##grain", &settings.filmGrain.intensity, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }

                if (ImGui::DragFloat("Size", &settings.filmGrain.size, 0.1f, 0.5f, 5.0f, "%.1f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Controls the size of the grain particles.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Post Process Configuration", &visible))
        {
            if (ImGui::Checkbox("Enable Post Processing", &settings.enabled))
            {
                isDirty = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            drawToneMappingSection();
            drawTAASection();
            drawUpscaleSection();
            drawBloomSection();
            drawVignetteSection();
            drawChromaticAberrationSection();
            drawFilmGrainSection();
            drawDepthOfFieldSection();
            drawSSAOSection();
            drawEdgeDetectionSection();
            drawColorGradingSection();
            drawUnderwaterSection();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Apply", ImVec2(80, 0)))
            {
                applySettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload", ImVec2(80, 0)))
            {
                loadSettings();
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
        }
        ImGui::End();
    }

}
