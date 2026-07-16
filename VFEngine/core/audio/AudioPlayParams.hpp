#pragma once

#include "AudioExport.hpp"
#include <glm/glm.hpp>
#include <string>

namespace core::audio
{
    #pragma warning(push)
    #pragma warning(disable: 4251)
    struct VF_AUDIO_API PlaySoundParams
    {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
        bool streaming = false;

        bool enableDistanceFilter = false;
        float filterStartDistance = 10.0f;
        float filterMaxDistance = 100.0f;
        float filterIntensity = 1.0f;

        float innerConeAngle = 360.0f;
        float outerConeAngle = 360.0f;
        float outerConeGain = 0.0f;
        glm::vec3 direction{0.0f, 0.0f, -1.0f};

        std::string busName = "Master";

        // VK-1513: voice-budget arbitration weight. LOWER = MORE IMPORTANT (0 = critical,
        // 255 = least); 128 is exactly neutral. Consumed by VoicePolicy at play time and
        // retained in AudioThread's voice record — deliberately NOT forwarded into
        // AudioSourceConfig, which exists only to be mapped onto OpenAL, and OpenAL has no
        // priority concept.
        uint8_t priority = 128;

        // VK-1521: ramp this sound up from silence over fadeInMs instead of starting at full
        // gain. 0 (the default) means no fade, which keeps every pre-VK-1521 play byte-identical.
        // Consumed at play time by AudioSourceManager::startFadeIn (pooled) or passed into
        // StreamingAudioManager::playStreaming (streaming) — and, like priority above,
        // deliberately NOT forwarded into AudioSourceConfig: the ramp is driven by the fade
        // queues, and OpenAL has no fade concept to map it onto.
        float fadeInMs = 0.0f;
    };
    #pragma warning(pop)
}
