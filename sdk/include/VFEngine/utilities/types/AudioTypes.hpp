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
        AudioDistanceModel distanceModel = AudioDistanceModel::InverseDistanceClamped;
        float defaultRolloffFactor = 1.0f;

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
            settings.distanceModel = AudioDistanceModel::InverseDistanceClamped;
            settings.defaultRolloffFactor = 1.0f;
            settings.enableDistanceFilter = true;
            settings.defaultFilterStartDistance = 10.0f;
            settings.defaultFilterMaxDistance = 100.0f;
            settings.defaultFilterIntensity = 1.0f;
            return settings;
        }
    };
}
