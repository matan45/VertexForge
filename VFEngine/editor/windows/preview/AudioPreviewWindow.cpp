#include "AudioPreviewWindow.hpp"
#include "imgui.h"
#include "resource/ResourceManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/AudioEvents.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace windows
{
    AudioPreviewWindow::AudioPreviewWindow(const std::string& filePath)
        : audioPath(filePath)
    {
        std::filesystem::path path(filePath);
        windowTitle = "Audio Preview: " + path.filename().string();
    }

    AudioPreviewWindow::~AudioPreviewWindow()
    {
        // Cancel loading if in progress
        loadingCancelled.store(true);
        if (loadFuture.valid())
        {
            loadFuture.wait();
        }
        
        if (currentAudioHandle.isValid())
        {
            events::audio::StopSoundCommand stopCmd;
            stopCmd.handle = currentAudioHandle;
            events::EventDispatcher::instance().execute(stopCmd);
        }
    }

    void AudioPreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }
        
        if (needsInit)
        {
            startAsyncLoad();
            needsInit = false;
        }
        
        updateAsyncLoading();

        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                float panelWidth = 150.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                
                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();
                
                float waveformWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("WaveformPanel", ImVec2(waveformWidth, contentSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                if (loadingInProgress.load())
                {
                    drawLoadingIndicator();
                }
                else
                {
                    drawWaveformPanel();
                }

                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void AudioPreviewWindow::startAsyncLoad()
    {
        loadingInProgress.store(true);
        loadingCancelled.store(false);
        loadingStatus = "Loading audio file...";

        loadFuture = std::async(std::launch::async, [this]() {
            return loadAudioBackground(audioPath);
        });
    }

    void AudioPreviewWindow::updateAsyncLoading()
    {
        if (!loadingInProgress.load() || !loadFuture.valid())
        {
            return;
        }
        
        if (loadFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            try
            {
                AudioLoadResult result = loadFuture.get();

                if (result.success)
                {
                    totalDurationInSeconds = result.totalDurationInSeconds;
                    channels = result.channels;
                    sampleRate = result.sampleRate;
                    frames = result.frames;
                    dataSizeBytes = result.dataSizeBytes;
                    waveformCache = std::move(result.waveformCache);
                    audioLoaded = true;
                }
                else
                {
                    loadFailed = true;
                }
            }
            catch (const std::exception&)
            {
                loadFailed = true;
            }

            loadingInProgress.store(false);
        }
    }

    AudioLoadResult AudioPreviewWindow::loadAudioBackground(const std::string& path)
    {
        AudioLoadResult result;

        try
        {
            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            auto audioFuture = resource::ResourceManager::loadAudioAsync(path);
            auto audioPtr = audioFuture.get();
            if (!audioPtr)
            {
                result.errorMessage = "Failed to load audio data";
                return result;
            }

            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            result.totalDurationInSeconds = audioPtr->totalDurationInSeconds;
            result.channels = audioPtr->channels;
            result.sampleRate = audioPtr->sampleRate;
            result.frames = audioPtr->frames;
            result.dataSizeBytes = audioPtr->data.size() * sizeof(short);

            result.waveformCache = generateWaveformCache(*audioPtr, audioPtr->channels);

            result.success = true;
        }
        catch (const std::exception& e)
        {
            result.errorMessage = e.what();
        }
        catch (...)
        {
            result.errorMessage = "Unknown error";
        }

        return result;
    }

    std::vector<WaveformPoint> AudioPreviewWindow::generateWaveformCache(const resource::AudioData& data, uint32_t numChannels)
    {
        std::vector<WaveformPoint> cache;

        if (data.data.empty() || numChannels == 0)
        {
            return cache;
        }

        cache.reserve(WAVEFORM_RESOLUTION);

        size_t numSamples = data.data.size() / numChannels;
        size_t samplesPerPoint = std::max(size_t(1), numSamples / WAVEFORM_RESOLUTION);

        for (size_t i = 0; i < WAVEFORM_RESOLUTION; ++i)
        {
            size_t sampleStart = i * samplesPerPoint * numChannels;
            size_t sampleEnd = std::min(sampleStart + samplesPerPoint * numChannels, data.data.size());

            short minVal = 0, maxVal = 0;
            for (size_t j = sampleStart; j < sampleEnd; j += numChannels)
            {
                for (uint32_t ch = 0; ch < numChannels && j + ch < sampleEnd; ++ch)
                {
                    short sample = data.data[j + ch];
                    minVal = std::min(minVal, sample);
                    maxVal = std::max(maxVal, sample);
                }
            }

            WaveformPoint point;
            point.minVal = static_cast<float>(minVal) / 32768.0f;
            point.maxVal = static_cast<float>(maxVal) / 32768.0f;
            cache.push_back(point);
        }

        return cache;
    }

    void AudioPreviewWindow::drawLoadingIndicator()
    {
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        ImVec2 windowPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        drawList->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y),
            IM_COL32(30, 30, 30, 255)
        );
        
        float contentWidth = 200.0f;
        float contentHeight = 80.0f;
        float centerX = windowPos.x + (availSize.x - contentWidth) * 0.5f;
        float centerY = windowPos.y + (availSize.y - contentHeight) * 0.5f;
        
        float time = static_cast<float>(ImGui::GetTime());
        float spinnerRadius = 16.0f;
        float spinnerThickness = 3.0f;
        ImVec2 spinnerCenter(centerX + contentWidth * 0.5f, centerY + 20.0f);
        
        int numSegments = 24;
        float startAngle = time * 4.0f;
        float arcLength = 3.14159f * 1.3f;

        for (int i = 0; i < numSegments; ++i)
        {
            float t1 = static_cast<float>(i) / static_cast<float>(numSegments);
            float t2 = static_cast<float>(i + 1) / static_cast<float>(numSegments);
            float angle1 = startAngle + t1 * arcLength;
            float angle2 = startAngle + t2 * arcLength;
            
            int alpha = static_cast<int>(255 * (1.0f - t1 * 0.7f));
            ImU32 segColor = IM_COL32(100, 180, 255, alpha);

            ImVec2 p1(spinnerCenter.x + cosf(angle1) * spinnerRadius,
                      spinnerCenter.y + sinf(angle1) * spinnerRadius);
            ImVec2 p2(spinnerCenter.x + cosf(angle2) * spinnerRadius,
                      spinnerCenter.y + sinf(angle2) * spinnerRadius);

            drawList->AddLine(p1, p2, segColor, spinnerThickness);
        }
        
        const char* statusText = loadingStatus.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(statusText);
        ImVec2 textPos(centerX + (contentWidth - textSize.x) * 0.5f, centerY + 50.0f);
        drawList->AddText(textPos, IM_COL32(200, 200, 200, 255), statusText);
        
        ImGui::Dummy(availSize);
    }

    void AudioPreviewWindow::drawInfoPanel()
    {
        ImGui::Text("Info");
        ImGui::Separator();

        if (loadFailed)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load");
            return;
        }

        if (!audioLoaded)
        {
            ImGui::TextDisabled("Loading...");
            return;
        }
        
        uint32_t minutes = totalDurationInSeconds / 60;
        uint32_t seconds = totalDurationInSeconds % 60;
        ImGui::Text("Duration:");
        ImGui::Text("  %02u:%02u", minutes, seconds);

        ImGui::Spacing();
        
        ImGui::Text("Channels: %u", channels);
        ImGui::Text("Sample Rate:");
        ImGui::Text("  %u Hz", sampleRate);
        ImGui::Text("Frames: %u", frames);

        ImGui::Spacing();
        
        size_t dataSizeKB = dataSizeBytes / 1024;
        ImGui::Text("Data Size:");
        ImGui::Text("  %zu KB", dataSizeKB);

        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            
            if (currentAudioHandle.isValid())
            {
                events::audio::IsSoundPlayingQuery playingQuery;
                playingQuery.handle = currentAudioHandle;
                isPlaying = dispatcher.query(playingQuery);
                
                if (isPlaying)
                {
                    events::audio::GetPlaybackPositionQuery posQuery;
                    posQuery.handle = currentAudioHandle;
                    float currentPos = dispatcher.query(posQuery);
                    if (audioDurationSeconds > 0.0f)
                    {
                        playbackPosition = currentPos / audioDurationSeconds;
                    }
                }
            }
            else
            {
                isPlaying = false;
            }
            
            if (ImGui::Button(isPlaying ? "Pause" : "Play", ImVec2(-1, 0)))
            {
                if (isPlaying)
                {
                    events::audio::PauseSoundCommand pauseCmd;
                    pauseCmd.handle = currentAudioHandle;
                    dispatcher.execute(pauseCmd);
                }
                else
                {
                    if (!currentAudioHandle.isValid())
                    {
                        events::audio::PlayStreamingSoundCommand playCmd;
                        playCmd.path = audioPath;
                        playCmd.params.volume = volume;
                        currentAudioHandle = dispatcher.execute(playCmd);
                        
                        if (currentAudioHandle.isValid())
                        {
                            events::audio::GetDurationQuery durQuery;
                            durQuery.handle = currentAudioHandle;
                            audioDurationSeconds = dispatcher.query(durQuery);
                        }
                    }
                    else
                    {
                        events::audio::ResumeSoundCommand resumeCmd;
                        resumeCmd.handle = currentAudioHandle;
                        dispatcher.execute(resumeCmd);
                    }
                }
            }
            
            if (ImGui::Button("Stop", ImVec2(-1, 0)))
            {
                if (currentAudioHandle.isValid())
                {
                    events::audio::StopSoundCommand stopCmd;
                    stopCmd.handle = currentAudioHandle;
                    dispatcher.execute(stopCmd);
                    currentAudioHandle = {};
                }
                playbackPosition = 0.0f;
            }
            
            if (ImGui::SliderFloat("##Position", &playbackPosition, 0.0f, 1.0f, ""))
            {
                if (currentAudioHandle.isValid() && audioDurationSeconds > 0.0f)
                {
                    float seekSeconds = playbackPosition * audioDurationSeconds;
                    events::audio::SetPlaybackPositionCommand seekCmd;
                    seekCmd.handle = currentAudioHandle;
                    seekCmd.seconds = seekSeconds;
                    dispatcher.execute(seekCmd);
                }
            }
            
            if (audioDurationSeconds > 0.0f)
            {
                float currentSeconds = playbackPosition * audioDurationSeconds;
                int curMin = static_cast<int>(currentSeconds) / 60;
                int curSec = static_cast<int>(currentSeconds) % 60;
                int totalMin = static_cast<int>(audioDurationSeconds) / 60;
                int totalSec = static_cast<int>(audioDurationSeconds) % 60;
                ImGui::Text("%02d:%02d / %02d:%02d", curMin, curSec, totalMin, totalSec);
            }

            ImGui::Spacing();
            
            ImGui::Text("Volume");
            float volumePercent = volume * 100.0f;
            if (ImGui::SliderFloat("##Volume", &volumePercent, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
            {
                volume = volumePercent / 100.0f;
                if (currentAudioHandle.isValid())
                {
                    events::audio::SetSoundVolumeCommand volCmd;
                    volCmd.handle = currentAudioHandle;
                    volCmd.volume = volume;
                    dispatcher.execute(volCmd);
                }
            }
        }
    }

    void AudioPreviewWindow::drawWaveformPanel()
    {
        ImGui::Text("Waveform");
        ImGui::Separator();

        if (loadFailed || !audioLoaded || waveformCache.empty())
        {
            ImGui::TextDisabled("No waveform data");
            return;
        }

        ImVec2 availSize = ImGui::GetContentRegionAvail();
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize(availSize.x, availSize.y - 10.0f);
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
            IM_COL32(30, 30, 30, 255));
        
        float centerY = canvasPos.y + canvasSize.y * 0.5f;
        drawList->AddLine(
            ImVec2(canvasPos.x, centerY),
            ImVec2(canvasPos.x + canvasSize.x, centerY),
            IM_COL32(60, 60, 60, 255));
        
        ImU32 waveColor = IM_COL32(100, 180, 255, 255);
        float halfHeight = canvasSize.y * 0.45f;
        float xScale = canvasSize.x / static_cast<float>(waveformCache.size());

        for (size_t i = 0; i < waveformCache.size(); ++i)
        {
            const auto& point = waveformCache[i];
            float x = canvasPos.x + i * xScale;
            float y1 = centerY - point.maxVal * halfHeight;
            float y2 = centerY - point.minVal * halfHeight;

            drawList->AddLine(ImVec2(x, y1), ImVec2(x, y2), waveColor);
        }
        
        if (playbackPosition > 0.0f || isPlaying)
        {
            float playheadX = canvasPos.x + playbackPosition * canvasSize.x;
            drawList->AddLine(
                ImVec2(playheadX, canvasPos.y),
                ImVec2(playheadX, canvasPos.y + canvasSize.y),
                IM_COL32(255, 100, 100, 255),
                2.0f);
        }
        
        ImGui::Dummy(canvasSize);
    }
}
