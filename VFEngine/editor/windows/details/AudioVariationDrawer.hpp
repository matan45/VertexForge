#pragma once
#include "asset/AssetRef.hpp"
#include "nfd/FileDialog.hpp"
#include "print/Log.hpp"
#include "types/AudioVariationTypes.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include <imgui.h>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// VK-1520: the variation-container UI, shared by AudioSource2DDrawer and
// AudioSource3DDrawer. Their DTOs (services::AudioSource2DData / 3DData) carry
// identical variation blocks, so these are templated over the DTO rather than
// written twice — the two drawers must not drift apart.
//
// Header-only inline: both call sites are the only consumers, and the templates
// need the definition anyway.

namespace windows::details
{
    // Per-entity preview cursor for the drawer's Play button. Deliberately drawer
    // -local rather than on the DTO: lastVariant/playCount are runtime state and
    // are excluded from the DTO on purpose (the drawer round-trips the whole DTO
    // every frame a slider is dragged, which would reset the round-robin cursor
    // mid-drag). Mirrors the existing audioPreviewHandles map.
    struct AudioPreviewRoll
    {
        uint8_t lastVariant = types::AUDIO_VARIANT_NONE;
        uint32_t playCount = 0;
    };

    using AudioPreviewRollMap = std::unordered_map<uint64_t, AudioPreviewRoll>;

    namespace audiovariation
    {
        inline std::string fileNameOf(const asset::AssetRef& ref)
        {
            std::string filename = ref.resolve();
            const auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            return filename;
        }

        // The shared "pick a .vfAudio from disk" flow, matching the existing
        // Select Audio File button (validity check + log on failure).
        inline bool pickAudioFile(asset::AssetRef& outRef)
        {
            nfd::FileDialog fileDialog;
            const std::string path =
                fileDialog.openFileDialog({{L"VF Audio Files (*.vfAudio)", L"*.vfAudio"}});
            if (path.empty())
            {
                return false;
            }

            std::ifstream file(path);
            if (!file.good())
            {
                vfLogError("Selected audio file does not exist or cannot be read: {}", path);
                return false;
            }
            file.close();
            outRef = asset::AssetRef::fromPath(path);
            return true;
        }
    }

