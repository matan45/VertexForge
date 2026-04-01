#include "WeatherEditorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "weather/WeatherPresets.hpp"
#include <imgui.h>

namespace windows
{
    void WeatherEditorWindow::show()
    {
        visible = true;
        settingsLoaded = false;
    }

    void WeatherEditorWindow::loadState()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        try
        {
            currentState = dispatcher.query(events::weather::GetWeatherStateQuery{});
            snowAccumulation = dispatcher.query(events::weather::GetSnowAccumulationQuery{});
            transitionProgress = dispatcher.query(events::weather::GetWeatherTransitionProgressQuery{});
            weatherEnabled = dispatcher.query(events::weather::IsWeatherEnabledQuery{});
            scheduleEnabled = dispatcher.query(events::weather::IsWeatherScheduleEnabledQuery{});
        }
        catch (...) {}
        settingsLoaded = true;
    }

    void WeatherEditorWindow::draw()
    {
        if (!visible) return;

        if (!settingsLoaded) loadState();

        // Refresh state every frame for live updates
        loadState();

        ImGui::SetNextWindowSize(ImVec2(400, 550), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Weather System", &visible))
        {
            // Enable toggle
            {
                bool enabled = weatherEnabled;
                if (ImGui::Checkbox("Weather Enabled", &enabled))
                {
                    events::weather::SetWeatherEnabledCommand cmd;
                    cmd.enabled = enabled;
                    events::EventDispatcher::instance().execute(cmd);
                }
            }

            if (weatherEnabled)
            {
                ImGui::Spacing();
                drawPresetButtons();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                drawCurrentState();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                drawManualControls();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                drawScheduleControls();
            }
        }
        ImGui::End();
    }

    void WeatherEditorWindow::drawPresetButtons()
    {
        ImGui::Text("Weather Presets");
        ImGui::Spacing();

        ImGui::SliderFloat("Transition Duration", &transitionDuration, 0.0f, 120.0f, "%.1f s");
        ImGui::Spacing();

        struct PresetButton
        {
            const char* label;
            weather::WeatherPresetId id;
        };

        static const PresetButton presets[] = {
            {"Clear",        weather::WeatherPresetId::Clear},
            {"Cloudy",       weather::WeatherPresetId::Cloudy},
            {"Overcast",     weather::WeatherPresetId::Overcast},
            {"Light Rain",   weather::WeatherPresetId::LightRain},
            {"Heavy Rain",   weather::WeatherPresetId::HeavyRain},
            {"Thunderstorm", weather::WeatherPresetId::Thunderstorm},
            {"Light Snow",   weather::WeatherPresetId::LightSnow},
            {"Heavy Snow",   weather::WeatherPresetId::HeavySnow},
            {"Fog",          weather::WeatherPresetId::Fog},
            {"Sandstorm",    weather::WeatherPresetId::Sandstorm},
        };

        float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        for (int i = 0; i < 10; i++)
        {
            if (i % 2 != 0)
                ImGui::SameLine();

            if (ImGui::Button(presets[i].label, ImVec2(buttonWidth, 0)))
            {
                events::weather::SetWeatherPresetCommand cmd;
                cmd.preset = presets[i].id;
                cmd.transitionDuration = transitionDuration;
                cmd.easing = weather::WeatherEasing::EaseInOut;
                events::EventDispatcher::instance().execute(cmd);
            }
        }
    }

    void WeatherEditorWindow::drawCurrentState()
    {
        if (ImGui::CollapsingHeader("Current State", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            // Transition progress
            if (transitionProgress < 1.0f)
            {
                ImGui::ProgressBar(transitionProgress, ImVec2(-1, 0), "Transitioning...");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Stable");
            }

            ImGui::Spacing();

            // Precipitation
            const char* precipNames[] = {"None", "Rain", "Snow"};
            int precipIdx = static_cast<int>(currentState.precipType);
            ImGui::Text("Precipitation: %s (%.0f%%)",
                precipIdx < 3 ? precipNames[precipIdx] : "?",
                currentState.precipIntensity * 100.0f);

            // Cloud
            ImGui::Text("Cloud Coverage: %.0f%%", currentState.cloudCoverage * 100.0f);
            ImGui::Text("Cloud Density: %.0f%%", currentState.cloudDensity * 100.0f);

            // Wind
            ImGui::Text("Wind: %.1f m/s at %.0f deg", currentState.windSpeed, currentState.windDirectionDeg);
            ImGui::Text("Gusts: %.0f%% strength, %.1f/s freq",
                currentState.gustStrength * 100.0f, currentState.gustFrequency);

            // Fog
            ImGui::Text("Fog Density: %.3f", currentState.fogDensity);

            // Atmosphere
            ImGui::Text("Ambient Light: %.0f%%", currentState.ambientLightMult * 100.0f);
            ImGui::Text("Tint: (%.2f, %.2f, %.2f)",
                currentState.atmosphereTint.x, currentState.atmosphereTint.y, currentState.atmosphereTint.z);

            // Snow accumulation
            ImGui::Spacing();
            ImGui::Text("Snow Accumulation:");
            ImGui::ProgressBar(snowAccumulation, ImVec2(-1, 0));

            ImGui::Unindent();
        }
    }

    void WeatherEditorWindow::drawManualControls()
    {
        if (ImGui::CollapsingHeader("Manual Override"))
        {
            ImGui::Indent();

            static weather::WeatherState manualState;
            static bool initialized = false;
            if (!initialized)
            {
                manualState = currentState;
                initialized = true;
            }

            if (ImGui::Button("Copy Current"))
                manualState = currentState;

            ImGui::Spacing();
            ImGui::SliderFloat("Cloud Coverage##manual", &manualState.cloudCoverage, 0.0f, 1.0f);
            ImGui::SliderFloat("Cloud Density##manual", &manualState.cloudDensity, 0.0f, 1.0f);
            ImGui::SliderFloat("Precip Intensity##manual", &manualState.precipIntensity, 0.0f, 1.0f);

            static int precipTypeIdx = 0;
            const char* precipItems[] = {"None", "Rain", "Snow"};
            if (ImGui::Combo("Precip Type##manual", &precipTypeIdx, precipItems, 3))
                manualState.precipType = static_cast<weather::PrecipitationType>(precipTypeIdx);

            ImGui::SliderFloat("Wind Speed##manual", &manualState.windSpeed, 0.0f, 50.0f, "%.1f m/s");
            ImGui::SliderFloat("Wind Dir##manual", &manualState.windDirectionDeg, 0.0f, 360.0f, "%.0f deg");
            ImGui::SliderFloat("Fog Density##manual", &manualState.fogDensity, 0.0f, 0.2f, "%.4f");
            ImGui::SliderFloat("Ambient Light##manual", &manualState.ambientLightMult, 0.0f, 1.5f);
            ImGui::ColorEdit3("Atmos Tint##manual", &manualState.atmosphereTint.x);

            ImGui::Spacing();

            if (ImGui::Button("Apply Immediately"))
            {
                events::weather::SetWeatherImmediateCommand cmd;
                cmd.state = manualState;
                events::EventDispatcher::instance().execute(cmd);
            }
            ImGui::SameLine();
            if (ImGui::Button("Transition To"))
            {
                events::weather::SetWeatherStateCommand cmd;
                cmd.state = manualState;
                cmd.transitionDuration = transitionDuration;
                cmd.easing = weather::WeatherEasing::EaseInOut;
                events::EventDispatcher::instance().execute(cmd);
            }

            ImGui::Unindent();
        }
    }

    void WeatherEditorWindow::drawScheduleControls()
    {
        if (ImGui::CollapsingHeader("Schedule"))
        {
            ImGui::Indent();

            bool sched = scheduleEnabled;
            if (ImGui::Checkbox("Auto Schedule", &sched))
            {
                events::weather::SetWeatherScheduleEnabledCommand cmd;
                cmd.enabled = sched;
                events::EventDispatcher::instance().execute(cmd);
            }

            ImGui::Unindent();
        }
    }
}
