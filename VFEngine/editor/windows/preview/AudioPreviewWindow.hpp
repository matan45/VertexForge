#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewWindowChrome.hpp"
#include "resource/Types.hpp"
#include "interfaces/audio/IAudioService.hpp"
#include <string>
#include <vector>
#include <future>
#include <atomic>

namespace windows
{
   
    struct WaveformPoint
    {
        float minVal;
        float maxVal;
    };
    
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
        
        uint32_t totalDurationInSeconds = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t frames = 0;
        size_t dataSizeBytes = 0;
        bool loadFailed = false;
        bool audioLoaded = false;

      
        std::vector<WaveformPoint> waveformCache;
        static constexpr size_t WAVEFORM_RESOLUTION = 1024;

       
        bool isOpen = true;
        bool needsInit = true;

        editor::preview::WindowMaximizer maximizer;
        ImVec2 initialSize{0.0f, 0.0f};
        bool sizeSaved = false;


        std::future<AudioLoadResult> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::atomic<bool> loadingCancelled{false};
        std::string loadingStatus = "Starting...";

        
        bool isPlaying = false;
        bool isScrubbing = false;
        float playbackPosition = 0.0f;
        float volume = 1.0f;
        bool loopEnabled = false;
        float pitch = 1.0f;
        services::AudioHandle currentAudioHandle;
        float audioDurationSeconds = 0.0f;

    public:
        explicit AudioPreviewWindow(const std::string& filePath);
        ~AudioPreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }

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
