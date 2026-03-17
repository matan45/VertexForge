#include "AtmosphereConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/AtmosphereEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>

namespace windows
{
    void AtmosphereConfigWindow::show()
    {
        visible = true;
        settingsLoaded = false;
    }

    void AtmosphereConfigWindow::notifySceneLoaded()
    {
        settingsLoaded = false;
        isDirty = false;
    }

    void AtmosphereConfigWindow::loadSettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        settings = dispatcher.query(events::atmosphere::GetAtmosphereSettingsQuery{});
        settingsLoaded = true;
        isDirty = false;
    }

    void AtmosphereConfigWindow::applySettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::atmosphere::ApplyAtmosphereSettingsCommand cmd;
        cmd.settings = settings;
        dispatcher.execute(cmd);

        // Also sync to scene render settings
        auto renderSettings = dispatcher.query(events::scene::GetRenderSettingsQuery{});
        renderSettings.atmosphere = settings;
        events::scene::SetRenderSettingsCommand sceneCmd;
        sceneCmd.settings = renderSettings;
        dispatcher.execute(sceneCmd);

        isDirty = false;
    }

    void AtmosphereConfigWindow::resetToDefaults()
    {
        settings = render::atmosphere::AtmosphereSettings{};
        isDirty = true;
    }

    void AtmosphereConfigWindow::drawPlanetSection()
    {
        ImGui::Text("Planet");
        ImGui::Separator();

        if (ImGui::DragFloat("Planet Radius", &settings.planetRadius, 1000.0f, 100000.0f, 100000000.0f, "%.0f m"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Radius of the planet surface (Earth ~6360km).");

        if (ImGui::DragFloat("Atmosphere Radius", &settings.atmosphereRadius, 1000.0f, 100000.0f, 100000000.0f, "%.0f m"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Outer radius of the atmosphere (Earth ~6460km).");

        if (ImGui::ColorEdit3("Ground Albedo", &settings.groundAlbedo.x))
            isDirty = true;
    }

    void AtmosphereConfigWindow::drawRayleighSection()
    {
        ImGui::Spacing();
        ImGui::Text("Rayleigh Scattering (Air)");
        ImGui::Separator();

        // Display as 1e-6 scale for readability
        glm::vec3 rayleighDisp = settings.rayleighScattering * 1e6f;
        if (ImGui::DragFloat3("Scattering (x1e-6)", &rayleighDisp.x, 0.1f, 0.0f, 100.0f, "%.3f"))
        {
            settings.rayleighScattering = rayleighDisp * 1e-6f;
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rayleigh scattering coefficients (RGB). Higher blue = bluer sky.");

        float scaleHeight = -1.0f / settings.rayleighDensityExpScale;
        if (ImGui::DragFloat("Scale Height##rayleigh", &scaleHeight, 100.0f, 100.0f, 50000.0f, "%.0f m"))
        {
            settings.rayleighDensityExpScale = -1.0f / scaleHeight;
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Density scale height for Rayleigh scattering (Earth ~8000m).");
    }

    void AtmosphereConfigWindow::drawMieSection()
    {
        ImGui::Spacing();
        ImGui::Text("Mie Scattering (Aerosols/Haze)");
        ImGui::Separator();

        float mieScatDisp = settings.mieScattering * 1e6f;
        if (ImGui::DragFloat("Scattering (x1e-6)##mie", &mieScatDisp, 0.1f, 0.0f, 100.0f, "%.3f"))
        {
            settings.mieScattering = mieScatDisp * 1e-6f;
            isDirty = true;
        }

        float mieAbsDisp = settings.mieAbsorption * 1e6f;
        if (ImGui::DragFloat("Absorption (x1e-6)##mie", &mieAbsDisp, 0.1f, 0.0f, 100.0f, "%.3f"))
        {
            settings.mieAbsorption = mieAbsDisp * 1e-6f;
            isDirty = true;
        }

        if (ImGui::SliderFloat("Anisotropy (g)", &settings.mieAnisotropy, -1.0f, 1.0f, "%.2f"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Phase function anisotropy. 0.8 = strong forward scattering (sun halo).");

        float mieScaleHeight = -1.0f / settings.mieDensityExpScale;
        if (ImGui::DragFloat("Scale Height##mie", &mieScaleHeight, 50.0f, 50.0f, 20000.0f, "%.0f m"))
        {
            settings.mieDensityExpScale = -1.0f / mieScaleHeight;
            isDirty = true;
        }
    }

    void AtmosphereConfigWindow::drawOzoneSection()
    {
        ImGui::Spacing();
        ImGui::Text("Ozone Absorption");
        ImGui::Separator();

        glm::vec3 ozoneDisp = settings.ozoneAbsorption * 1e6f;
        if (ImGui::DragFloat3("Absorption (x1e-6)##ozone", &ozoneDisp.x, 0.01f, 0.0f, 10.0f, "%.3f"))
        {
            settings.ozoneAbsorption = ozoneDisp * 1e-6f;
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ozone absorption adds warmth to sunrise/sunset colors.");

        if (ImGui::DragFloat("Center Altitude", &settings.ozoneCenterAlt, 500.0f, 0.0f, 100000.0f, "%.0f m"))
            isDirty = true;

        if (ImGui::DragFloat("Width", &settings.ozoneWidth, 500.0f, 1000.0f, 50000.0f, "%.0f m"))
            isDirty = true;
    }

    void AtmosphereConfigWindow::drawSunSection()
    {
        ImGui::Spacing();
        ImGui::Text("Sun");
        ImGui::Separator();

        if (ImGui::DragFloat3("Irradiance", &settings.sunIrradiance.x, 0.01f, 0.0f, 10.0f, "%.4f"))
            isDirty = true;

        ImGui::TextDisabled("Sun direction is driven by the Directional Light in the scene.");
        ImGui::Spacing();

        if (ImGui::DragFloat("Azimuth (fallback)", &settings.sunAzimuth, 1.0f, 0.0f, 360.0f, "%.1f deg"))
            isDirty = true;

        if (ImGui::DragFloat("Elevation (fallback)", &settings.sunElevation, 0.5f, -10.0f, 90.0f, "%.1f deg"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Used when no directional light is present in the scene.");

        float angRadDeg = glm::degrees(settings.sunAngularRadius);
        if (ImGui::DragFloat("Angular Radius", &angRadDeg, 0.01f, 0.01f, 5.0f, "%.3f deg"))
        {
            settings.sunAngularRadius = glm::radians(angRadDeg);
            isDirty = true;
        }
    }

    void AtmosphereConfigWindow::drawAerialSection()
    {
        ImGui::Spacing();
        ImGui::Text("Aerial Perspective");
        ImGui::Separator();

        if (ImGui::DragFloat("Max Distance", &settings.aerialMaxDist, 1000.0f, 1000.0f, 1000000.0f, "%.0f m"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum distance for aerial perspective effect.");

        if (ImGui::SliderFloat("Intensity##aerial", &settings.aerialIntensity, 0.0f, 5.0f, "%.2f"))
            isDirty = true;
    }

    void AtmosphereConfigWindow::draw()
    {
        if (!visible) return;

        if (!settingsLoaded) loadSettings();

        ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Atmosphere Configuration", &visible))
        {
            if (ImGui::Checkbox("Enable Atmosphere", &settings.enabled))
                isDirty = true;

            if (settings.enabled)
            {
                drawPlanetSection();
                drawRayleighSection();
                drawMieSection();
                drawOzoneSection();
                drawSunSection();
                drawAerialSection();
            }

            ImGui::Spacing();
            ImGui::Separator();

            if (ImGui::Button("Apply"))
                applySettings();
            ImGui::SameLine();
            if (ImGui::Button("Reload"))
                loadSettings();
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults"))
                resetToDefaults();

            if (isDirty)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
            }
        }
        ImGui::End();
    }
}
