#include "WeatherEditorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "weather/WeatherPresets.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>

namespace windows
{
    void WeatherEditorWindow::show()
    {
        visible = true;
        settingsLoaded = false;
        audioConfigLoaded = false;
    }

    void WeatherEditorWindow::notifySceneLoaded()
    {
        settingsLoaded = false;
        audioConfigLoaded = false;
        manualState = weather::WeatherState{};
    }

    void WeatherEditorWindow::loadState()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        try
        {
            currentState = dispatcher.query(events::weather::GetWeatherStateQuery{});
            snowAccumulation = dispatcher.query(events::weather::GetSnowAccumulationQuery{});
            wetness = dispatcher.query(events::weather::GetWetnessQuery{});
            transitionProgress = dispatcher.query(events::weather::GetWeatherTransitionProgressQuery{});
            weatherEnabled = dispatcher.query(events::weather::IsWeatherEnabledQuery{});
            scheduleEnabled = dispatcher.query(events::weather::IsWeatherScheduleEnabledQuery{});
        }
        catch (...) {}
        settingsLoaded = true;
    }

    void WeatherEditorWindow::drawContent()
    {
        loadState();

        bool enabled = weatherEnabled;
        if (ImGui::Checkbox("Weather Enabled", &enabled))
        {
            events::weather::SetWeatherEnabledCommand cmd;
            cmd.enabled = enabled;
            events::EventDispatcher::instance().execute(cmd);
            weatherEnabled = enabled;
        }

        if (!weatherEnabled) return;

        ImGui::Spacing();
        drawPresetButtons();
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        drawStateInfo();
        drawAccumulationBars();
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        drawManualSliders();
        drawManualButtons();
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        drawScheduleControls();
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        drawAudioBrowseFields();
    }

    void WeatherEditorWindow::draw()
    {
        if (!visible) return;
        ImGui::SetNextWindowSize(ImVec2(400, 550), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Weather System", &visible))
            drawContent();
        ImGui::End();
    }

    void WeatherEditorWindow::drawPresetButtons()
    {
        ImGui::Text("Weather Presets");
        ImGui::Spacing();
        ImGui::SliderFloat("Transition Duration", &transitionDuration, 0.0f, 120.0f, "%.1f s");
        ImGui::Spacing();

        struct Entry { const char* label; weather::WeatherPresetId id; };
        static const Entry presets[] = {
            {"Clear", weather::WeatherPresetId::Clear},
            {"Cloudy", weather::WeatherPresetId::Cloudy},
            {"Overcast", weather::WeatherPresetId::Overcast},
            {"Light Rain", weather::WeatherPresetId::LightRain},
            {"Heavy Rain", weather::WeatherPresetId::HeavyRain},
            {"Thunderstorm", weather::WeatherPresetId::Thunderstorm},
            {"Light Snow", weather::WeatherPresetId::LightSnow},
            {"Heavy Snow", weather::WeatherPresetId::HeavySnow},
            {"Fog", weather::WeatherPresetId::Fog},
            {"Sandstorm", weather::WeatherPresetId::Sandstorm},
        };

        float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        for (int i = 0; i < 10; i++)
        {
            if (i % 2 != 0) ImGui::SameLine();
            if (ImGui::Button(presets[i].label, ImVec2(btnW, 0)))
            {
                events::weather::SetWeatherPresetCommand cmd;
                cmd.preset = presets[i].id;
                cmd.transitionDuration = transitionDuration;
                cmd.easing = weather::WeatherEasing::EaseInOut;
                events::EventDispatcher::instance().execute(cmd);
            }
        }
    }

    void WeatherEditorWindow::drawStateInfo()
    {
        if (!ImGui::CollapsingHeader("Current State", ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::Indent();

        if (transitionProgress < 1.0f)
            ImGui::ProgressBar(transitionProgress, ImVec2(-1, 0), "Transitioning...");
        else
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Stable");

        ImGui::Spacing();
        const char* precipNames[] = {"None", "Rain", "Snow"};
        int idx = static_cast<int>(currentState.precipType);
        ImGui::Text("Precipitation: %s (%.0f%%)", idx < 3 ? precipNames[idx] : "?", currentState.precipIntensity * 100.0f);
        ImGui::Text("Cloud Coverage: %.0f%%", currentState.cloudCoverage * 100.0f);
        ImGui::Text("Cloud Density: %.0f%%", currentState.cloudDensity * 100.0f);
        ImGui::Text("Wind: %.1f m/s at %.0f deg", currentState.windSpeed, currentState.windDirectionDeg);
        ImGui::Text("Gusts: %.0f%% strength, %.1f/s freq", currentState.gustStrength * 100.0f, currentState.gustFrequency);
        ImGui::Text("Fog Density: %.3f", currentState.fogDensity);
        ImGui::Text("Ambient Light: %.0f%%", currentState.ambientLightMult * 100.0f);

        ImGui::Unindent();
    }

    void WeatherEditorWindow::drawAccumulationBars()
    {
        ImGui::Spacing();
        ImGui::Text("Snow Accumulation:");
        ImGui::ProgressBar(snowAccumulation, ImVec2(-1, 0));
        ImGui::Text("Wetness:");
        ImGui::ProgressBar(wetness, ImVec2(-1, 0));
    }

    void WeatherEditorWindow::drawManualSliders()
    {
        if (!ImGui::CollapsingHeader("Manual Override")) return;
        ImGui::Indent();

        if (ImGui::Button("Copy Current")) manualState = currentState;
        ImGui::Spacing();

        ImGui::SliderFloat("Cloud Coverage##m", &manualState.cloudCoverage, 0.0f, 1.0f);
        ImGui::SliderFloat("Cloud Density##m", &manualState.cloudDensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Precip Intensity##m", &manualState.precipIntensity, 0.0f, 1.0f);

        int precipIdx = static_cast<int>(manualState.precipType);
        const char* items[] = {"None", "Rain", "Snow"};
        if (ImGui::Combo("Precip Type##m", &precipIdx, items, 3))
            manualState.precipType = static_cast<weather::PrecipitationType>(precipIdx);

        ImGui::SliderFloat("Wind Speed##m", &manualState.windSpeed, 0.0f, 50.0f, "%.1f m/s");
        ImGui::SliderFloat("Wind Dir##m", &manualState.windDirectionDeg, 0.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("Fog Density##m", &manualState.fogDensity, 0.0f, 0.2f, "%.4f");
        ImGui::SliderFloat("Ambient Light##m", &manualState.ambientLightMult, 0.0f, 1.5f);
        ImGui::ColorEdit3("Atmos Tint##m", &manualState.atmosphereTint.x);

        ImGui::Unindent();
    }

    void WeatherEditorWindow::drawManualButtons()
    {
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
    }

    void WeatherEditorWindow::drawScheduleControls()
    {
        if (!ImGui::CollapsingHeader("Schedule")) return;
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

    void WeatherEditorWindow::drawAudioBrowseFields()
    {
        if (!ImGui::CollapsingHeader("Audio Assets")) return;
        ImGui::Indent();

        auto& dispatcher = events::EventDispatcher::instance();
        if (!audioConfigLoaded)
        {
            try { audioConfig = dispatcher.query(events::weather::GetWeatherAudioConfigQuery{}); }
            catch (...) {}
            audioConfigLoaded = true;
        }

        auto browse = [&](const char* label, std::string& path)
        {
            ImGui::Text("%s", label);
            ImGui::SameLine(120);
            if (!path.empty())
            {
                std::string name = path.substr(path.find_last_of("/\\") + 1);
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", name.c_str());
            }
            else
                ImGui::TextDisabled("(none)");

            ImGui::SameLine();
            if (ImGui::SmallButton((std::string("...##") + label).c_str()))
            {
                nfd::FileDialog fd;
                std::string sel = fd.openFileDialog({{L"VF Audio (*.vfAudio)", L"*.vfAudio"}});
                if (!sel.empty())
                {
                    path = sel;
                    events::weather::SetWeatherAudioConfigCommand cmd;
                    cmd.config = audioConfig;
                    dispatcher.execute(cmd);
                }
            }
            if (!path.empty())
            {
                ImGui::SameLine();
                if (ImGui::SmallButton((std::string("X##") + label).c_str()))
                {
                    path.clear();
                    events::weather::SetWeatherAudioConfigCommand cmd;
                    cmd.config = audioConfig;
                    dispatcher.execute(cmd);
                }
            }
        };

        ImGui::Text("Ambient Loops");
        ImGui::Separator();
        browse("Wind Loop", audioConfig.windLoopPath);
        browse("Rain Loop", audioConfig.rainLoopPath);
        browse("Snow Loop", audioConfig.snowLoopPath);
        ImGui::Spacing();
        ImGui::Text("Thunder SFX");
        ImGui::Separator();
        browse("Thunder 1", audioConfig.thunderPaths[0]);
        browse("Thunder 2", audioConfig.thunderPaths[1]);
        browse("Thunder 3", audioConfig.thunderPaths[2]);
        ImGui::Spacing();
        if (ImGui::Button("Reload Config"))
            audioConfigLoaded = false;

        ImGui::Unindent();
    }
}