    // Draws the whole clip pool as ONE flat list. Row 0 is bound to audioData
    // .audioRef and rows 1..N to clipVariants — the storage seam is deliberately
    // invisible to the designer, who just sees "the clips this source plays".
    // Row 0 has no Remove button: clearing it is what "no clip" means, and the
    // engine treats audioRef as variant 0 of the pool.
    template<typename AudioData>
    bool drawClipVariantList(AudioData& audioData, const char* suffix)
    {
        bool changed = false;

        ImGui::PushID(suffix);

        // --- row 0: audioRef ---
        ImGui::PushID(-1);
        if (audioData.audioRef.isValid())
        {
            ImGui::Text("[0] %s", audiovariation::fileNameOf(audioData.audioRef).c_str());
        }
        else
        {
            ImGui::TextDisabled("No audio file selected");
        }
        if (auto dropped = acceptAssetDropOnLastItem("AudioClipDrop", {".vfaudio"}))
        {
            audioData.audioRef = asset::AssetRef::fromPath(*dropped);
            changed = true;
        }

        if (ImGui::Button("Select Audio File"))
        {
            changed |= audiovariation::pickAudioFile(audioData.audioRef);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!audioData.audioRef.isValid());
        if (ImGui::Button("Clear"))
        {
            audioData.audioRef = asset::AssetRef::invalid();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();

        // --- rows 1..N: clipVariants ---
        // Deferred erase by index — mutating the vector mid-iteration would
        // invalidate the loop. Same shape as VFXSequenceDrawer::drawTriggers.
        int variantToRemove = -1;
        for (int i = 0; i < static_cast<int>(audioData.clipVariants.size()); ++i)
        {
            auto& variant = audioData.clipVariants[i];
            ImGui::PushID(i);

            if (variant.isValid())
            {
                ImGui::Text("[%d] %s", i + 1, audiovariation::fileNameOf(variant).c_str());
            }
            else
            {
                ImGui::TextDisabled("[%d] <empty>", i + 1);
            }
            if (auto dropped = acceptAssetDropOnLastItem("AudioVariantDrop", {".vfaudio"}))
            {
                variant = asset::AssetRef::fromPath(*dropped);
                changed = true;
            }

            if (ImGui::Button("Select"))
            {
                changed |= audiovariation::pickAudioFile(variant);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!variant.isValid());
            if (ImGui::Button("Clear"))
            {
                variant = asset::AssetRef::invalid();
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Remove"))
            {
                variantToRemove = i;
            }

            ImGui::PopID();
        }

        if (variantToRemove >= 0)
        {
            audioData.clipVariants.erase(audioData.clipVariants.begin() + variantToRemove);
            changed = true;
        }

        // The pool is [audioRef] ++ clipVariants and is indexed by uint8_t with
        // 0xFF reserved as a sentinel, so the list caps one below audioRef's slot.
        const bool atCap = audioData.clipVariants.size() + 1 >= types::AUDIO_MAX_VARIANTS;
        ImGui::BeginDisabled(atCap);
        if (ImGui::Button("+ Add Variant"))
        {
            audioData.clipVariants.emplace_back();
            changed = true;
        }
        ImGui::EndDisabled();
        if (atCap && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("A source can hold at most %zu clips.", types::AUDIO_MAX_VARIANTS);
        }

        ImGui::PopID();
        return changed;
    }

    // Play order + the two jitter sliders. Jitter is intentionally shown in ALL
    // play orders including Single: one clip must be de-mechanisable without
    // authoring fake variants.
    template<typename AudioData>
    bool drawVariationSettings(AudioData& audioData, const char* suffix)
    {
        bool changed = false;
        ImGui::PushID(suffix);

        // An int temp because the field is a uint8_t enum and Combo needs an int*
        // — same reason as the Priority DragInt above.
        static const char* const orderNames[] = {"Single", "Random", "Round Robin"};
        int order = static_cast<int>(audioData.playOrder);
        if (ImGui::Combo("Play Order", &order, orderNames, IM_ARRAYSIZE(orderNames)))
        {
            audioData.playOrder = static_cast<types::AudioPlayOrder>(order);
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Single: always play the first clip.\n"
                              "Random: pick a clip at random, never the same one twice in a row.\n"
                              "Round Robin: cycle through the clips in order.");
        }

        // Percent temps so the label reads as a designer thinks ("+/- 10%"). The
        // 0..50% range is derived from the clamps in AudioVariationTypes.hpp, so
        // the safety clamp never bites a legitimately authored value.
        float pitchPct = audioData.pitchVariation * 100.0f;
        if (ImGui::SliderFloat("Pitch Variation", &pitchPct, 0.0f, 50.0f, "+/- %.0f%%"))
        {
            audioData.pitchVariation = pitchPct * 0.01f;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Randomises pitch around the authored value on every play.\n"
                              "0%% is inert. Applies in every play order.");
        }

        float volumePct = audioData.volumeVariation * 100.0f;
        if (ImGui::SliderFloat("Volume Variation", &volumePct, 0.0f, 50.0f, "+/- %.0f%%"))
        {
            audioData.volumeVariation = volumePct * 0.01f;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            // The asymmetry is real and looks like a bug if it is not called out.
            ImGui::SetTooltip("Randomises volume around the authored value on every play.\n"
                              "Volume 1.0 is the ceiling, so variation there only ducks -\n"
                              "lower volume to ~0.9 for symmetric swing.");
        }

        ImGui::PopID();
        return changed;
    }

    // Rolls the preview audition: which clip to play and at what pitch/volume.
    // Uses the same pure helpers as the runtime play path, so hitting Play five
    // times auditions five different footsteps exactly as the game will.
    struct AudioPreviewPick
    {
        std::string path;
        float volume = 1.0f;
        float pitch = 1.0f;
    };

    template<typename AudioData>
    AudioPreviewPick rollPreview(const AudioData& audioData, AudioPreviewRoll& state)
    {
        std::vector<const asset::AssetRef*> pool;
        pool.reserve(1 + audioData.clipVariants.size());
        if (audioData.audioRef.isValid())
        {
            pool.push_back(&audioData.audioRef);
        }
        for (const auto& variant : audioData.clipVariants)
        {
            if (pool.size() >= types::AUDIO_MAX_VARIANTS)
            {
                break;
            }
            if (variant.isValid())
            {
                pool.push_back(&variant);
            }
        }

        AudioPreviewPick<AudioData> pick;
        if (pool.empty())
        {
            return pick;
        }

        // No entity id here — the preview is one source at a time, so playCount
        // alone carries the entropy.
        const uint32_t seed = types::audioPlaySeed(0u, state.playCount++);
        const uint8_t index = types::selectVariant(
            pool.size(), state.lastVariant, audioData.playOrder,
            types::audioVarianceUnit(seed, types::AudioVarianceStream::ClipSelect));
        state.lastVariant = index;

        pick.path = pool[index]->resolve();
        pick.pitch = types::audioJitterPitch(
            audioData.pitch, audioData.pitchVariation,
            types::audioVarianceSigned(seed, types::AudioVarianceStream::Pitch));
        pick.volume = types::audioJitterVolume(
            audioData.volume, audioData.volumeVariation,
            types::audioVarianceSigned(seed, types::AudioVarianceStream::Volume));
        return pick;
    }
}
