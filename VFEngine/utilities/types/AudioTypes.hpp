#pragma once
#include <cstdint>

namespace types
{
    // OpenAL Distance Model enumeration
    enum class AudioDistanceModel : uint8_t
    {
        None = 0,                    // AL_NONE - no distance attenuation
        InverseDistance = 1,         // AL_INVERSE_DISTANCE
        InverseDistanceClamped = 2,  // AL_INVERSE_DISTANCE_CLAMPED (default)
        LinearDistance = 3,          // AL_LINEAR_DISTANCE
        LinearDistanceClamped = 4,   // AL_LINEAR_DISTANCE_CLAMPED
        ExponentDistance = 5,        // AL_EXPONENT_DISTANCE
        ExponentDistanceClamped = 6  // AL_EXPONENT_DISTANCE_CLAMPED
    };

    struct AudioSettings
    {
        // Listener Settings
        float masterVolume = 1.0f;           // AL_GAIN on listener (0.0 - any positive value)
        float dopplerFactor = 1.0f;          // alDopplerFactor (0.0 = disabled, 1.0 = normal)
        float speedOfSound = 343.3f;         // alSpeedOfSound in m/s (default 343.3)

        // Distance Model Settings
        AudioDistanceModel distanceModel = AudioDistanceModel::InverseDistanceClamped;
        float defaultRolloffFactor = 1.0f;   // Default rolloff for new sources (0.0 - any positive)

        // Factory method for defaults (matching OpenAL defaults)
        static AudioSettings createDefault()
        {
            AudioSettings settings;
            settings.masterVolume = 1.0f;
            settings.dopplerFactor = 1.0f;
            settings.speedOfSound = 343.3f;
            settings.distanceModel = AudioDistanceModel::InverseDistanceClamped;
            settings.defaultRolloffFactor = 1.0f;
            return settings;
        }
    };
}
