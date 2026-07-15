#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "interfaces/audio/IAudioService.hpp"
#include "AudioVariationDrawer.hpp"
#include <unordered_map>

namespace windows::details
{
    class AudioSource2DDrawer
    {
    private:
        std::unordered_map<uint64_t, services::AudioHandle> audioPreviewHandles;
        // VK-1520: per-entity preview cursor, so hitting Play repeatedly auditions
        // the variation instead of the same clip. Runtime state, kept here rather
        // than on the DTO — see AudioVariationDrawer.hpp.
        AudioPreviewRollMap previewRolls;

    public:
        bool draw(services::EntityHandle handle);
        void clearHandles();

    private:
        bool drawHeader(bool& outRemove);
        bool drawAudioFilePath(services::AudioSource2DData& audioData);
        bool drawAudioSettings(services::AudioSource2DData& audioData);
        bool drawVariation(services::AudioSource2DData& audioData);
        void drawPlaybackControls(services::EntityHandle handle, const services::AudioSource2DData& audioData);
    };
}
