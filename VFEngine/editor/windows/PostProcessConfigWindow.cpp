#include "PostProcessConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/PostProcessEvents.hpp"
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

        settings.enabled = dispatcher.query(events::postprocess::GetPostProcessEnabledQuery{});
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

                if (ImGui::DragFloat("Exposure", &settings.toneMapping.exposure, 0.01f, 0.01f, 10.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Controls scene brightness before tone mapping.");
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
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawFXAASection()
    {
        if (ImGui::CollapsingHeader("FXAA", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable FXAA", &settings.fxaa.enabled))
            {
                isDirty = true;
            }

            if (settings.fxaa.enabled)
            {
                ImGui::Spacing();

                const char* qualityItems[] = {"Low", "Medium", "High"};
                int currentQuality = static_cast<int>(settings.fxaa.quality);
                if (ImGui::Combo("Quality", &currentQuality, qualityItems, 3))
                {
                    settings.fxaa.quality = static_cast<postprocess::FXAAQuality>(currentQuality);
                    isDirty = true;
                }

                if (ImGui::DragFloat("Edge Threshold Min", &settings.fxaa.edgeThresholdMin, 0.001f, 0.01f, 0.1f, "%.4f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Minimum luminance threshold for edge detection.\nLower = more edges detected.");
                }

                if (ImGui::DragFloat("Edge Threshold", &settings.fxaa.edgeThreshold, 0.001f, 0.05f, 0.5f, "%.4f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Maximum luminance threshold for edge detection.\nLower = more aggressive anti-aliasing.");
                }
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

    void PostProcessConfigWindow::drawGodRaysSection()
    {
        if (ImGui::CollapsingHeader("God Rays", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable God Rays", &settings.godRays.enabled))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Requires a directional light in the scene.\nThe sun position is derived from the first directional light.");
            }

            if (settings.godRays.enabled)
            {
                ImGui::Spacing();

                if (ImGui::DragFloat("Intensity##godrays", &settings.godRays.intensity, 0.01f, 0.0f, 2.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Overall brightness of the god rays effect.");
                }

                if (ImGui::DragFloat("Decay", &settings.godRays.decay, 0.001f, 0.9f, 1.0f, "%.3f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Exponential falloff per sample step.\nCloser to 1.0 = longer rays.");
                }

                if (ImGui::DragFloat("Density##godrays", &settings.godRays.density, 0.01f, 0.1f, 2.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Controls spacing between sample steps.\nHigher = denser sampling.");
                }

                if (ImGui::DragFloat("Weight##godrays", &settings.godRays.weight, 0.01f, 0.0f, 2.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Per-sample contribution weight.");
                }

                int samples = settings.godRays.sampleCount;
                if (ImGui::SliderInt("Samples", &samples, 16, 128))
                {
                    settings.godRays.sampleCount = samples;
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Number of ray marching steps.\nMore = higher quality but slower.");
                }

                if (ImGui::DragFloat("Threshold##godrays", &settings.godRays.threshold, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Depth threshold for sky detection.\nPixels with depth >= threshold contribute light.");
                }
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PostProcessConfigWindow::drawDepthOfFieldSection()
    {
        if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Depth of Field", &settings.depthOfField.enabled))
            {
                isDirty = true;
            }

            if (settings.depthOfField.enabled)
            {
                ImGui::Spacing();

                if (ImGui::DragFloat("Focal Distance", &settings.depthOfField.focalDistance, 0.1f, 0.1f, 1000.0f, "%.1f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Distance at which objects are in perfect focus.");
                }

                if (ImGui::DragFloat("Focal Range", &settings.depthOfField.focalRange, 0.1f, 0.1f, 100.0f, "%.1f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Range around the focal distance that remains sharp.\nSmaller = shallower depth of field.");
                }

                if (ImGui::DragFloat("Max Blur Radius", &settings.depthOfField.maxBlurRadius, 0.1f, 0.0f, 20.0f, "%.1f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Maximum blur amount in pixels for fully out-of-focus areas.");
                }

                int samples = settings.depthOfField.sampleCount;
                if (ImGui::SliderInt("Samples##dof", &samples, 4, 32))
                {
                    settings.depthOfField.sampleCount = samples;
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Number of Poisson disc samples.\nMore = smoother blur but slower.");
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
            drawFXAASection();
            drawBloomSection();
            drawVignetteSection();
            drawChromaticAberrationSection();
            drawFilmGrainSection();
            drawGodRaysSection();
            drawDepthOfFieldSection();

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
