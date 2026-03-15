#include "AudioMixerWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include <imgui.h>

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

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Audio Mixer", &visible))
        {
            drawBusChannels();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            drawCreateBusSection();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            drawSnapshotSection();
        }
        ImGui::End();
    }

    void AudioMixerWindow::drawBusChannels()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::audio::GetBusNamesQuery namesQuery;
        auto busNames = dispatcher.query(namesQuery);

        if (busNames.empty())
        {
            ImGui::TextDisabled("No audio buses configured");
            return;
        }

        float channelWidth = 80.0f;
        float sliderHeight = 150.0f;

        for (size_t i = 0; i < busNames.size(); ++i)
        {
            const auto& name = busNames[i];

            ImGui::BeginGroup();
            ImGui::PushID(static_cast<int>(i));

            // Bus name label
            ImGui::Text("%s", name.c_str());

            // Volume query
            events::audio::GetBusVolumeQuery volQuery;
            volQuery.busName = name;
            float volume = dispatcher.query(volQuery);

            // Vertical volume slider
            ImGui::PushItemWidth(30.0f);
            if (ImGui::VSliderFloat("##vol", ImVec2(30, sliderHeight), &volume, 0.0f, 1.0f, ""))
            {
                events::audio::SetBusVolumeCommand cmd;
                cmd.busName = name;
                cmd.volume = volume;
                dispatcher.execute(cmd);
            }
            ImGui::PopItemWidth();

            // Volume readout
            ImGui::Text("%.0f%%", volume * 100.0f);

            // Mute button
            events::audio::IsBusMutedQuery muteQuery;
            muteQuery.busName = name;
            bool muted = dispatcher.query(muteQuery);

            if (muted)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            }
            if (ImGui::Button("M", ImVec2(25, 20)))
            {
                events::audio::SetBusMutedCommand cmd;
                cmd.busName = name;
                cmd.muted = !muted;
                dispatcher.execute(cmd);
            }
            if (muted)
            {
                ImGui::PopStyleColor();
            }

            // Solo button
            ImGui::SameLine();

            // Note: Solo query uses the muted query type pattern but we'd need a separate query.
            // For now, use a simple toggle approach since we don't have IsSoloedQuery registered.
            if (ImGui::Button("S", ImVec2(25, 20)))
            {
                events::audio::SetBusSoloedCommand cmd;
                cmd.busName = name;
                cmd.soloed = true;
                dispatcher.execute(cmd);
            }

            ImGui::PopID();
            ImGui::EndGroup();

            if (i < busNames.size() - 1)
            {
                ImGui::SameLine();
                ImGui::Dummy(ImVec2(10, 0));
                ImGui::SameLine();
            }
        }
    }

    void AudioMixerWindow::drawCreateBusSection()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Text("Create Bus:");

        char buf[128] = {};
        if (newBusName.size() < sizeof(buf))
        {
            std::copy(newBusName.begin(), newBusName.end(), buf);
        }

        ImGui::PushItemWidth(150);
        if (ImGui::InputText("##NewBusName", buf, sizeof(buf)))
        {
            newBusName = buf;
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();

        events::audio::GetBusNamesQuery namesQuery;
        auto busNames = dispatcher.query(namesQuery);

        ImGui::PushItemWidth(120);
        if (selectedParentIndex >= static_cast<int>(busNames.size()))
        {
            selectedParentIndex = 0;
        }
        const char* parentPreview = busNames.empty() ? "Master" : busNames[selectedParentIndex].c_str();
        if (ImGui::BeginCombo("##ParentBus", parentPreview))
        {
            for (int i = 0; i < static_cast<int>(busNames.size()); ++i)
            {
                bool isSelected = (selectedParentIndex == i);
                if (ImGui::Selectable(busNames[i].c_str(), isSelected))
                {
                    selectedParentIndex = i;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();

        bool canCreate = !newBusName.empty();
        ImGui::BeginDisabled(!canCreate);
        if (ImGui::Button("Add Bus"))
        {
            std::string parentName = busNames.empty() ? "Master" : busNames[selectedParentIndex];
            events::audio::CreateBusCommand cmd;
            cmd.busName = newBusName;
            cmd.parentName = parentName;
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
        if (snapshotName.size() < sizeof(buf))
        {
            std::copy(snapshotName.begin(), snapshotName.end(), buf);
        }

        ImGui::PushItemWidth(200);
        if (ImGui::InputText("##SnapshotName", buf, sizeof(buf)))
        {
            snapshotName = buf;
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        bool canSave = !snapshotName.empty();
        ImGui::BeginDisabled(!canSave);
        if (ImGui::Button("Save"))
        {
            events::audio::SaveMixSnapshotCommand cmd;
            cmd.name = snapshotName;
            dispatcher.execute(cmd);
        }
        ImGui::EndDisabled();

        // Existing snapshots
        events::audio::GetSnapshotNamesQuery snapshotQuery;
        auto snapshotNames = dispatcher.query(snapshotQuery);

        if (!snapshotNames.empty())
        {
            ImGui::Spacing();
            for (const auto& name : snapshotNames)
            {
                ImGui::PushID(name.c_str());
                if (ImGui::Button("Load"))
                {
                    events::audio::LoadMixSnapshotCommand cmd;
                    cmd.name = name;
                    dispatcher.execute(cmd);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                {
                    events::audio::DeleteMixSnapshotCommand cmd;
                    cmd.name = name;
                    dispatcher.execute(cmd);
                }
                ImGui::SameLine();
                ImGui::Text("%s", name.c_str());
                ImGui::PopID();
            }
        }
    }
}
