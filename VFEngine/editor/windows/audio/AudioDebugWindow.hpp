#pragma once

// VK-1515: "what is actually playing right now" — the audio equivalent of the VFX/animation
// stats windows, and this engine's answer to UE's au.Debug.Sounds.
//
// Deliberately polls on a timer rather than every frame (AudioMixerWindow does the latter
// and fires a fistful of queries per bus per frame). The rows are rebuilt on the audio
// thread at 10Hz, so reading them at 60Hz would just copy the same vector sixty times.

#include "imguiHandler/ImguiWindow.hpp"
#include "AudioVoiceRows.hpp"
#include "types/AudioTypes.hpp"

#include <vector>

namespace windows
{
    class AudioDebugWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        // Mirrors `visible` so draw() can spot the edge and gate the audio thread's capture.
        // Not merged with `visible`: ImGui's close button writes that one behind our back.
        bool captureEnabled = false;

        std::vector<types::AudioVoiceRow> cachedVoices;
        types::AudioVoiceStats cachedStats;
        bool audioAvailable = true;

        // Starts due, so the first frame after opening shows data instead of an empty table.
        float refreshTimer = REFRESH_INTERVAL;
        static constexpr float REFRESH_INTERVAL = 0.5f;

        voicerows::Column sortColumn = voicerows::Column::Level;
        bool sortAscending = false; // loudest first: the default question is "what is loud"

    public:
        explicit AudioDebugWindow() = default;
        ~AudioDebugWindow() override = default;

        void draw() override;
        void show() { visible = true; }

    private:
        void refreshData();
        void setCaptureEnabled(bool enabled);
        void drawSummary();
        void drawVoiceTable();
    };
}
