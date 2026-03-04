#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "interfaces/audio/IAudioService.hpp"
#include <unordered_map>

namespace windows::details
{
    class AudioSource2DDrawer
    {
    private:
        std::unordered_map<uint64_t, services::AudioHandle> audioPreviewHandles;

    public:
        bool draw(services::EntityHandle handle);
        void clearHandles();

    private:
        bool drawHeader(bool& outRemove);
        bool drawAudioFilePath(services::AudioSource2DData& audioData);
        bool drawAudioSettings(services::AudioSource2DData& audioData);
        void drawPlaybackControls(services::EntityHandle handle, const services::AudioSource2DData& audioData);
    };
}
