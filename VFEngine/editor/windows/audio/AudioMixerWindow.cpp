#include "AudioMixerWindow.hpp"
#include "AudioWidgets.hpp"
#include "events/EventDispatcher.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include "events/audio/AudioEffectEvents.hpp"
#include "types/AudioEffectTypes.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace
{
    // VK-1515: the dB floor, the colour thresholds and the drawing moved to
    // windows/audio/AudioWidgets.hpp when the active-sounds overlay needed the same meter.
    // They were file-local here, so a second window could not link to them at all. Only the
    // strip geometry stays — it is specific to a mixer channel.
    constexpr float kBusMeterWidth = 12.0f;
    constexpr float kBusMeterHeight = 150.0f;
}

namespace windows
{
    void AudioMixerWindow::show()
    {
        visible = true;
    }

    void AudioMixerWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Audio Mixer", &visible))
        {
            drawContent();
        }
        ImGui::End();
    }

    void AudioMixerWindow::drawContent()
    {
        drawBusChannels();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        drawEffectChainSection();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        drawCreateBusSection();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        drawSnapshotSection();
    }

    void AudioMixerWindow::drawBusChannels()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const auto busLevels = dispatcher.query(events::audio::GetBusLevelsQuery{});
        if (busLevels.empty()) { ImGui::TextDisabled("No audio buses configured"); return; }

        ImGui::TextDisabled("Levels: estimated, pre-effects");

        for (size_t i = 0; i < busLevels.size(); ++i) {
            const auto& level = busLevels[i];
            const auto& name = level.name;
            ImGui::BeginGroup();
            ImGui::PushID(static_cast<int>(i));

            bool isSelected = (name == selectedBusName);
            if (isSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
            if (ImGui::Selectable(name.c_str(), isSelected, 0, ImVec2(80.0f, 0)))
                selectedBusName = (selectedBusName == name) ? "" : name;
            if (isSelected) ImGui::PopStyleColor();

            events::audio::GetBusVolumeQuery volQuery; volQuery.busName = name;
            float volume = dispatcher.query(volQuery);
            ImGui::PushItemWidth(30.0f);
            if (ImGui::VSliderFloat("##vol", ImVec2(30, 150), &volume, 0.0f, 1.0f, "")) {
                events::audio::SetBusVolumeCommand cmd; cmd.busName = name; cmd.volume = volume;
                dispatcher.execute(cmd);
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            const ImVec2 meterPos = ImGui::GetCursorScreenPos();
            audiowidgets::drawVerticalMeter(meterPos, ImVec2(kBusMeterWidth, kBusMeterHeight),
                                            level.rms, level.peakHold);
            ImGui::Dummy(ImVec2(kBusMeterWidth, kBusMeterHeight));
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Estimated level (pre-effects)\n"
                                  "RMS envelope with bus gain and distance attenuation\n"
                                  "Floor: -60 dB");
            }
            ImGui::Text("%.0f%%", volume * 100.0f);

            events::audio::IsBusMutedQuery muteQuery; muteQuery.busName = name;
            bool muted = dispatcher.query(muteQuery);
            if (muted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("M", ImVec2(25, 20))) {
                events::audio::SetBusMutedCommand cmd; cmd.busName = name; cmd.muted = !muted;
                dispatcher.execute(cmd);
            }
            if (muted) ImGui::PopStyleColor();

            ImGui::SameLine();
            events::audio::IsBusSoloedQuery soloQuery; soloQuery.busName = name;
            bool soloed = dispatcher.query(soloQuery);
            if (soloed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.15f, 1.0f));
            if (ImGui::Button("S", ImVec2(25, 20))) {
                events::audio::SetBusSoloedCommand cmd; cmd.busName = name; cmd.soloed = !soloed;
                dispatcher.execute(cmd);
            }
            if (soloed) ImGui::PopStyleColor();

            ImGui::PopID();
            ImGui::EndGroup();
            if (i < busLevels.size() - 1) { ImGui::SameLine(); ImGui::Dummy(ImVec2(10, 0)); ImGui::SameLine(); }
        }
    }

    void AudioMixerWindow::drawEffectChainSection()
    {
        if (selectedBusName.empty()) { ImGui::TextDisabled("Select a bus to edit its effect chain"); return; }

        auto& dispatcher = events::EventDispatcher::instance();
        ImGui::Text("Effects: %s", selectedBusName.c_str());

        events::audio::GetBusEffectChainQuery chainQuery; chainQuery.busName = selectedBusName;
        auto effectChain = dispatcher.query(chainQuery);
        int maxEffects = dispatcher.query(events::audio::GetMaxEffectsPerBusQuery{});

        for (size_t i = 0; i < effectChain.size(); ++i) {
            auto& effect = effectChain[i];
            ImGui::PushID(static_cast<int>(effect.id));

            ImGui::Text("%zu. %s", i + 1, types::audioEffectTypeToString(effect.type).c_str());
            ImGui::SameLine(200);

            bool enabled = effect.enabled;
            if (ImGui::Checkbox("##en", &enabled)) {
                events::audio::SetBusEffectEnabledCommand cmd;
                cmd.busName = selectedBusName; cmd.effectId = effect.id; cmd.enabled = enabled;
                dispatcher.execute(cmd);
            }
            ImGui::SameLine();

            float wetDry = effect.wetDryMix;
            ImGui::PushItemWidth(100);
            if (ImGui::SliderFloat("##wd", &wetDry, 0.0f, 1.0f, "Wet %.2f")) {
                events::audio::SetBusEffectWetDryCommand cmd;
                cmd.busName = selectedBusName; cmd.effectId = effect.id; cmd.wetDryMix = wetDry;
                dispatcher.execute(cmd);
            }
            ImGui::PopItemWidth(); ImGui::SameLine();

            if (ImGui::Button("X##rm")) {
                events::audio::RemoveBusEffectCommand cmd;
                cmd.busName = selectedBusName; cmd.effectId = effect.id;
                dispatcher.execute(cmd);
                ImGui::PopID(); break;
            }

            if (ImGui::TreeNode("Parameters")) {
                switch (effect.type) {
                case types::AudioEffectType::Reverb:    drawReverbEditor(selectedBusName, effect.id); break;
                case types::AudioEffectType::EQ:        drawEQEditor(selectedBusName, effect.id); break;
                case types::AudioEffectType::Compressor: drawCompressorEditor(selectedBusName, effect.id); break;
                case types::AudioEffectType::Echo:      drawEchoEditor(selectedBusName, effect.id); break;
                case types::AudioEffectType::Chorus:    drawChorusEditor(selectedBusName, effect.id); break;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        bool atMax = static_cast<int>(effectChain.size()) >= maxEffects;
        ImGui::BeginDisabled(atMax);
        ImGui::PushItemWidth(120);
        ImGui::Combo("##addtype", &addEffectTypeIndex, "Reverb\0EQ\0Compressor\0Echo\0Chorus\0", 5);
        ImGui::PopItemWidth(); ImGui::SameLine();
        if (ImGui::Button("Add Effect")) {
            events::audio::AddBusEffectCommand cmd; cmd.busName = selectedBusName;
            cmd.config = types::BusEffectConfig::createDefault(static_cast<types::AudioEffectType>(addEffectTypeIndex));
            dispatcher.execute(cmd);
        }
        ImGui::EndDisabled();
        if (atMax && maxEffects > 0) { ImGui::SameLine(); ImGui::TextDisabled("(max %d effects)", maxEffects); }
        else if (maxEffects == 0) { ImGui::TextDisabled("EFX not available"); }
    }

    void AudioMixerWindow::drawReverbEditor(const std::string& busName, uint32_t effectId)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        static std::vector<std::string> presetNames = {
            "Generic", "Room", "Bathroom", "Living Room", "Stone Room",
            "Auditorium", "Concert Hall", "Cave", "Arena", "Hangar",
            "Hallway", "Forest", "City", "Mountains", "Underwater",
            "Chapel", "Castle Hall", "Factory Hall"};

        static int selectedPreset = 0;
        ImGui::PushItemWidth(200);
        if (ImGui::BeginCombo("Preset##rv", presetNames[selectedPreset].c_str())) {
            for (int i = 0; i < static_cast<int>(presetNames.size()); ++i) {
                bool isSelected = (selectedPreset == i);
                if (ImGui::Selectable(presetNames[i].c_str(), isSelected)) {
                    selectedPreset = i;
                    types::BusEffectConfig config; config.id = effectId; config.type = types::AudioEffectType::Reverb;
                    types::ReverbParams params; params.presetName = presetNames[i]; config.params = params;
                    events::audio::UpdateBusEffectCommand cmd;
                    cmd.busName = busName; cmd.effectId = effectId; cmd.config = config;
                    dispatcher.execute(cmd);
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Use presets to configure reverb parameters");
    }

    void AudioMixerWindow::drawEQEditor(const std::string& busName, uint32_t effectId)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // EQ band controls
        static types::EQParams eqParams;

        bool changed = false;
        ImGui::PushItemWidth(150);

        changed |= ImGui::SliderFloat("Low Gain", &eqParams.lowGain, 0.126f, 7.943f, "%.2f");
        changed |= ImGui::SliderFloat("Low Cutoff", &eqParams.lowCutoff, 50.0f, 800.0f, "%.0f Hz");
        changed |= ImGui::SliderFloat("Mid1 Gain", &eqParams.mid1Gain, 0.126f, 7.943f, "%.2f");
        changed |= ImGui::SliderFloat("Mid1 Center", &eqParams.mid1Center, 200.0f, 3000.0f, "%.0f Hz");
        changed |= ImGui::SliderFloat("Mid2 Gain", &eqParams.mid2Gain, 0.126f, 7.943f, "%.2f");
        changed |= ImGui::SliderFloat("Mid2 Center", &eqParams.mid2Center, 1000.0f, 8000.0f, "%.0f Hz");
        changed |= ImGui::SliderFloat("High Gain", &eqParams.highGain, 0.126f, 7.943f, "%.2f");
        changed |= ImGui::SliderFloat("High Cutoff", &eqParams.highCutoff, 4000.0f, 16000.0f, "%.0f Hz");

        ImGui::PopItemWidth();

        if (changed)
        {
            types::BusEffectConfig config;
            config.id = effectId;
            config.type = types::AudioEffectType::EQ;
            config.params = eqParams;

            events::audio::UpdateBusEffectCommand cmd;
            cmd.busName = busName;
            cmd.effectId = effectId;
            cmd.config = config;
            dispatcher.execute(cmd);
        }
    }

    void AudioMixerWindow::drawCompressorEditor(const std::string& busName, uint32_t effectId)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        static bool compOn = true;
        if (ImGui::Checkbox("Compressor On", &compOn))
        {
            types::BusEffectConfig config;
            config.id = effectId;
            config.type = types::AudioEffectType::Compressor;
            config.params = types::CompressorParams{compOn};

            events::audio::UpdateBusEffectCommand cmd;
            cmd.busName = busName;
            cmd.effectId = effectId;
            cmd.config = config;
            dispatcher.execute(cmd);
        }
    }

    void AudioMixerWindow::drawEchoEditor(const std::string& busName, uint32_t effectId)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        static types::EchoParams echoParams;
        bool changed = false;

        ImGui::PushItemWidth(150);
        changed |= ImGui::SliderFloat("Delay", &echoParams.delay, 0.0f, 0.207f, "%.3f s");
        changed |= ImGui::SliderFloat("LR Delay", &echoParams.lrDelay, 0.0f, 0.404f, "%.3f s");
        changed |= ImGui::SliderFloat("Damping", &echoParams.damping, 0.0f, 0.99f, "%.2f");
        changed |= ImGui::SliderFloat("Feedback", &echoParams.feedback, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Spread", &echoParams.spread, -1.0f, 1.0f, "%.2f");
        ImGui::PopItemWidth();

        if (changed)
        {
            types::BusEffectConfig config;
            config.id = effectId;
            config.type = types::AudioEffectType::Echo;
            config.params = echoParams;

            events::audio::UpdateBusEffectCommand cmd;
            cmd.busName = busName;
            cmd.effectId = effectId;
            cmd.config = config;
            dispatcher.execute(cmd);
        }
    }

    void AudioMixerWindow::drawChorusEditor(const std::string& busName, uint32_t effectId)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        static types::ChorusParams chorusParams;
        bool changed = false;

        ImGui::PushItemWidth(150);

        const char* waveforms[] = {"Sinusoid", "Triangle"};
        changed |= ImGui::Combo("Waveform", &chorusParams.waveform, waveforms, 2);
        changed |= ImGui::SliderInt("Phase", &chorusParams.phase, -180, 180);
        changed |= ImGui::SliderFloat("Rate", &chorusParams.rate, 0.0f, 10.0f, "%.1f Hz");
        changed |= ImGui::SliderFloat("Depth", &chorusParams.depth, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Feedback", &chorusParams.feedback, -1.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Delay", &chorusParams.delay, 0.0f, 0.016f, "%.4f s");

        ImGui::PopItemWidth();

        if (changed)
        {
            types::BusEffectConfig config;
            config.id = effectId;
            config.type = types::AudioEffectType::Chorus;
            config.params = chorusParams;

            events::audio::UpdateBusEffectCommand cmd;
            cmd.busName = busName;
            cmd.effectId = effectId;
            cmd.config = config;
            dispatcher.execute(cmd);
        }
    }

    void AudioMixerWindow::drawCreateBusSection()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        ImGui::Text("Create Bus:");

        char buf[128] = {};
        if (newBusName.size() < sizeof(buf)) std::copy(newBusName.begin(), newBusName.end(), buf);
        ImGui::PushItemWidth(150);
        if (ImGui::InputText("##NewBusName", buf, sizeof(buf))) newBusName = buf;
        ImGui::PopItemWidth(); ImGui::SameLine();

        auto busNames = dispatcher.query(events::audio::GetBusNamesQuery{});
        if (selectedParentIndex >= static_cast<int>(busNames.size())) selectedParentIndex = 0;
        const char* parentPreview = busNames.empty() ? "Master" : busNames[selectedParentIndex].c_str();
        ImGui::PushItemWidth(120);
        if (ImGui::BeginCombo("##ParentBus", parentPreview)) {
            for (int i = 0; i < static_cast<int>(busNames.size()); ++i) {
                bool isSelected = (selectedParentIndex == i);
                if (ImGui::Selectable(busNames[i].c_str(), isSelected)) selectedParentIndex = i;
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth(); ImGui::SameLine();

        ImGui::BeginDisabled(newBusName.empty());
        if (ImGui::Button("Add Bus")) {
            events::audio::CreateBusCommand cmd; cmd.busName = newBusName;
            cmd.parentName = busNames.empty() ? "Master" : busNames[selectedParentIndex];
            dispatcher.execute(cmd);
            newBusName.clear();
        }
        ImGui::EndDisabled();
    }

    void AudioMixerWindow::drawSnapshotSection()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        ImGui::Text("Mix Snapshots:");

        char buf[128] = {};
        if (snapshotName.size() < sizeof(buf)) std::copy(snapshotName.begin(), snapshotName.end(), buf);
        ImGui::PushItemWidth(200);
        if (ImGui::InputText("##SnapshotName", buf, sizeof(buf))) snapshotName = buf;
        ImGui::PopItemWidth(); ImGui::SameLine();

        ImGui::BeginDisabled(snapshotName.empty());
        if (ImGui::Button("Save")) {
            events::audio::SaveMixSnapshotCommand cmd; cmd.name = snapshotName;
            dispatcher.execute(cmd);
        }
        ImGui::EndDisabled();

        auto snapshotNames = dispatcher.query(events::audio::GetSnapshotNamesQuery{});
        if (!snapshotNames.empty()) {
            ImGui::Spacing();
            for (const auto& name : snapshotNames) {
                ImGui::PushID(name.c_str());
                if (ImGui::Button("Load")) { events::audio::LoadMixSnapshotCommand cmd; cmd.name = name; dispatcher.execute(cmd); }
                ImGui::SameLine();
                if (ImGui::Button("Delete")) { events::audio::DeleteMixSnapshotCommand cmd; cmd.name = name; dispatcher.execute(cmd); }
                ImGui::SameLine(); ImGui::Text("%s", name.c_str());
                ImGui::PopID();
            }
        }
    }
}
