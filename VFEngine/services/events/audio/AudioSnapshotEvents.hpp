#pragma once

#include "../EventTypes.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace events::audio::snapshot
{
    struct FadeOutAudioCommand : ::events::ICommand<void>
    {
        uint64_t handleId = 0;
        float fadeDurationMs = 300.0f;

        std::string_view getName() const override { return "FadeOutAudio"; }
    };

    struct GetAudioPlaybackPositionQuery : ::events::IQuery<float>
    {
        uint64_t handleId = 0;
        std::string_view getName() const override { return "GetAudioPlaybackPosition"; }
    };

    struct IsAudioPlayingQuery : ::events::IQuery<bool>
    {
        uint64_t handleId = 0;
        std::string_view getName() const override { return "IsAudioPlaying"; }
    };

    struct AudioPlaybackSnapshot
    {
        bool wasPlaying = false;
        float playbackPosition = 0.0f;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        std::string audioPath;
        std::string busName;
    };

    struct GetAudioSnapshotQuery : ::events::IQuery<std::optional<AudioPlaybackSnapshot>>
    {
        uint64_t entityUUID = 0;

        std::string_view getName() const override { return "GetAudioSnapshot"; }
    };

    // Duplicate play/seek commands for use in WorldSectorServiceImpl
    // (avoids including AudioEvents.hpp which has conflicting namespace)
    struct PlayRestoredAudio3DCommand : ::events::ICommand<uint64_t>
    {
        std::string path;
        glm::vec3 position{0.0f};
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        std::string busName;
        uint8_t priority = 128; // VK-1513: lower = more important

        std::string_view getName() const override { return "PlayRestoredAudio3D"; }
    };

    struct PlayRestoredAudio2DCommand : ::events::ICommand<uint64_t>
    {
        std::string path;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        std::string busName;
        uint8_t priority = 128; // VK-1513: lower = more important

        std::string_view getName() const override { return "PlayRestoredAudio2D"; }
    };

    struct SeekAudioCommand : ::events::ICommand<void>
    {
        uint64_t handleId = 0;
        float seconds = 0.0f;

        std::string_view getName() const override { return "SeekAudio"; }
    };

} // namespace events::audio::snapshot
