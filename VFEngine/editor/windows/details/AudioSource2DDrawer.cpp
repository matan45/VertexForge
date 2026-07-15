#include "print/Log.hpp"
#include "AudioSource2DDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include <imgui.h>
#include <algorithm>
#include <fstream>

namespace windows::details
{
    bool AudioSource2DDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasAudioSource2DComponentQuery hasAudioQuery;
        hasAudioQuery.entity = handle;
        bool hasAudioSource = dispatcher.query(hasAudioQuery);

        if (!hasAudioSource)
        {
            return false;
        }

        events::scene::GetAudioSource2DDataQuery audioQuery;
        audioQuery.entity = handle;
        auto audioOpt = dispatcher.query(audioQuery);

        if (!audioOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("AudioSource2DComponent");

        bool removeAudioSource = false;
        bool isOpen = drawHeader(removeAudioSource);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::AudioSource2DData audioData = *audioOpt;
            bool changed = false;

            ImGui::TextDisabled("Use for: background music, ambient sounds");
            ImGui::Spacing();

            changed |= drawAudioFilePath(audioData);
            ImGui::Spacing();
            changed |= drawAudioSettings(audioData);
            ImGui::Spacing();
            changed |= drawVariation(audioData);

            if (changed)
            {
                events::scene::SetAudioSource2DDataCommand cmd;
                cmd.entity = handle;
                cmd.audioData = audioData;
                dispatcher.execute(cmd);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            drawPlaybackControls(handle, audioData);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeAudioSource)
        {
            events::scene::RemoveAudioSource2DComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);

            uint64_t previewKey = handle.id;
            audioPreviewHandles.erase(previewKey);
            previewRolls.erase(previewKey);
        }

