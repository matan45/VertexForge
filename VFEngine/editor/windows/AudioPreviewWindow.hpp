#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "resource/Types.hpp"
#include <string>
#include <optional>
#include <vector>

namespace windows
{
    // Cached waveform point for rendering
    struct WaveformPoint
    {
        float minVal;
        float maxVal;
    };

    class AudioPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string audioPath;
        std::string windowTitle;

        // Audio data (metadata only, raw data cleared after waveform generation)
        uint32_t totalDurationInSeconds = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t frames = 0;
        size_t dataSizeBytes = 0;
        bool loadFailed = false;
        bool audioLoaded = false;

        // Cached waveform data
        std::vector<WaveformPoint> waveformCache;
        static constexpr size_t WAVEFORM_RESOLUTION = 1024;

        // Window state
        bool isOpen = true;
        bool needsInit = true;

        // Placeholder playback state (for future use)
        bool isPlaying = false;
        float playbackPosition = 0.0f;

    public:
        explicit AudioPreviewWindow(const std::string& filePath);
        ~AudioPreviewWindow() override = default;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }
        const std::string& getAudioPath() const { return audioPath; }

    private:
        void loadAudio();
        void generateWaveformCache(const resource::AudioData& data);
        void drawInfoPanel();
        void drawWaveformPanel();
    };
}
