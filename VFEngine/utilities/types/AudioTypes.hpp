#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>
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

    // VK-1513: live occupancy of the real-voice budget, reported by the audio thread for
    // the editor's readout. `maxRealVoices` <= 0 means the cap is disabled.
    struct AudioVoiceStats
    {
        int realVoices = 0;
        int maxRealVoices = 0;
        // VK-1515: voices kept alive on a simulated clock after losing the budget. Not
        // capped by maxRealVoices — they hold no AL source (see kDefaultMaxVirtualVoices).
        int virtualVoices = 0;
    };

    // VK-1515: what the editor's active-sounds overlay shows for one live voice.
    //
    // Deliberately NOT folded into the audio thread's AudioStateSnapshot: that is returned
    // by value and isPlaying()/getPlaybackPosition()/getDuration() each call it, so two
    // std::strings per voice would be deep-copied on every one of those hot main-thread
    // calls. This travels on its own gated channel, published only while the overlay is
    // open — the same reasoning that kept AudioVoiceStats out of the snapshot.
    enum class AudioVoiceKind : uint8_t
    {
        Sound2D = 0, // pooled, AL_SOURCE_RELATIVE at the origin — never attenuates
        Sound3D = 1, // pooled, positional
        Stream = 2,  // streaming (music/ambience); exempt from the real-voice budget
        Virtual = 3  // holds no AL source; advancing a clock until it is revived
    };

    inline const char* audioVoiceKindToString(AudioVoiceKind kind)
    {
        switch (kind)
        {
        case AudioVoiceKind::Sound2D: return "2D";
        case AudioVoiceKind::Sound3D: return "3D";
        case AudioVoiceKind::Stream:  return "Stream";
        case AudioVoiceKind::Virtual: return "Virtual";
        }
        return "?";
    }

    struct AudioVoiceRow
    {
        uint64_t handle = 0;
        std::string path;
        std::string busName;
        AudioVoiceKind kind = AudioVoiceKind::Sound2D;
        // Estimated, pre-effects. RMS envelope x distance attenuation x source gain (which
        // carries user volume and bus volume/mute/solo) x fade gain. Not the bus meters'
        // input, which is pre-bus-fader — see AudioBusManager::updateBusMeters.
        float level = 0.0f;
        // LOWER = MORE IMPORTANT (0 = critical, 128 = neutral). Do not render it backwards.
        uint8_t priority = 128;
        bool playing = false; // false = paused; still holds its slot, so it still costs
        glm::vec3 position{0.0f};
        float distance = 0.0f;         // to the listener; 0 for 2D and streaming voices
        float playbackPosition = 0.0f; // seconds; a virtual voice's simulated clock
    };

    // VK-1519: one fixed-reduction sidechain assignment for a target bus. amountDb is a
    // positive attenuation amount; 0 dB is inert. These limits are shared by the backend
    // and editor so queued commands cannot bypass UI validation.
    inline constexpr float kBusDuckMinThresholdDb = -60.0f;
    inline constexpr float kBusDuckMaxThresholdDb = 0.0f;
    inline constexpr float kBusDuckMinAmountDb = 0.0f;
    inline constexpr float kBusDuckMaxAmountDb = 60.0f;
    inline constexpr float kBusDuckMinTimeMs = 0.0f;
    inline constexpr float kBusDuckMaxTimeMs = 10000.0f;

    struct BusDuckConfig
    {
        std::string sourceBus;
        float thresholdDb = -30.0f;
        float amountDb = 12.0f;
        float attackMs = 50.0f;
        float releaseMs = 250.0f;
    };

    // VK-1514/VK-1519: estimated post-authored-bus-fader, pre-duck, pre-effects RMS and
    // its decaying hold. Duck detectors deliberately consume this stable signal.
    struct AudioBusLevel
    {
        std::string name;
        float rms = 0.0f;
        float peakHold = 0.0f;
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
        // VK-1506: a single-frame move implying a higher speed (m/s) is treated as a
        // teleport and produces no doppler pitch shift. Consumed by AudioSceneUpdater.
        float maxDopplerSpeed = 343.3f;
        AudioDistanceModel distanceModel = AudioDistanceModel::InverseDistanceClamped;
        float defaultRolloffFactor = 1.0f;

        // VK-1508: request binaural HRTF rendering on the OpenAL device (headphones).
        bool enableHrtf = false;

        // VK-1513: scene-wide budget of simultaneously playing (real) voices. When it is
        // full, an incoming sound either steals the slot of the least-important live voice
        // or is denied outright — see core/audio/VoicePolicy.hpp. <= 0 disables the cap.
        // Takes effect for newly started sounds; live voices are never cut off, and the
        // source pool is never shrunk.
        int maxRealVoices = 64;

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
            settings.maxRealVoices = 64;
            settings.enableDistanceFilter = true;
            settings.defaultFilterStartDistance = 10.0f;
            settings.defaultFilterMaxDistance = 100.0f;
            settings.defaultFilterIntensity = 1.0f;
            return settings;
        }
    };

    // VK-1513. The full answer about a live handle, read from one snapshot.
    //
    // `rejected` disambiguates the two very different things "not playing" can mean. The
    // handle is minted on the calling thread and returned immediately, BEFORE the audio
    // thread has arbitrated the request — so for the first few ticks "not playing" honestly
    // means "not yet", and a caller must not conclude anything from it. If the arbitration
    // then throws the request away (the voice budget denied it AND the virtual set was
    // full), "not playing" becomes permanent — but it looks identical, so a caller waiting
    // to observe the voice play waits forever. This is the flag that tells them apart.
    //
    // Lives here rather than beside AudioHandle so the Audio DLL can answer the query
    // without pulling in a service interface for a POD.
    struct SoundStatus
    {
        bool playing = false;
        bool rejected = false;
    };
}
