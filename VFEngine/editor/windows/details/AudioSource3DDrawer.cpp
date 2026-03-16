#include "print/Log.hpp"
#include "AudioSource3DDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include <imgui.h>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <glm/trigonometric.hpp>

namespace windows::details
{
    bool AudioSource3DDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasAudioSource3DComponentQuery hasAudioQuery;
        hasAudioQuery.entity = handle;
        bool hasAudioSource = dispatcher.query(hasAudioQuery);

        if (!hasAudioSource)
        {
            return false;
        }

        events::scene::GetAudioSource3DDataQuery audioQuery;
        audioQuery.entity = handle;
        auto audioOpt = dispatcher.query(audioQuery);

        if (!audioOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("AudioSource3DComponent");

        bool removeAudioSource = false;
        bool isOpen = drawHeader(removeAudioSource);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::AudioSource3DData audioData = *audioOpt;
            bool changed = false;

            ImGui::TextDisabled("Use for: spatial sound effects");
            ImGui::Spacing();

            changed |= drawAudioFilePath(audioData);
            ImGui::Spacing();
            changed |= drawAudioSettings(audioData);
            ImGui::Spacing();
            changed |= drawSpatialSettings(audioData);
            ImGui::Spacing();
            changed |= drawDistanceFilterSettings(audioData);
            ImGui::Spacing();
            changed |= drawConeSettings(audioData);

            if (changed)
            {
                events::scene::SetAudioSource3DDataCommand cmd;
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
            events::scene::RemoveAudioSource3DComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);

            uint64_t previewKey = handle.id;
            audioPreviewHandles.erase(previewKey);
        }

