#include "AudioSource3DDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/AudioEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details {

    bool AudioSource3DDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasAudioSource3DComponentQuery hasAudioQuery;
        hasAudioQuery.entity = handle;
        bool hasAudioSource = dispatcher.query(hasAudioQuery);

        if (!hasAudioSource)
            return false;

        events::scene::GetAudioSource3DDataQuery audioQuery;
        audioQuery.entity = handle;
        auto audioOpt = dispatcher.query(audioQuery);

        if (!audioOpt.has_value())
            return true;

        ImGui::PushID("AudioSource3DComponent");

        bool removeAudioSource = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##AudioSource3DHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Audio Source 3D (Spatial)");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveAudioSource3D", ImVec2(18, 18)))
        {
            removeAudioSource = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::AudioSource3DData audioData = *audioOpt;
            bool changed = false;

            ImGui::TextDisabled("Use for: spatial sound effects");
            ImGui::Spacing();

            if (!audioData.audioFilePath.empty())
            {
                std::string filename = audioData.audioFilePath;
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
                        audioData.audioFilePath = path;
                        changed = true;
                    }
                    else
                    {
                        vfLogError("Selected audio file does not exist or cannot be read: {}", path);
                    }
                }
            }

            ImGui::SameLine();
            if (audioData.audioFilePath.empty()) ImGui::BeginDisabled();
            if (ImGui::Button("Clear##Audio3D"))
            {
                audioData.audioFilePath = "";
                changed = true;
            }
            if (audioData.audioFilePath.empty()) ImGui::EndDisabled();

            ImGui::Spacing();

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

            // Check if we have an active preview handle for this entity
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

            // Track if paused (has handle but not playing)
            bool isPaused = hasPreviewHandle && !isCurrentlyPlaying;

            // Play button - only enabled if audio file is set and not already playing
            bool canPlay = !audioData.audioFilePath.empty() && !isCurrentlyPlaying;
            if (!canPlay) ImGui::BeginDisabled();
            if (ImGui::Button("Play##3D", ImVec2(60, 0)))
            {
                if (isPaused)
                {
                    // Resume from paused position
                    events::audio::ResumeSoundCommand resumeCmd;
                    resumeCmd.handle = previewIt->second;
                    dispatcher.execute(resumeCmd);
                }
                else
                {
                    // Start new playback
                    // 3D audio: use cached resource audio at entity position
                    events::scene::GetTransformQuery transformQuery;
                    transformQuery.entity = handle;
                    auto transformOpt = dispatcher.query(transformQuery);
                    glm::vec3 position = transformOpt.has_value() ? transformOpt->position : glm::vec3(0.0f);

                    events::audio::PlaySound3DCommand playCmd;
                    playCmd.path = audioData.audioFilePath;
                    playCmd.position = position;
                    playCmd.params.volume = audioData.volume;
                    playCmd.params.pitch = audioData.pitch;
                    playCmd.params.loop = audioData.loop;
                    playCmd.params.minDistance = audioData.minDistance;
                    playCmd.params.maxDistance = audioData.maxDistance;

                    services::AudioHandle newHandle = dispatcher.execute(playCmd);
                    audioPreviewHandles[previewKey] = newHandle;
                }
            }
            if (!canPlay) ImGui::EndDisabled();

            ImGui::SameLine();

            if (!isCurrentlyPlaying) ImGui::BeginDisabled();
            if (ImGui::Button("Pause##3D", ImVec2(60, 0)))
            {
                if (hasPreviewHandle)
                {
                    events::audio::PauseSoundCommand pauseCmd;
                    pauseCmd.handle = previewIt->second;
                    dispatcher.execute(pauseCmd);
                }
            }
            if (!isCurrentlyPlaying) ImGui::EndDisabled();

            ImGui::SameLine();

            if (!hasPreviewHandle) ImGui::BeginDisabled();
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
            if (!hasPreviewHandle) ImGui::EndDisabled();

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

    void AudioSource3DDrawer::clearHandles()
    {
        audioPreviewHandles.clear();
    }

}
