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

    void AtmosphereConfigWindow::drawDayNightSection()
    {
        ImGui::Spacing();
        ImGui::Text("Day-Night Cycle");
        ImGui::Separator();

        if (ImGui::Checkbox("Enable Day-Night Cycle", &settings.dayNightEnabled))
            isDirty = true;

        if (settings.dayNightEnabled)
        {
            if (ImGui::SliderFloat("Time of Day", &settings.timeOfDay, 0.0f, 24.0f, "%.1f h"))
                isDirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0=Midnight, 6=Dawn, 12=Noon, 18=Dusk");

            if (ImGui::DragFloat("Cycle Speed", &settings.cycleSpeed, 0.1f, 0.0f, 100.0f, "%.1f"))
                isDirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("1.0 = one game-hour per real-time minute. 0 = paused.");

            if (ImGui::SliderFloat("Moon Phase Offset", &settings.moonPhaseOffset, 0.0f, 1.0f, "%.2f"))
                isDirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Shifts the moon orbit relative to sun. 0 = full moon at midnight.");
        }

        // VK-1566: sun -> scene light feedback (works with a static sun or the cycle).
        ImGui::Spacing();
        ImGui::Text("Scene Lighting Feedback");
        ImGui::Separator();

        if (ImGui::Checkbox("Sun Color From Atmosphere", &settings.sunColorFromAtmosphere))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tint the directional light by the atmospheric sun transmittance\n(warm at sunrise/sunset, dims below the horizon).");

        if (settings.sunColorFromAtmosphere)
        {
            if (ImGui::SliderFloat("Feedback Strength", &settings.sunColorFeedbackStrength, 0.0f, 1.0f, "%.2f"))
                isDirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = white sun, 1 = full atmospheric tint.");
        }

        if (ImGui::Checkbox("Cycle Controls Sun Entity", &settings.cycleControlsSunEntity))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("While the day-night cycle runs, rotate the sun light entity so\nscene lighting and shadows track the moving sun.");

        // VK-1569: dynamic sky -> IBL ambient (time-sliced atmosphere capture).
        ImGui::Spacing();
        ImGui::Text("Dynamic Sky Ambient");
        ImGui::Separator();

        if (ImGui::Checkbox("Dynamic Sky Ambient", &settings.dynamicAmbient))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Capture the atmosphere sky into IBL cubemaps so diffuse/specular ambient\ntracks time-of-day and weather. Overrides the scene's static IBL while on.");

        if (settings.dynamicAmbient)
        {
            if (ImGui::SliderFloat("Ambient Intensity", &settings.ambientIntensity, 0.0f, 10.0f, "%.2f"))
                isDirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Multiplier on the captured ambient (weather routes its ambient light\nmultiplier here).");

            if (ImGui::SliderInt("Capture Items / Frame", &settings.ambientItemsPerFrame, 1, 32))
                isDirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Time-slicing budget. 42 items make one full refresh\n(6 => ~7 frames per update).");
        }
    }

    void AtmosphereConfigWindow::drawMoonSection()
    {
        ImGui::Spacing();
        ImGui::Text("Moon");
        ImGui::Separator();

        if (settings.dayNightEnabled)
        {
            ImGui::TextDisabled("Moon position is auto-computed by Day-Night Cycle.");
            ImGui::Text("Azimuth: %.1f deg  Elevation: %.1f deg", settings.moonAzimuth, settings.moonElevation);
        }
        else
        {
            if (ImGui::DragFloat("Moon Azimuth", &settings.moonAzimuth, 1.0f, 0.0f, 360.0f, "%.1f deg"))
                isDirty = true;

            if (ImGui::DragFloat("Moon Elevation", &settings.moonElevation, 0.5f, -90.0f, 90.0f, "%.1f deg"))
                isDirty = true;
        }

        float moonAngDeg = glm::degrees(settings.moonAngularRadius);
        if (ImGui::DragFloat("Moon Angular Radius", &moonAngDeg, 0.01f, 0.01f, 5.0f, "%.3f deg"))
        {
            settings.moonAngularRadius = glm::radians(moonAngDeg);
            isDirty = true;
        }

        if (ImGui::SliderFloat("Moon Brightness", &settings.moonBrightness, 0.0f, 1.0f, "%.3f"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Moon brightness relative to sun irradiance.");
    }

    void AtmosphereConfigWindow::drawStarsSection()
    {
        ImGui::Spacing();
        ImGui::Text("Stars");
        ImGui::Separator();

        if (ImGui::DragFloat("Star Density", &settings.starDensity, 0.0005f, 0.0f, 0.05f, "%.4f"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Probability of a star per grid cell. Higher = more stars.");

        if (ImGui::DragFloat("Star Brightness", &settings.starBrightness, 0.05f, 0.0f, 10.0f, "%.2f"))
            isDirty = true;

        if (ImGui::DragFloat("Twinkle Speed", &settings.starTwinkleSpeed, 0.1f, 0.0f, 5.0f, "%.1f"))
            isDirty = true;

        if (ImGui::DragFloat("Night Sky Brightness", &settings.nightSkyBrightness, 0.0005f, 0.0f, 0.05f, "%.4f"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ambient sky brightness floor at night.");
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

    void AtmosphereConfigWindow::drawContent()
    {
        if (!settingsLoaded) loadSettings();

        if (ImGui::Checkbox("Enable Atmosphere", &settings.enabled))
            isDirty = true;

        if (settings.enabled)
        {
            drawPlanetSection();
            drawRayleighSection();
            drawMieSection();
            drawOzoneSection();
            drawSunSection();
            drawDayNightSection();
            drawMoonSection();
            drawStarsSection();
            drawAerialSection();
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Apply##atmo"))
            applySettings();
        ImGui::SameLine();
        if (ImGui::Button("Reload##atmo"))
            loadSettings();
        ImGui::SameLine();
        if (ImGui::Button("Reset Defaults##atmo"))
            resetToDefaults();

        if (isDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }
    }

    void AtmosphereConfigWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Atmosphere Configuration", &visible))
        {
            drawContent();
        }
        ImGui::End();
    }
}
