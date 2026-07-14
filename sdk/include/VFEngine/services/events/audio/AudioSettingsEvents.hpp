#pragma once

#include "../EventTypes.hpp"
#include "types/AudioTypes.hpp"

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

    // VK-1506: broadcast when audio settings are applied so main-thread consumers
    // (AudioSceneUpdater's doppler teleport guard) can cache the value instead of
    // querying the full settings struct every frame.
    struct AudioSettingsChangedNotification : ::events::INotification
    {
        float maxDopplerSpeed = 343.3f;
        std::string_view getName() const override { return "AudioSettingsChanged"; }
    };
}
