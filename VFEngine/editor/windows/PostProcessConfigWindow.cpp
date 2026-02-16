#include "PostProcessConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/PostProcessEvents.hpp"
#include "events/SceneEvents.hpp"
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

                const char* focusModes[] = {"Manual", "Target Point"};
                int focusModeIdx = static_cast<int>(settings.depthOfField.focusMode);
                if (ImGui::Combo("Focus Mode", &focusModeIdx, focusModes, IM_ARRAYSIZE(focusModes)))
                {
                    settings.depthOfField.focusMode = static_cast<::postprocess::DoFFocusMode>(focusModeIdx);
                    isDirty = true;
                }

                if (settings.depthOfField.focusMode == ::postprocess::DoFFocusMode::Manual)
                {
                    if (ImGui::DragFloat("Focal Distance", &settings.depthOfField.focalDistance, 0.1f, 0.1f, 1000.0f, "%.1f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Distance at which objects are in perfect focus.");
                    }
                }
                else
                {
                    float focusTarget[3] = {settings.depthOfField.focusTargetX,
                                            settings.depthOfField.focusTargetY,
                                            settings.depthOfField.focusTargetZ};
                    if (ImGui::DragFloat3("Focus Target", focusTarget, 0.1f))
                    {
                        settings.depthOfField.focusTargetX = focusTarget[0];
                        settings.depthOfField.focusTargetY = focusTarget[1];
                        settings.depthOfField.focusTargetZ = focusTarget[2];
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("World-space position to focus on.");
                    }

                    if (ImGui::DragFloat("Focus Smoothing", &settings.depthOfField.focusSmoothing, 0.1f, 0.1f, 50.0f, "%.1f"))
                    {
                        isDirty = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("How quickly focus transitions to the target.\nHigher = faster.");
                    }
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

    void PostProcessConfigWindow::drawVolumetricFogSection()
    {
        if (ImGui::CollapsingHeader("Volumetric Fog", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Volumetric Fog", &settings.volumetricFog.enabled))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Froxel-based volumetric fog with light scattering.\nRequires GPU-driven rendering to be active.");
            }

            if (settings.volumetricFog.enabled)
            {
                ImGui::Spacing();

                const char* qualityItems[] = {"Low (80x45x64)", "Medium (160x90x128)", "High (240x135x128)"};
                int currentQuality = static_cast<int>(settings.volumetricFog.quality);
                if (ImGui::Combo("Quality##vfog", &currentQuality, qualityItems, 3))
                {
                    settings.volumetricFog.quality = static_cast<postprocess::VolumetricQuality>(currentQuality);
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Resolution of the 3D froxel grid.\nHigher = better quality but more GPU cost.");
                }

                ImGui::Spacing();
                ImGui::Text("Fog Density");
                ImGui::Separator();

                if (ImGui::DragFloat("Uniform Density", &settings.volumetricFog.uniformDensity, 0.001f, 0.0f, 1.0f, "%.3f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Constant fog density across the entire volume.");
                }

                if (ImGui::ColorEdit3("Fog Color", settings.volumetricFog.fogColor))
                {
                    isDirty = true;
                }

                ImGui::Spacing();
                ImGui::Text("Height Fog");
                ImGui::Separator();

                if (ImGui::DragFloat("Height Density", &settings.volumetricFog.heightFogDensity, 0.001f, 0.0f, 1.0f, "%.3f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Density of exponential height-based fog.\n0 = no height fog.");
                }

                if (ImGui::DragFloat("Height Falloff", &settings.volumetricFog.heightFogFalloff, 0.01f, 0.0f, 5.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("How quickly height fog diminishes with altitude.\nHigher = fog concentrated closer to ground.");
                }

                if (ImGui::DragFloat("Height Offset", &settings.volumetricFog.heightFogOffset, 0.1f, -100.0f, 100.0f, "%.1f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Vertical offset for the height fog base level.");
                }

                ImGui::Spacing();
                ImGui::Text("Scattering");
                ImGui::Separator();

                if (ImGui::DragFloat("Scattering Coeff", &settings.volumetricFog.scatteringCoefficient, 0.01f, 0.0f, 5.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("How much light is scattered by the fog.\nHigher = brighter fog around light sources.");
                }

                if (ImGui::DragFloat("Absorption Coeff", &settings.volumetricFog.absorptionCoefficient, 0.01f, 0.0f, 5.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("How much light is absorbed by the fog.\nHigher = darker, more opaque fog.");
                }

                if (ImGui::SliderFloat("Anisotropy", &settings.volumetricFog.anisotropy, -1.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Henyey-Greenstein phase function parameter.\n0 = isotropic, >0 = forward scattering (halos around lights),\n<0 = back scattering.");
                }

                ImGui::Spacing();
                ImGui::Text("General");
                ImGui::Separator();

                if (ImGui::DragFloat("Intensity##vfog", &settings.volumetricFog.intensity, 0.01f, 0.0f, 5.0f, "%.2f"))
                {
                    isDirty = true;
                }

                if (ImGui::DragFloat("Ambient Intensity", &settings.volumetricFog.ambientIntensity, 0.01f, 0.0f, 2.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Amount of ambient light contribution to the fog.\nPrevents fog from being completely black in shadows.");
                }

                if (ImGui::SliderFloat("Temporal Blend", &settings.volumetricFog.temporalBlendFactor, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Temporal reprojection blend factor.\nHigher = smoother but more ghosting.\n0.9 is a good default.");
                }

                if (ImGui::DragFloat("Max Distance", &settings.volumetricFog.maxDistance, 1.0f, 10.0f, 5000.0f, "%.0f"))
                {
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Maximum distance for volumetric fog evaluation.");
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
            drawVolumetricFogSection();

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
