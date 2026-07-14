#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewWindowChrome.hpp"
#include "resource/Types.hpp"
#include "resource/AudioAnalysis.hpp"
#include "interfaces/audio/IAudioService.hpp"
#include <string>
#include <vector>
#include <future>
#include <atomic>
#include <memory>
#include <cstdint>

namespace windows
{
   
    struct AudioLoadResult
    {
        bool success = false;
        std::string errorMessage;
        uint32_t totalDurationInSeconds = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t frames = 0;
        size_t dataSizeBytes = 0;
        std::shared_ptr<resource::AudioData> audioData; // retained decoded PCM (VK-1510)
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

        // Retained decoded PCM (VK-1510): meters + spectrum read from this on the UI thread.
        std::shared_ptr<resource::AudioData> audioData;

        // Waveform view range in per-channel FRAME units; [start,end) == [0,frames] is the
        // min-zoom full view. All draw/playhead/scrub math routes through this range.
        size_t viewStartFrame = 0;
        size_t viewEndFrame = 0;

        // Re-bucket cache + invalidation guard (only re-bucket when view or width changes).
        std::vector<resource::WaveformBucket> viewBuckets;
        size_t cachedViewStart = SIZE_MAX;
        size_t cachedViewEnd = SIZE_MAX;
        int cachedBucketCount = -1;

        // Current playhead in whole-file frames (derived each frame).
        size_t currentPlayheadFrame = 0;

        // Per-channel level meters (normalized 0..1) + peak-hold state.
        std::vector<float> meterRms;
        std::vector<float> meterPeakHold;

        // Spectrum strip (collapsible, default closed).
        bool spectrumOpen = false;
        std::vector<float> spectrumBars;

        // Tuning.
        static constexpr size_t MIN_VIEW_FRAMES = 256;   // deepest zoom
        static constexpr int MAX_BUCKETS = 2048;         // ~1 bar/px cap
        static constexpr float METER_WINDOW_SEC = 0.05f; // RMS window
        static constexpr float PEAK_DECAY_PER_SEC = 0.6f;
        static constexpr float METER_COL_W = 22.0f;      // px per channel bar
        static constexpr float SPECTRUM_HEIGHT = 90.0f;


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

        // VK-1511: 3D audition mode. Auditions the clip through the real 3D
        // attenuation / cone / distance-LPF / HRTF path with NO scene entity — the
        // source is driven each frame relative to the live (camera) listener.
        bool audition3D = false;
        float auditionDistance = 5.0f;     // metres from listener
        float auditionAzimuth = 0.0f;      // deg; 0 = front, +90 = listener's right
        float auditionElevation = 0.0f;    // deg; +90 = directly above
        bool autoOrbit = false;
        float orbitSpeedDegPerSec = 45.0f;
        // Attenuation tuning (applied at Play — OpenAL fixes distance params at source creation).
        float auditionMinDistance = 1.0f;  // gain == 1.0 within this radius
        float auditionMaxDistance = 50.0f; // gain floor beyond; also the distance-slider max

    public:
        explicit AudioPreviewWindow(const std::string& filePath);
        ~AudioPreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }

    private:
        void startAsyncLoad();
        void updateAsyncLoading();
        AudioLoadResult loadAudioBackground(const std::string& path);
        void drawInfoPanel();
        void drawWaveformPanel();
        void drawLevelMeters(ImVec2 pos, ImVec2 size);
        void drawSpectrumStrip(ImVec2 pos, float width, float height);
        void drawLoadingIndicator();

        // VK-1511: stop + reset the current handle (used on any 2D<->3D mode switch so
        // the two paths never sound at once); advance orbit + re-sync the 3D source pos.
        void stopCurrentPlayback();
        void updateAudition3D();
    };
}
