#include "VolumetricFogConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>

namespace windows
{
    void VolumetricFogConfigWindow::show()
    {
        visible = true;
        if (!settingsLoaded)
        {
            loadSettings();
        }
    }

    void VolumetricFogConfigWindow::notifySceneLoaded()
    {
        settingsLoaded = false;
        if (visible)
        {
            loadSettings();
        }
    }

    void VolumetricFogConfigWindow::loadSettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto fullSettings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
        settings = fullSettings.volumetricFog;
        settingsLoaded = true;
        isDirty = false;
    }

    void VolumetricFogConfigWindow::applySettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto fullSettings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
        fullSettings.volumetricFog = settings;

        events::postprocess::ApplyPostProcessSettingsCommand settingsCmd;
        settingsCmd.settings = fullSettings;
        dispatcher.execute(settingsCmd);

        auto renderSettings = dispatcher.query(events::scene::GetRenderSettingsQuery{});
        renderSettings.postProcess = fullSettings;
        events::scene::SetRenderSettingsCommand sceneCmd;
        sceneCmd.settings = renderSettings;
        dispatcher.execute(sceneCmd);

        isDirty = false;
    }

    void VolumetricFogConfigWindow::resetToDefaults()
    {
        settings = postprocess::VolumetricFogSettings{};
        isDirty = true;
    }

    void VolumetricFogConfigWindow::drawDensitySection()
    {
        ImGui::Text("Fog Density");
        ImGui::Separator();

        if (ImGui::DragFloat("Uniform Density", &settings.uniformDensity, 0.001f, 0.0f, 1.0f, "%.3f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Constant fog density across the entire volume.");
        }

        if (ImGui::ColorEdit3("Fog Color", settings.fogColor))
        {
            isDirty = true;
        }
    }

    void VolumetricFogConfigWindow::drawHeightFogSection()
    {
        ImGui::Spacing();
        ImGui::Text("Height Fog");
        ImGui::Separator();

        if (ImGui::DragFloat("Height Density", &settings.heightFogDensity, 0.001f, 0.0f, 1.0f, "%.3f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Density of exponential height-based fog.\n0 = no height fog.");
        }

        if (ImGui::DragFloat("Height Falloff", &settings.heightFogFalloff, 0.01f, 0.0f, 5.0f, "%.2f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("How quickly height fog diminishes with altitude.\nHigher = fog concentrated closer to ground.");
        }

        if (ImGui::DragFloat("Height Offset", &settings.heightFogOffset, 0.1f, -100.0f, 100.0f, "%.1f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Vertical offset for the height fog base level.");
        }
    }

    void VolumetricFogConfigWindow::drawNoiseSection()
    {
        ImGui::Spacing();
        ImGui::Text("Noise / Turbulence");
        ImGui::Separator();

        if (ImGui::Checkbox("Enable Noise##vfog", &settings.noiseEnabled))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Modulate fog density with 3D noise for organic look.");
        }

        if (settings.noiseEnabled)
        {
            if (ImGui::DragFloat("Noise Scale", &settings.noiseScale, 0.001f, 0.001f, 1.0f, "%.4f"))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("World-space scale for noise UVW.\nSmaller = larger noise features.");
            }

            if (ImGui::SliderFloat("Noise Intensity", &settings.noiseIntensity, 0.0f, 1.0f, "%.2f"))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("How strongly noise modulates fog density.\n0 = no effect, 1 = full modulation.");
            }

            if (ImGui::DragFloat("Noise Speed", &settings.noiseSpeed, 0.01f, 0.0f, 2.0f, "%.3f"))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Animation speed of the noise pattern.\n0 = static noise.");
            }

            if (ImGui::SliderInt("Noise Octaves", &settings.noiseOctaves, 1, 4))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("FBM octaves for noise sampling.\nMore octaves = finer detail but higher cost.");
            }
        }
    }

    void VolumetricFogConfigWindow::drawScatteringSection()
    {
        ImGui::Spacing();
        ImGui::Text("Scattering");
        ImGui::Separator();

        if (ImGui::DragFloat("Scattering Coeff", &settings.scatteringCoefficient, 0.01f, 0.0f, 5.0f, "%.2f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("How much light is scattered by the fog.\nHigher = brighter fog around light sources.");
        }

        if (ImGui::DragFloat("Absorption Coeff", &settings.absorptionCoefficient, 0.01f, 0.0f, 5.0f, "%.2f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("How much light is absorbed by the fog.\nHigher = darker, more opaque fog.");
        }

        if (ImGui::SliderFloat("Anisotropy", &settings.anisotropy, -1.0f, 1.0f, "%.2f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Henyey-Greenstein phase function parameter.\n0 = isotropic, >0 = forward scattering (halos around lights),\n<0 = back scattering.");
        }
    }

    void VolumetricFogConfigWindow::drawGeneralSection()
    {
        ImGui::Spacing();
        ImGui::Text("General");
        ImGui::Separator();

        if (ImGui::DragFloat("Intensity##vfog", &settings.intensity, 0.01f, 0.0f, 5.0f, "%.2f"))
        {
            isDirty = true;
        }

        if (ImGui::DragFloat("Ambient Intensity", &settings.ambientIntensity, 0.01f, 0.0f, 2.0f, "%.2f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Amount of ambient light contribution to the fog.\nPrevents fog from being completely black in shadows.");
        }

        if (ImGui::SliderFloat("Temporal Blend", &settings.temporalBlendFactor, 0.0f, 1.0f, "%.2f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Temporal reprojection blend factor.\nHigher = smoother but more ghosting.\n0.9 is a good default.");
        }

        if (ImGui::DragFloat("Max Distance", &settings.maxDistance, 1.0f, 10.0f, 5000.0f, "%.0f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Maximum distance for volumetric fog evaluation.");
        }
    }

    void VolumetricFogConfigWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Volumetric Fog Configuration", &visible))
        {
            if (ImGui::Checkbox("Enable Volumetric Fog", &settings.enabled))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Froxel-based volumetric fog with light scattering.\nRequires GPU-driven rendering to be active.");
            }

            if (settings.enabled)
            {
                ImGui::Spacing();

                const char* qualityItems[] = {"Low (80x45x64)", "Medium (160x90x128)", "High (240x135x128)"};
                int currentQuality = static_cast<int>(settings.quality);
                if (ImGui::Combo("Quality##vfog", &currentQuality, qualityItems, 3))
                {
                    settings.quality = static_cast<postprocess::VolumetricQuality>(currentQuality);
                    isDirty = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Resolution of the 3D froxel grid.\nHigher = better quality but more GPU cost.");
                }

                ImGui::Spacing();

                drawDensitySection();
                drawHeightFogSection();
                drawNoiseSection();
                drawScatteringSection();
                drawGeneralSection();
            }

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
