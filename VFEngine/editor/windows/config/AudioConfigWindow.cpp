#include "print/Log.hpp"
#include "AudioConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/AudioSettingsEvents.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>

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

    void AudioConfigWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Audio Configuration", &visible))
        {
            drawListenerSection();
            ImGui::Spacing();
            drawDistanceModelSection();

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
            ImGui::TextDisabled("Audio settings are saved with the scene file.");
        }
        ImGui::End();
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
