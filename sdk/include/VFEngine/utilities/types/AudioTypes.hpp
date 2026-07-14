#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "AudioEffectTypes.hpp"

namespace types
{
    enum class AudioDistanceModel : uint8_t
    {
        None = 0,
        InverseDistance = 1,
        InverseDistanceClamped = 2,
        LinearDistance = 3,
        LinearDistanceClamped = 4,
        ExponentDistance = 5,
        ExponentDistanceClamped = 6
    };

    // VK-1508: live HRTF (binaural) state reported by the OpenAL device.
    // Values 0..5 deliberately match ALC_HRTF_*_SOFT so the audio backend can
    // static_cast the raw ALC status straight into this enum; -1 = extension absent.
    enum class AudioHrtfStatus : int
    {
        Unsupported = -1,
        Disabled = 0,
        Enabled = 1,
        Denied = 2,
        Required = 3,
        HeadphonesDetected = 4,
        UnsupportedFormat = 5
    };

    inline const char* audioHrtfStatusToString(AudioHrtfStatus status)
    {
        switch (status)
        {
        case AudioHrtfStatus::Disabled:           return "Disabled";
        case AudioHrtfStatus::Enabled:            return "Enabled";
        case AudioHrtfStatus::Denied:             return "Denied";
        case AudioHrtfStatus::Required:           return "Required";
        case AudioHrtfStatus::HeadphonesDetected: return "Headphones detected";
        case AudioHrtfStatus::UnsupportedFormat:  return "Unsupported format";
        case AudioHrtfStatus::Unsupported:        return "Not supported";
        }
        return "Not supported";
    }

    struct AudioBusDefinition
    {
        std::string name;
        std::string parentName = "Master";
        float defaultVolume = 1.0f;
        std::vector<BusEffectConfig> effects;
    };

    struct AudioMixSnapshotDefinition
    {
        std::string name;
        std::map<std::string, float> busVolumes;
        std::map<std::string, bool> busMutes;
    };

    struct AudioSettings
    {
        float masterVolume = 1.0f;
        float dopplerFactor = 1.0f;
        float speedOfSound = 343.3f;
        // VK-1506: a single-frame move implying a higher speed (m/s) is treated as a
        // teleport and produces no doppler pitch shift. Consumed by AudioSceneUpdater.
        float maxDopplerSpeed = 343.3f;
        AudioDistanceModel distanceModel = AudioDistanceModel::InverseDistanceClamped;
        float defaultRolloffFactor = 1.0f;

        // VK-1508: request binaural HRTF rendering on the OpenAL device (headphones).
        bool enableHrtf = false;

        bool enableDistanceFilter = true;
        float defaultFilterStartDistance = 10.0f;
        float defaultFilterMaxDistance = 100.0f;
        float defaultFilterIntensity = 1.0f;

        std::vector<AudioBusDefinition> busDefinitions;
        std::vector<AudioMixSnapshotDefinition> mixSnapshots;

        static AudioSettings createDefault()
        {
            AudioSettings settings;
            settings.masterVolume = 1.0f;
            settings.dopplerFactor = 1.0f;
            settings.speedOfSound = 343.3f;
            settings.maxDopplerSpeed = 343.3f;
            settings.distanceModel = AudioDistanceModel::InverseDistanceClamped;
            settings.defaultRolloffFactor = 1.0f;
            settings.enableHrtf = false;
            settings.enableDistanceFilter = true;
            settings.defaultFilterStartDistance = 10.0f;
            settings.defaultFilterMaxDistance = 100.0f;
            settings.defaultFilterIntensity = 1.0f;
            return settings;
        }
    };
}