        return true;
    }

    bool AudioSource2DDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##AudioSource2DHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Audio Source 2D (Streaming)");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveAudioSource2D", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool AudioSource2DDrawer::drawAudioFilePath(services::AudioSource2DData& audioData)
    {
        // VK-1520: the clip pool is drawn as one flat list — row 0 is audioRef,
        // rows 1..N are clipVariants. A user who only ever wants one clip sees the
        // same Select/Clear pair they always had.
        return drawClipVariantList(audioData, "2D");
    }

    bool AudioSource2DDrawer::drawVariation(services::AudioSource2DData& audioData)
    {
        // Collapsed by default: the single-clip user never has to open it.
        if (!ImGui::CollapsingHeader("Variation##2D"))
        {
            return false;
        }
        ImGui::Indent(10.0f);
        const bool changed = drawVariationSettings(audioData, "2D");
        ImGui::Unindent(10.0f);
        return changed;
    }

    bool AudioSource2DDrawer::drawAudioSettings(services::AudioSource2DData& audioData)
    {
        bool changed = false;

        if (ImGui::SliderFloat("Volume##2D", &audioData.volume, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }

        if (ImGui::SliderFloat("Pitch##2D", &audioData.pitch, 0.5f, 2.0f, "%.2f"))
        {
            changed = true;
        }

        ImGui::Spacing();

        if (ImGui::Checkbox("Loop##2D", &audioData.loop))
        {
            changed = true;
        }

        ImGui::Spacing();

        auto& busDispatcher = events::EventDispatcher::instance();
        events::audio::GetBusNamesQuery busNamesQuery;
        auto busNames = busDispatcher.query(busNamesQuery);
        if (!busNames.empty())
        {
            if (ImGui::BeginCombo("Bus##2D", audioData.busName.c_str()))
            {
                for (const auto& name : busNames)
                {
                    bool isSelected = (audioData.busName == name);
                    if (ImGui::Selectable(name.c_str(), isSelected))
                    {
                        audioData.busName = name;
                        changed = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        // VK-1513: arbitration weight when the scene's real-voice budget is full. Lower wins.
        // An int temp because the field is a uint8_t and DragInt needs an int*.
        int priority = static_cast<int>(audioData.priority);
        if (ImGui::DragInt("Priority##2D", &priority, 1.0f, 0, 255))
        {
            audioData.priority = static_cast<uint8_t>(std::clamp(priority, 0, 255));
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Lower = more important (0 = critical, 128 = normal, 255 = least).\n"
                              "When the voice budget is full, the least important sound is stolen.");
        }

        return changed;
    }

    void AudioSource2DDrawer::drawPlaybackControls(services::EntityHandle handle,
                                                   const services::AudioSource2DData& audioData)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        uint64_t previewKey = handle.id;
        auto previewIt = audioPreviewHandles.find(previewKey);
        bool hasPreviewHandle = previewIt != audioPreviewHandles.end();

        // Clean up invalid handles
        if (hasPreviewHandle && !previewIt->second.isValid())
        {
            audioPreviewHandles.erase(previewIt);
            hasPreviewHandle = false;
            previewIt = audioPreviewHandles.end();
        }

        // Check if currently playing
        bool isCurrentlyPlaying = false;
        if (hasPreviewHandle)
        {
            events::audio::IsSoundPlayingQuery playingQuery;
            playingQuery.handle = previewIt->second;
            isCurrentlyPlaying = dispatcher.query(playingQuery);
        }

        bool isPaused = hasPreviewHandle && !isCurrentlyPlaying;

        // Play button. VK-1520: a variant-only source (audioRef cleared but variants
        // authored) is playable, so gate on the pool rather than on audioRef alone.
        bool hasClip = audioData.audioRef.isValid();
        for (const auto& variant : audioData.clipVariants)
        {
            hasClip = hasClip || variant.isValid();
        }
        bool canPlay = hasClip && !isCurrentlyPlaying;
        if (!canPlay) ImGui::BeginDisabled();
        if (ImGui::Button("Play##2D", ImVec2(60, 0)))
        {
            if (isPaused)
            {
                events::audio::ResumeSoundCommand resumeCmd;
                resumeCmd.handle = previewIt->second;
                dispatcher.execute(resumeCmd);
            }
            else
            {
                // VK-1520: audition through the same roll the runtime uses, so
                // hitting Play five times gives five different footsteps. This is
                // the only place a designer can actually hear the container.
                const auto pick = rollPreview(audioData, previewRolls[previewKey]);
                if (!pick.path.empty())
                {
                    events::audio::PlayStreamingSoundCommand playCmd;
                    playCmd.path = pick.path;
                    playCmd.params.volume = pick.volume;
                    playCmd.params.pitch = pick.pitch;
                    playCmd.params.loop = audioData.loop;
                    playCmd.params.busName = audioData.busName;

                    services::AudioHandle newHandle = dispatcher.execute(playCmd);
                    audioPreviewHandles[previewKey] = newHandle;
                }
            }
        }
        if (!canPlay) ImGui::EndDisabled();

        // Pause button
        ImGui::SameLine();
        if (!isCurrentlyPlaying) ImGui::BeginDisabled();
        if (ImGui::Button("Pause##2D", ImVec2(60, 0)))
        {
            if (hasPreviewHandle)
            {
                events::audio::PauseSoundCommand pauseCmd;
                pauseCmd.handle = previewIt->second;
                dispatcher.execute(pauseCmd);
            }
        }
        if (!isCurrentlyPlaying) ImGui::EndDisabled();

        // Stop button
        ImGui::SameLine();
        if (!hasPreviewHandle) ImGui::BeginDisabled();
        if (ImGui::Button("Stop##2D", ImVec2(60, 0)))
        {
            if (hasPreviewHandle)
            {
                events::audio::StopSoundCommand stopCmd;
                stopCmd.handle = previewIt->second;
                dispatcher.execute(stopCmd);
                audioPreviewHandles.erase(previewKey);
            }
        }
        if (!hasPreviewHandle) ImGui::EndDisabled();
    }

    void AudioSource2DDrawer::clearHandles()
    {
        audioPreviewHandles.clear();
        previewRolls.clear();
    }
}
