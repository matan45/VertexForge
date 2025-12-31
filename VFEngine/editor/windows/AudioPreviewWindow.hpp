#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "resource/Types.hpp"
#include "interfaces/IAudioService.hpp"
#include <string>
#include <optional>
#include <vector>
#include <future>
#include <atomic>

namespace windows
{
    // Cached waveform point for rendering
    struct WaveformPoint
    {
        float minVal;
        float maxVal;
    };

    // Audio loading result from background thread
    struct AudioLoadResult
    {
        bool success = false;
        std::string errorMessage;
        uint32_t totalDurationInSeconds = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t frames = 0;
        size_t dataSizeBytes = 0;
        std::vector<WaveformPoint> waveformCache;
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

        // Async loading state
        std::future<AudioLoadResult> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::atomic<bool> loadingCancelled{false};
        float loadingProgress = 0.0f;
        std::string loadingStatus = "Starting...";

        // Playback state
        bool isPlaying = false;
        float playbackPosition = 0.0f;
        float volume = 1.0f;
        services::AudioHandle currentAudioHandle;
        float audioDurationSeconds = 0.0f;

    public:
        explicit AudioPreviewWindow(const std::string& filePath);
        ~AudioPreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }
        const std::string& getAudioPath() const { return audioPath; }

    private:
        void startAsyncLoad();
        void updateAsyncLoading();
        AudioLoadResult loadAudioBackground(const std::string& path);
        static std::vector<WaveformPoint> generateWaveformCache(const resource::AudioData& data, uint32_t channels);
        void drawInfoPanel();
        void drawWaveformPanel();
        void drawLoadingIndicator();
    };
}
