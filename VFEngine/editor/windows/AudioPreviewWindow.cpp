#include "AudioPreviewWindow.hpp"
#include "imgui.h"
#include "resource/AudioResource.hpp"
#include <filesystem>
#include <algorithm>

namespace windows
{
    AudioPreviewWindow::AudioPreviewWindow(const std::string& filePath)
        : audioPath(filePath)
    {
        std::filesystem::path path(filePath);
        windowTitle = "Audio Preview: " + path.filename().string();
    }

    void AudioPreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }

        // Load audio on first draw
        if (needsInit)
        {
            loadAudio();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                // Split layout: left panel for info, right for waveform
                float panelWidth = 150.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Info panel on the left
                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                // Waveform panel on the right
                float waveformWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("WaveformPanel", ImVec2(waveformWidth, contentSize.y), true);
                drawWaveformPanel();
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void AudioPreviewWindow::loadAudio()
    {
        try
        {
            resource::AudioData data = resource::AudioResource::loadAudio(audioPath);

            // Store metadata
            totalDurationInSeconds = data.totalDurationInSeconds;
            channels = data.channels;
            sampleRate = data.sampleRate;
            frames = data.frames;
            dataSizeBytes = data.data.size() * sizeof(short);

            // Generate waveform cache from raw data
            generateWaveformCache(data);

            // Raw audio data is now discarded (data goes out of scope)
            audioLoaded = true;
        }
        catch (...)
        {
            loadFailed = true;
        }
    }

    void AudioPreviewWindow::generateWaveformCache(const resource::AudioData& data)
    {
        if (data.data.empty() || channels == 0)
        {
            return;
        }

        waveformCache.clear();
        waveformCache.reserve(WAVEFORM_RESOLUTION);

        size_t numSamples = data.data.size() / channels;
        size_t samplesPerPoint = std::max(size_t(1), numSamples / WAVEFORM_RESOLUTION);

        for (size_t i = 0; i < WAVEFORM_RESOLUTION; ++i)
        {
            size_t sampleStart = i * samplesPerPoint * channels;
            size_t sampleEnd = std::min(sampleStart + samplesPerPoint * channels, data.data.size());

            short minVal = 0, maxVal = 0;
            for (size_t j = sampleStart; j < sampleEnd; j += channels)
            {
                // Check all channels to get accurate amplitude
                for (uint32_t ch = 0; ch < channels && j + ch < sampleEnd; ++ch)
                {
                    short sample = data.data[j + ch];
                    minVal = std::min(minVal, sample);
                    maxVal = std::max(maxVal, sample);
                }
            }

            WaveformPoint point;
            point.minVal = static_cast<float>(minVal) / 32768.0f;
            point.maxVal = static_cast<float>(maxVal) / 32768.0f;
            waveformCache.push_back(point);
        }
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

        // Duration
        uint32_t minutes = totalDurationInSeconds / 60;
        uint32_t seconds = totalDurationInSeconds % 60;
        ImGui::Text("Duration:");
        ImGui::Text("  %02u:%02u", minutes, seconds);

        ImGui::Spacing();

        // Format info
        ImGui::Text("Channels: %u", channels);
        ImGui::Text("Sample Rate:");
        ImGui::Text("  %u Hz", sampleRate);
        ImGui::Text("Frames: %u", frames);

        ImGui::Spacing();

        // Data size
        size_t dataSizeKB = dataSizeBytes / 1024;
        ImGui::Text("Data Size:");
        ImGui::Text("  %zu KB", dataSizeKB);

        ImGui::Separator();
        ImGui::Spacing();

        // Playback controls (placeholder)
        if (ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("(Coming soon)");

            ImGui::BeginDisabled(true);

            // Play/Pause button
            if (ImGui::Button(isPlaying ? "Pause" : "Play", ImVec2(-1, 0)))
            {
                isPlaying = !isPlaying;
            }

            // Stop button
            if (ImGui::Button("Stop", ImVec2(-1, 0)))
            {
                isPlaying = false;
                playbackPosition = 0.0f;
            }

            // Progress slider
            ImGui::SliderFloat("##Position", &playbackPosition, 0.0f, 1.0f, "");

            ImGui::EndDisabled();
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

        // Draw waveform background
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
            IM_COL32(30, 30, 30, 255));

        // Draw center line
        float centerY = canvasPos.y + canvasSize.y * 0.5f;
        drawList->AddLine(
            ImVec2(canvasPos.x, centerY),
            ImVec2(canvasPos.x + canvasSize.x, centerY),
            IM_COL32(60, 60, 60, 255));

        // Draw cached waveform
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

        // Reserve space for the canvas
        ImGui::Dummy(canvasSize);
    }
}