        return true;
    }

    bool AudioSource3DDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##AudioSource3DHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Audio Source 3D (Spatial)");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveAudioSource3D", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool AudioSource3DDrawer::drawAudioFilePath(services::AudioSource3DData& audioData)
    {
        bool changed = false;

        if (audioData.audioRef.isValid())
        {
            std::string filename = audioData.audioRef.resolve();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("File: %s", filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("No audio file selected");
        }

        if (ImGui::Button("Select Audio File##3D"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Audio Files (*.vfAudio)", L"*.vfAudio"}});
            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    audioData.audioRef = asset::AssetRef::fromPath(path);
                    changed = true;
                }
                else
                {
                    vfLogError("Selected audio file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool clearDisabled = !audioData.audioRef.isValid();
        ImGui::BeginDisabled(clearDisabled);
        if (ImGui::Button("Clear##Audio3D"))
        {
            audioData.audioRef = asset::AssetRef::invalid();
            changed = true;
        }
        ImGui::EndDisabled();

        return changed;
    }

    bool AudioSource3DDrawer::drawAudioSettings(services::AudioSource3DData& audioData)
    {
        bool changed = false;

        if (ImGui::SliderFloat("Volume##3D", &audioData.volume, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }

        if (ImGui::SliderFloat("Pitch##3D", &audioData.pitch, 0.5f, 2.0f, "%.2f"))
        {
            changed = true;
        }

        ImGui::Spacing();

        if (ImGui::Checkbox("Loop##3D", &audioData.loop))
        {
            changed = true;
        }

        ImGui::Spacing();

        auto& busDispatcher = events::EventDispatcher::instance();
        events::audio::GetBusNamesQuery busNamesQuery;
        auto busNames = busDispatcher.query(busNamesQuery);
        if (!busNames.empty())
        {
            if (ImGui::BeginCombo("Bus##3D", audioData.busName.c_str()))
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

        return changed;
    }

    bool AudioSource3DDrawer::drawSpatialSettings(services::AudioSource3DData& audioData)
    {
        bool changed = false;

        ImGui::Text("Spatial Settings:");
        ImGui::Indent(10.0f);

        if (ImGui::SliderFloat("Min Distance##3D", &audioData.minDistance, 0.1f, 50.0f, "%.1f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Distance at which volume starts to attenuate");
        }

        if (ImGui::SliderFloat("Max Distance##3D", &audioData.maxDistance, 1.0f, 500.0f, "%.1f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Distance at which volume reaches minimum");
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Show Debug Spheres##3D", &audioData.showDebugSpheres))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Draw wireframe spheres for min/max distance");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool AudioSource3DDrawer::drawDistanceFilterSettings(services::AudioSource3DData& audioData)
    {
        bool changed = false;

        ImGui::Text("Distance Filter:");
        ImGui::Indent(10.0f);

        if (ImGui::Checkbox("Enable Distance Filter##3D", &audioData.enableDistanceFilter))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Apply low-pass filter based on distance to simulate air absorption");
        }

        if (audioData.enableDistanceFilter)
        {
            if (ImGui::SliderFloat("Filter Start Distance##3D", &audioData.filterStartDistance, 0.1f, 100.0f, "%.1f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Distance at which high-frequency attenuation begins");
            }

            if (ImGui::SliderFloat("Filter Max Distance##3D", &audioData.filterMaxDistance, 1.0f, 500.0f, "%.1f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Distance at which filter reaches full strength");
            }

            if (ImGui::SliderFloat("Filter Intensity##3D", &audioData.filterIntensity, 0.0f, 1.0f, "%.2f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Strength of the low-pass filter effect");
            }
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool AudioSource3DDrawer::drawConeSettings(services::AudioSource3DData& audioData)
    {
        bool changed = false;

        ImGui::Text("Cone Attenuation:");
        ImGui::Indent(10.0f);

        if (ImGui::SliderFloat("Inner Cone Angle##3D", &audioData.innerConeAngle, 0.0f, 360.0f, "%.0f deg"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Full volume within this angle (360 = omnidirectional)");
        }

        if (ImGui::SliderFloat("Outer Cone Angle##3D", &audioData.outerConeAngle, 0.0f, 360.0f, "%.0f deg"))
        {
            if (audioData.outerConeAngle < audioData.innerConeAngle)
            {
                audioData.outerConeAngle = audioData.innerConeAngle;
            }
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Volume fades to outer gain between inner and outer cone angles");
        }

        if (ImGui::SliderFloat("Outer Cone Gain##3D", &audioData.outerConeGain, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Volume multiplier outside the outer cone (0 = silent)");
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Show Debug Cone##3D", &audioData.showDebugCone))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Draw wireframe cone visualization in editor");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    void AudioSource3DDrawer::drawPlaybackControls(services::EntityHandle handle,
                                                   const services::AudioSource3DData& audioData)
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

        // Play button
        bool canPlay = audioData.audioRef.isValid() && !isCurrentlyPlaying;
        ImGui::BeginDisabled(!canPlay);
        if (ImGui::Button("Play##3D", ImVec2(60, 0)))
        {
            if (isPaused)
            {
                events::audio::ResumeSoundCommand resumeCmd;
                resumeCmd.handle = previewIt->second;
                dispatcher.execute(resumeCmd);
            }
            else
            {
                events::scene::GetTransformQuery transformQuery;
                transformQuery.entity = handle;
                auto transformOpt = dispatcher.query(transformQuery);
                glm::vec3 position = transformOpt.has_value() ? transformOpt->position : glm::vec3(0.0f);

                events::audio::PlaySound3DCommand playCmd;
                playCmd.path = audioData.audioRef.resolve();
                playCmd.position = position;
                playCmd.params.volume = audioData.volume;
                playCmd.params.pitch = audioData.pitch;
                playCmd.params.loop = audioData.loop;
                playCmd.params.minDistance = audioData.minDistance;
                playCmd.params.maxDistance = audioData.maxDistance;
                playCmd.params.enableDistanceFilter = audioData.enableDistanceFilter;
                playCmd.params.filterStartDistance = audioData.filterStartDistance;
                playCmd.params.filterMaxDistance = audioData.filterMaxDistance;
                playCmd.params.filterIntensity = audioData.filterIntensity;
                playCmd.params.innerConeAngle = audioData.innerConeAngle;
                playCmd.params.outerConeAngle = audioData.outerConeAngle;
                playCmd.params.outerConeGain = audioData.outerConeGain;
                playCmd.params.busName = audioData.busName;

                if (transformOpt.has_value())
                {
                    float yawRad = glm::radians(transformOpt->rotation.y);
                    float pitchRad = glm::radians(transformOpt->rotation.x);
                    glm::vec3 forward;
                    forward.x = -std::sin(yawRad) * std::cos(pitchRad);
                    forward.y = std::sin(pitchRad);
                    forward.z = -std::cos(yawRad) * std::cos(pitchRad);
                    playCmd.params.direction = glm::normalize(forward);
                }

                services::AudioHandle newHandle = dispatcher.execute(playCmd);
                audioPreviewHandles[previewKey] = newHandle;
            }
        }
        ImGui::EndDisabled();

        // Pause button
        ImGui::SameLine();
        ImGui::BeginDisabled(!isCurrentlyPlaying);
        if (ImGui::Button("Pause##3D", ImVec2(60, 0)))
        {
            if (hasPreviewHandle)
            {
                events::audio::PauseSoundCommand pauseCmd;
                pauseCmd.handle = previewIt->second;
                dispatcher.execute(pauseCmd);
            }
        }
        ImGui::EndDisabled();

        // Stop button
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasPreviewHandle);
        if (ImGui::Button("Stop##3D", ImVec2(60, 0)))
        {
            if (hasPreviewHandle)
            {
                events::audio::StopSoundCommand stopCmd;
                stopCmd.handle = previewIt->second;
                dispatcher.execute(stopCmd);
                audioPreviewHandles.erase(previewKey);
            }
        }
        ImGui::EndDisabled();
    }

    void AudioSource3DDrawer::clearHandles()
    {
        audioPreviewHandles.clear();
    }
}
