#include "print/Log.hpp"
#include "AudioSource3DDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/audio/AudioBusEvents.hpp"
// VK-1518: occlusion trace-channel layer names (same pair ColliderDrawer uses)
#include "events/physics/PhysicsSettingsEvents.hpp"
#include "types/PhysicsTypes.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "resource/VfAudioHeader.hpp"
#include "resource/Types.hpp"
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
            changed |= drawVariation(audioData);
            ImGui::Spacing();
            changed |= drawSpatialSettings(audioData);
            ImGui::Spacing();
            changed |= drawDistanceFilterSettings(audioData);
            ImGui::Spacing();
            changed |= drawOcclusionSettings(audioData);
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
            previewRolls.erase(previewKey);
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
        // VK-1520: the clip pool is drawn as one flat list — row 0 is audioRef,
        // rows 1..N are clipVariants.
        const bool changed = drawClipVariantList(audioData, "3D");
        drawStereoBadge(audioData);
        return changed;
    }

    // VK-1507 advisory badge: OpenAL spatializes mono best. A stereo clip on a 3D
    // source is downmixed at runtime (AL_SOURCE_SPATIALIZE_SOFT), but a Force-Mono
    // reimport is smaller and spatializes cleaner.
    //
    // VK-1520 note: this checks audioRef (pool variant 0) only, not the variant
    // clips — the path cache holds a single entry, and badging every variant would
    // need a map keyed by path for no real gain. The badge is advisory either way,
    // and Half 1 of VK-1507 makes stereo audible regardless.
    void AudioSource3DDrawer::drawStereoBadge(const services::AudioSource3DData& audioData)
    {
        if (!audioData.audioRef.isValid())
        {
            return;
        }

        const std::string fullPath = audioData.audioRef.resolve();

        // Re-read the .vfAudio header only when the resolved path changes (no
        // per-frame disk IO).
        if (fullPath != cachedStereoPath)
        {
            cachedStereoPath = fullPath;
            cachedIsStereo = false;
            std::ifstream headerFile(fullPath, std::ios::binary);
            if (headerFile.good())
            {
                resource::VfAudioHeader header;
                resource::readVfAudioHeader(headerFile, header);
                // The reader performs no validation, so only trust the channel
                // count when the stream read succeeded and the file is a .vfAudio.
                if (headerFile.good() &&
                    header.fileType == static_cast<uint8_t>(resource::FileType::AUDIO))
                {
                    cachedIsStereo = header.channels > 1;
                }
            }
        }

        if (cachedIsStereo)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "Stereo clip - reimport with Force Mono for full spatialization");
        }
    }

    bool AudioSource3DDrawer::drawVariation(services::AudioSource3DData& audioData)
    {
        // Collapsed by default: the single-clip user never has to open it.
        if (!ImGui::CollapsingHeader("Variation##3D"))
        {
            return false;
        }
        ImGui::Indent(10.0f);
        const bool changed = drawVariationSettings(audioData, "3D");
        ImGui::Unindent(10.0f);
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

        // VK-1513: arbitration weight when the scene's real-voice budget is full. Lower wins.
        // An int temp because the field is a uint8_t and DragInt needs an int*.
        int priority = static_cast<int>(audioData.priority);
        if (ImGui::DragInt("Priority##3D", &priority, 1.0f, 0, 255))
        {
            audioData.priority = static_cast<uint8_t>(std::clamp(priority, 0, 255));
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Lower = more important (0 = critical, 128 = normal, 255 = least).\n"
                              "When the voice budget is full, the least important sound is stolen.\n"
                              "Distant sounds are already ranked quieter, so priority is a tie-breaker.");
        }

        // VK-1521: ramp up from silence when the sound starts. 0 = no fade.
        if (ImGui::DragFloat("Fade In (ms)##3D", &audioData.fadeInMs, 10.0f, 0.0f, 10000.0f,
                             "%.0f"))
        {
            audioData.fadeInMs = std::max(0.0f, audioData.fadeInMs);
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Ramp this sound up from silence over this many milliseconds.\n"
                              "0 = start at full volume.");
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

    bool AudioSource3DDrawer::drawOcclusionSettings(services::AudioSource3DData& audioData)
    {
        bool changed = false;

        ImGui::Text("Occlusion:");
        ImGui::Indent(10.0f);

        if (ImGui::Checkbox("Enable Occlusion##3D", &audioData.enableOcclusion))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Muffle this sound when level geometry blocks the line of sight\n"
                              "between the listener and this emitter");
        }

        if (audioData.enableOcclusion)
        {
            // Both sliders are CUT AMOUNTS, not gains: 0 leaves the sound untouched, 1 is a
            // full cut. Same sense as Filter Intensity above. The tooltips have to say so —
            // it is the one thing the name alone gets wrong.
            if (ImGui::SliderFloat("Occlusion Low-pass##3D", &audioData.occlusionLpf, 0.0f, 1.0f, "%.2f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("How much of the high end is cut when fully occluded\n"
                                  "0 = no effect, 1 = fully dark. This is what makes a wall\n"
                                  "sound like a wall rather than just a volume drop.");
            }

            if (ImGui::SliderFloat("Occlusion Volume##3D", &audioData.occlusionVolume, 0.0f, 1.0f, "%.2f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("How much volume is cut when fully occluded\n"
                                  "0 = no effect, 1 = silent. Affects the direct path only,\n"
                                  "so an occluded sound still feeds reverb zones.");
            }

            changed |= drawOcclusionLayerMask(audioData);
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool AudioSource3DDrawer::drawOcclusionLayerMask(services::AudioSource3DData& audioData)
    {
        bool changed = false;

        // Same source of truth (and same fallback) as ColliderDrawer's layer combo.
        std::vector<types::CollisionLayer> layers;
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::physics::GetCollisionLayersQuery layersQuery;
            layers = dispatcher.query(layersQuery);
        }
        catch (...)
        {
            // Query failed, fall through to the defaults below.
        }

        if (layers.empty())
        {
            layers = types::PhysicsSettings::createDefault().layers;
        }

        if (layers.empty())
        {
            ImGui::TextDisabled("No collision layers available");
            return false;
        }

        ImGui::Text("Blocked By:");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("The trace channel: which collision layers count as blocking.\n"
                              "Leave Sensor off (trigger volumes are not walls), and leave the\n"
                              "layer your player capsule lives on off, or the listener's own\n"
                              "body will occlude everything.");
        }

        ImGui::Indent(10.0f);
        for (const auto& layer : layers)
        {
            if (layer.index >= 16)
            {
                continue;  // the mask is a uint16_t; MAX_COLLISION_LAYERS is 16
            }
            const auto bit = static_cast<uint16_t>(1u << layer.index);
            bool enabled = (audioData.occlusionLayerMask & bit) != 0;
            const std::string label = layer.name + "##OcclusionLayer" + std::to_string(layer.index);
            if (ImGui::Checkbox(label.c_str(), &enabled))
            {
                if (enabled)
                {
                    audioData.occlusionLayerMask |= bit;
                }
                else
                {
                    audioData.occlusionLayerMask &= static_cast<uint16_t>(~bit);
                }
                changed = true;
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

        // Play button. VK-1520: a variant-only source (audioRef cleared but variants
        // authored) is playable, so gate on the pool rather than on audioRef alone.
        bool hasClip = audioData.audioRef.isValid();
        for (const auto& variant : audioData.clipVariants)
        {
            hasClip = hasClip || variant.isValid();
        }
        bool canPlay = hasClip && !isCurrentlyPlaying;
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

                // VK-1520: audition through the same roll the runtime uses, so
                // hitting Play five times gives five different footsteps. This is
                // the only place a designer can actually hear the container.
                const auto pick = rollPreview(audioData, previewRolls[previewKey]);

                events::audio::PlaySound3DCommand playCmd;
                playCmd.path = pick.path;
                playCmd.position = position;
                playCmd.params.volume = pick.volume;
                playCmd.params.pitch = pick.pitch;
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

                // Empty path == nothing playable (every clip in the pool invalid).
                if (!pick.path.empty())
                {
                    services::AudioHandle newHandle = dispatcher.execute(playCmd);
                    audioPreviewHandles[previewKey] = newHandle;
                }
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
        previewRolls.clear();
    }
}
