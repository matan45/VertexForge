#include "print/Log.hpp"
#include "AudioConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/audio/AudioSettingsEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <utility>
#include <vector>

namespace windows
{
    void AudioConfigWindow::show()
    {
        visible = true;
        if (!settingsLoaded)
        {
            loadFromScene();
        }
    }

    void AudioConfigWindow::notifySceneLoaded()
    {
        settingsLoaded = false;
    }

    void AudioConfigWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Audio Configuration", &visible))
        {
            drawContent();
        }
        ImGui::End();
    }

    void AudioConfigWindow::drawContent()
    {
        if (!settingsLoaded)
        {
            loadFromScene();
        }

        drawListenerSection();
        ImGui::Spacing();
        drawVoiceManagementSection();
        ImGui::Spacing();
        drawDistanceModelSection();
        ImGui::Spacing();
        drawDistanceFilterSection();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

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
        ImGui::TextDisabled("Save to Scene writes these settings to the active scene's linked settings asset.");
    }

    void AudioConfigWindow::drawListenerSection()
    {
        if (ImGui::CollapsingHeader("Listener Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Master Volume");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##MasterVolume", &settings.masterVolume, 0.01f, 0.0f, 2.0f, "%.2f"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("0.0 = muted, 1.0 = normal, >1.0 = amplified");

            ImGui::Spacing();

            ImGui::Text("Doppler Factor");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##DopplerFactor", &settings.dopplerFactor, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("0.0 = disabled, 1.0 = normal, >1.0 = exaggerated");

            ImGui::Spacing();

            ImGui::Text("Speed of Sound");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##SpeedOfSound", &settings.speedOfSound, 1.0f, 1.0f, 1000.0f, "%.1f m/s"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Default: 343.3 m/s (speed of sound in air)");

            ImGui::Spacing();

            ImGui::Text("Max Doppler Speed");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##MaxDopplerSpeed", &settings.maxDopplerSpeed, 1.0f, 1.0f, 10000.0f, "%.1f m/s"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Teleport guard: single-frame moves faster than this get no pitch shift");

            ImGui::Spacing();

            // VK-1508: HRTF (binaural) toggle + live device status line. The checkbox rides
            // the existing Apply/Save flow (ApplyAudioSettingsCommand carries the whole struct);
            // the status line reflects the ACTUAL device state, which can differ from the request.
            if (ImGui::Checkbox("Enable HRTF (Binaural)##Config", &settings.enableHrtf))
            {
                isDirty = true;
            }
            ImGui::TextDisabled("Head-tracked binaural rendering for headphones (OpenAL Soft)");

            types::AudioHrtfStatus hrtfStatus = types::AudioHrtfStatus::Unsupported;
            try
            {
                hrtfStatus = events::EventDispatcher::instance().query(events::audio::GetHrtfStatusQuery{});
            }
            catch (...)
            {
                hrtfStatus = types::AudioHrtfStatus::Unsupported;
            }

            ImGui::Text("Device status:");
            ImGui::SameLine();
            switch (hrtfStatus)
            {
            case types::AudioHrtfStatus::Enabled:
            case types::AudioHrtfStatus::Required:
            case types::AudioHrtfStatus::HeadphonesDetected:
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", types::audioHrtfStatusToString(hrtfStatus));
                break;
            case types::AudioHrtfStatus::Denied:
            case types::AudioHrtfStatus::UnsupportedFormat:
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", types::audioHrtfStatusToString(hrtfStatus));
                break;
            case types::AudioHrtfStatus::Unsupported:
                ImGui::TextDisabled("%s", types::audioHrtfStatusToString(hrtfStatus));
                break;
            default: // Disabled
                ImGui::TextUnformatted(types::audioHrtfStatusToString(hrtfStatus));
                break;
            }

            ImGui::Unindent();
        }
    }

    // VK-1513: the scene's real-voice budget, plus a live occupancy readout. The readout is
    // the instrument this feature is verified with — without it, "the count clamps" is only
    // observable from a debugger.
    void AudioConfigWindow::drawVoiceManagementSection()
    {
        if (ImGui::CollapsingHeader("Voice Management", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Max Real Voices");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragInt("##MaxRealVoices", &settings.maxRealVoices, 1.0f, 1, 256))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Sounds beyond the budget steal the least important voice, or are dropped");
            ImGui::TextDisabled("Takes effect for newly started sounds; live voices are never cut off");

            ImGui::Spacing();

            types::AudioVoiceStats voiceStats;
            try
            {
                voiceStats = events::EventDispatcher::instance().query(events::audio::GetVoiceCountQuery{});
            }
            catch (...)
            {
                voiceStats = types::AudioVoiceStats{};
            }

            ImGui::Text("Real voices:");
            ImGui::SameLine();
            if (voiceStats.maxRealVoices <= 0)
            {
                ImGui::TextDisabled("%d / unlimited (cap disabled)", voiceStats.realVoices);
            }
            else
            {
                // Amber as the budget fills, red once it binds and sounds start being
                // stolen or dropped — that transition is the thing worth noticing.
                const float load = static_cast<float>(voiceStats.realVoices) /
                                   static_cast<float>(voiceStats.maxRealVoices);
                ImVec4 colour(0.3f, 1.0f, 0.3f, 1.0f);
                if (load >= 1.0f)
                    colour = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                else if (load >= 0.8f)
                    colour = ImVec4(1.0f, 0.63f, 0.0f, 1.0f);
                ImGui::TextColored(colour, "%d / %d", voiceStats.realVoices, voiceStats.maxRealVoices);
            }
            ImGui::TextDisabled("Streaming sounds are exempt: they never draw from the source pool");

            ImGui::Unindent();
        }
    }

    void AudioConfigWindow::drawDistanceModelSection()
    {
        if (ImGui::CollapsingHeader("Distance Model", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Attenuation Model");
            ImGui::PushItemWidth(-1);

            const char* modelNames[] = {
                "None (No Attenuation)",
                "Inverse Distance",
                "Inverse Distance Clamped",
                "Linear Distance",
                "Linear Distance Clamped",
                "Exponent Distance",
                "Exponent Distance Clamped"
            };

            int currentModel = static_cast<int>(settings.distanceModel);
            if (ImGui::Combo("##DistanceModel", &currentModel, modelNames, IM_ARRAYSIZE(modelNames)))
            {
                settings.distanceModel = static_cast<types::AudioDistanceModel>(currentModel);
                isDirty = true;
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Clamped variants prevent sounds from getting louder when very close");

            ImGui::Spacing();

            ImGui::Text("Default Rolloff Factor");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##RolloffFactor", &settings.defaultRolloffFactor, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("How quickly sound attenuates with distance");

            ImGui::Unindent();
        }
    }

    void AudioConfigWindow::drawDistanceFilterSection()
    {
        if (ImGui::CollapsingHeader("Distance Filter Defaults", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            if (ImGui::Checkbox("Enable Distance Filtering##Config", &settings.enableDistanceFilter))
            {
                isDirty = true;
            }
            ImGui::TextDisabled("Globally enable/disable distance-based low-pass filtering for 3D sources");

            if (settings.enableDistanceFilter)
            {
                ImGui::Spacing();

                ImGui::Text("Default Filter Start Distance");
                ImGui::PushItemWidth(-1);
                if (ImGui::DragFloat("##FilterStartDist", &settings.defaultFilterStartDistance, 0.1f, 0.1f, 100.0f, "%.1f"))
                {
                    isDirty = true;
                }
                ImGui::PopItemWidth();
                ImGui::TextDisabled("Distance at which filtering begins");

                ImGui::Spacing();

                ImGui::Text("Default Filter Max Distance");
                ImGui::PushItemWidth(-1);
                if (ImGui::DragFloat("##FilterMaxDist", &settings.defaultFilterMaxDistance, 1.0f, 1.0f, 500.0f, "%.1f"))
                {
                    isDirty = true;
                }
                ImGui::PopItemWidth();
                ImGui::TextDisabled("Distance at which filter reaches full strength");

                ImGui::Spacing();

                ImGui::Text("Default Filter Intensity");
                ImGui::PushItemWidth(-1);
                if (ImGui::DragFloat("##FilterIntensity", &settings.defaultFilterIntensity, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    isDirty = true;
                }
                ImGui::PopItemWidth();
                ImGui::TextDisabled("Strength of the low-pass filter effect (0 = none, 1 = full)");
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Per-source settings override these defaults");

            ImGui::Unindent();
        }
    }

    void AudioConfigWindow::loadFromScene()
    {
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::GetAudioSettingsQuery query;
            settings = dispatcher.query(query);
        }
        catch (...)
        {
            settings = types::AudioSettings::createDefault();
        }

        settingsLoaded = true;
        isDirty = false;
    }

    void AudioConfigWindow::saveToScene()
    {
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::SetAudioSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);

            std::string path = dispatcher.query(events::scene::GetCurrentScenePathQuery{});
            if (path.empty())
            {
                const std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"VF Scene Files (*.vfScene)", L"*.vfScene"}
                };
                const nfd::FileDialog fileDialog;
                path = fileDialog.saveFileDialog(fileTypes, L"vfScene");
                if (path.empty())
                {
                    return;
                }
            }

            events::scene::SaveSceneCommand saveCmd;
            saveCmd.filePath = path;
            dispatcher.execute(saveCmd);

            isDirty = false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save audio settings: {}", e.what());
            return;
        }

        applySettings();
    }

    void AudioConfigWindow::applySettings()
    {
        try
        {
            events::audio::ApplyAudioSettingsCommand cmd;
            cmd.settings = settings;
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.execute(cmd);
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to apply audio settings: {}", e.what());
        }
    }

    void AudioConfigWindow::resetToDefaults()
    {
        settings = types::AudioSettings::createDefault();
        isDirty = true;
    }
}
