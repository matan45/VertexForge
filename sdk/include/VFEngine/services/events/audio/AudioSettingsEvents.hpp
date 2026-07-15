#pragma once

#include "../EventTypes.hpp"
#include "types/AudioTypes.hpp"
#include <vector>

namespace events::audio
{
    struct ApplyAudioSettingsCommand : ::events::ICommand<bool>
    {
        types::AudioSettings settings;
        std::string_view getName() const override { return "ApplyAudioSettings"; }
    };
    
    struct GetAudioSettingsQuery : ::events::IQuery<types::AudioSettings>
    {
        std::string_view getName() const override { return "GetAudioSettings"; }
    };

    // VK-1508: live device HRTF status (Enabled/Disabled/Denied/...), distinct from the
    // requested enableHrtf setting — the driver may refuse HRTF (non-headphone output etc.).
    struct GetHrtfStatusQuery : ::events::IQuery<types::AudioHrtfStatus>
    {
        std::string_view getName() const override { return "GetHrtfStatus"; }
    };

    // VK-1513: live voice-budget occupancy for the editor's "Real voices: N / M" readout.
    struct GetVoiceCountQuery : ::events::IQuery<types::AudioVoiceStats>
    {
        std::string_view getName() const override { return "GetVoiceCount"; }
    };

    // VK-1515: one row per live voice, for the active-sounds overlay. Returns empty unless
    // capture has been switched on with SetVoiceDebugEnabledCommand — the audio thread does
    // not build these unless someone is looking.
    struct GetActiveVoicesQuery : ::events::IQuery<std::vector<types::AudioVoiceRow>>
    {
        std::string_view getName() const override { return "GetActiveVoices"; }
    };

    // VK-1515: driven by the overlay's visibility, so a closed window costs the audio thread
    // one relaxed atomic load per tick and nothing else.
    struct SetVoiceDebugEnabledCommand : ::events::ICommand<void>
    {
        bool enabled = false;
        std::string_view getName() const override { return "SetVoiceDebugEnabled"; }
    };

    // VK-1506: broadcast when audio settings are applied so main-thread consumers
    // (AudioSceneUpdater's doppler teleport guard) can cache the value instead of
    // querying the full settings struct every frame.
    struct AudioSettingsChangedNotification : ::events::INotification
    {
        float maxDopplerSpeed = 343.3f;
        std::string_view getName() const override { return "AudioSettingsChanged"; }
    };
}
