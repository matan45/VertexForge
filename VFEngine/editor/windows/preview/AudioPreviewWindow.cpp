#include "AudioPreviewWindow.hpp"
#include "imgui.h"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include "events/EventDispatcher.hpp"
#include "events/audio/AudioEvents.hpp"
#include "math/TransformUtils.hpp"
#include <filesystem>
#include <system_error>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    // VK-1511: current listener pose for 3D audition. ListenerState's own defaults
    // already equal OpenAL's default listener (origin, -Z forward, +Y up), so the
    // returned value is a valid placement even before ViewPort dispatches its first
    // per-frame SetListenerPositionCommand (the `valid` flag is only a quality hint).
    events::audio::ListenerState currentListener()
    {
        events::audio::GetListenerStateQuery query;
        return events::EventDispatcher::instance().query(query);
    }
}

namespace
{
    // Bridges whole-file frame indices <-> waveform-canvas pixels for the current view
    // range. The single transform used by re-bucketing, the playhead, and scrubbing —
    // keeping all three consistent through zoom/scroll.
    struct ViewMap
    {
        float canvasX = 0.0f;
        float canvasW = 1.0f;
        double viewStart = 0.0; // frames
        double viewSpan = 1.0;  // frames (> 0)

        float frameToX(double f) const
        {
            return canvasX + static_cast<float>((f - viewStart) / viewSpan * static_cast<double>(canvasW));
        }
        double xToFrame(float x) const
        {
            return viewStart + static_cast<double>((x - canvasX) / canvasW) * viewSpan;
        }
    };
}

namespace
{
    // VK-1512: display strings for the .vfAudio import-info panel. These map the resource::
    // on-disk enums (NOT the 3-value importConfig:: variants). "Format" doubles as the
    // lossless/lossy quality indicator — the exact import quality tier and forceMono flag
    // are not persisted after import, so they are unrecoverable and deliberately not shown.
    // No default: case so a future enum value trips -Wswitch here.
    const char* formatDisplay(resource::AudioCompressionFormat f)
    {
        switch (f)
        {
        case resource::AudioCompressionFormat::PCM:    return "PCM (lossless)";
        case resource::AudioCompressionFormat::Vorbis: return "Vorbis (lossy)";
        }
        return "Unknown";
    }

    const char* loadTypeDisplay(resource::AudioLoadType t)
    {
        switch (t)
        {
        case resource::AudioLoadType::DecompressOnLoad: return "Decompress on load";
        case resource::AudioLoadType::Streaming:        return "Streaming";
        }
        return "Unknown";
    }
}

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

        audioData.reset(); // free retained PCM (~tens of MB for long clips)
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
        updateAudition3D();     // VK-1511: advance orbit + resync the 3D source each frame

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("AudioPreview", ImVec2(900, 550));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse | maximizer.windowFlags()))
        {
            if (isOpen)
            {
                maximizer.drawButton();

                // VK-1511: mode toggle. Top toolbar (not inside the Playback header) so it
                // stays visible when Playback is collapsed and reads as a mode switch.
                ImGui::SameLine();
                if (ImGui::Checkbox("3D Audition", &audition3D))
                {
                    stopCurrentPlayback();  // never let the 2D and 3D paths sound at once
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Audition through the real 3D attenuation / cone / "
                                      "distance-LPF / HRTF path (no scene entity)");
                }

                static float panelWidth = 150.0f;
                const float splitterThickness = 5.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                panelWidth = std::clamp(panelWidth, 120.0f,
                                        std::max(120.0f, contentSize.x - 250.0f - splitterThickness));
                float waveformWidth = contentSize.x - panelWidth - splitterThickness;

                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##audioSplit", splitterThickness, &panelWidth,
                                           &waveformWidth, 120.0f, 250.0f, contentSize.y);
                ImGui::SameLine(0.0f, 0.0f);

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

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("AudioPreview", maximizer.effectiveSize());
            sizeSaved = true;
        }
    }

    void AudioPreviewWindow::startAsyncLoad()
    {
        // Reset retained PCM + derived view/meter/spectrum state before a (re)load.
        audioData.reset();
        viewBuckets.clear();
        viewStartFrame = viewEndFrame = 0;
        cachedViewStart = cachedViewEnd = SIZE_MAX;
        cachedBucketCount = -1;
        meterRms.clear();
        meterPeakHold.clear();
        spectrumBars.clear();

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
                    compressionFormat = result.compressionFormat;
                    loadType = result.loadType;
                    onDiskSizeBytes = result.onDiskSizeBytes;
                    audioData = std::move(result.audioData); // retain PCM for meters/spectrum
                    audioDurationSeconds = (sampleRate > 0)
                        ? static_cast<float>(frames) / static_cast<float>(sampleRate)
                        : 0.0f;

                    // Init the waveform view to the full file and invalidate the bucket cache.
                    viewStartFrame = 0;
                    viewEndFrame = frames;
                    cachedViewStart = cachedViewEnd = SIZE_MAX;
                    cachedBucketCount = -1;
                    meterRms.assign(channels, 0.0f);
                    meterPeakHold.assign(channels, 0.0f);

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

            auto audioFuture = resource::ResourceManager::loadAudioAsync(asset::AssetRef::fromPath(path));
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

            // VK-1512: .vfAudio header fields (parsed into audioData by AudioResource::loadAudio,
            // AudioResource.cpp:317-318) + on-disk file size. compressedData is emptied during
            // decode, so the on-disk size must come from the file itself, not audioData.
            result.compressionFormat = audioPtr->compressionFormat;
            result.loadType = audioPtr->loadType;
            std::error_code sizeEc;
            const auto diskSize = std::filesystem::file_size(path, sizeEc);
            result.onDiskSizeBytes = sizeEc ? 0 : static_cast<size_t>(diskSize);

            // Retain the decoded PCM; the UI thread buckets/analyzes it directly (VK-1510).
            result.audioData = std::move(audioPtr);

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
        
        const char* channelLayout = (channels == 1) ? "Mono"
                                  : (channels == 2) ? "Stereo"
                                                    : "Multi-channel";
        ImGui::Text("Channels: %u (%s)", channels, channelLayout);

        // VK-1512: advisory hint (softened from the VK-1507 badge) — a multi-channel clip
        // downmixes on a 3D source, so a force-mono reimport is smaller and spatializes
        // cleaner. Advisory only: a stereo music/streaming clip is legitimately stereo.
        // Wrapped because the InfoPanel child is narrow (~120-150px). Same yellow as the badge.
        if (channels > 1)
        {
            const char* who = (channels == 2) ? "Stereo" : "Multi-channel";
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            ImGui::TextWrapped("%s - consider force-mono reimport for 3D use", who);
            ImGui::PopStyleColor();
        }

        // VK-1512: .vfAudio import-info carried by the retained audioData (no header re-read).
        ImGui::Text("Format:");
        ImGui::Text("  %s", formatDisplay(compressionFormat));
        ImGui::Text("Load Type:");
        ImGui::Text("  %s", loadTypeDisplay(loadType));

        ImGui::Text("Sample Rate:");
        ImGui::Text("  %u Hz", sampleRate);
        ImGui::Text("Frames: %u", frames);

        ImGui::Spacing();
        
        // VK-1512: on-disk (.vfAudio file) vs decoded PCM footprint — for a Vorbis clip the
        // former is far smaller, which explains the "Format: Vorbis (lossy)" line above.
        ImGui::Text("On-disk:");
        ImGui::Text("  %zu KB", onDiskSizeBytes / 1024);
        ImGui::Text("Decoded (RAM):");
        ImGui::Text("  %zu KB", dataSizeBytes / 1024);

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
                
                if (isPlaying && !isScrubbing && audioDurationSeconds > 0.0f)
                {
                    events::audio::GetPlaybackPositionQuery posQuery;
                    posQuery.handle = currentAudioHandle;
                    float currentPos = dispatcher.query(posQuery);
                    playbackPosition = currentPos / audioDurationSeconds;
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
                        if (audition3D)
                        {
                            // Real 3D path (non-streaming — the only path SetSoundTransform
                            // can move; streaming handles are a no-op). Distance-LPF, HRTF and
                            // stereo-spatialize all ride this path automatically.
                            const events::audio::ListenerState lis = currentListener();
                            const glm::vec3 offset = math::polarOffsetInBasis(
                                lis.forward, lis.up, auditionDistance, auditionAzimuth, auditionElevation);
                            const glm::vec3 srcPos = lis.position + offset;
                            const glm::vec3 toListener = lis.position - srcPos;

                            events::audio::PlaySound3DCommand playCmd;
                            playCmd.path = audioPath;               // non-streaming full-buffer (path-keyed)
                            playCmd.position = srcPos;
                            playCmd.params.volume = volume;
                            playCmd.params.loop = loopEnabled;
                            playCmd.params.pitch = pitch;
                            playCmd.params.minDistance = auditionMinDistance;
                            playCmd.params.maxDistance = auditionMaxDistance;
                            playCmd.params.enableDistanceFilter = true;   // distance-LPF ON (per Jira)
                            playCmd.params.filterStartDistance = 5.0f;
                            playCmd.params.filterMaxDistance = auditionMaxDistance;
                            playCmd.params.filterIntensity = 1.0f;
                            // Face the listener (immaterial while cones stay omni, robust if narrowed).
                            playCmd.params.direction = (glm::dot(toListener, toListener) > 1e-8f)
                                ? glm::normalize(toListener) : glm::vec3(0.0f, 0.0f, -1.0f);
                            currentAudioHandle = dispatcher.execute(playCmd);
                        }
                        else
                        {
                            events::audio::PlayStreamingSoundCommand playCmd;
                            playCmd.path = audioPath;
                            playCmd.params.volume = volume;
                            playCmd.params.loop = loopEnabled;
                            playCmd.params.pitch = pitch;
                            currentAudioHandle = dispatcher.execute(playCmd);
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
                stopCurrentPlayback();
            }

            ImGui::Checkbox("Loop", &loopEnabled);
            
            ImGui::SliderFloat("##Position", &playbackPosition, 0.0f, 1.0f, "");
            if (ImGui::IsItemActive())
            {
                isScrubbing = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                isScrubbing = false;
                if (currentAudioHandle.isValid() && audioDurationSeconds > 0.0f)
                {
                    events::audio::SetPlaybackPositionCommand seekCmd;
                    seekCmd.handle = currentAudioHandle;
                    seekCmd.seconds = playbackPosition * audioDurationSeconds;
                    dispatcher.execute(seekCmd);
                }
            }
            else if (ImGui::IsItemDeactivated())
            {
                isScrubbing = false;
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

            ImGui::Spacing();

            ImGui::Text("Pitch");
            if (ImGui::SliderFloat("##Pitch", &pitch, 0.5f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
            {
                if (currentAudioHandle.isValid())
                {
                    events::audio::SetSoundPitchCommand pitchCmd;
                    pitchCmd.handle = currentAudioHandle;
                    pitchCmd.pitch = pitch;
                    dispatcher.execute(pitchCmd);
                }
            }

            // VK-1511: 3D audition controls. Distance / azimuth / elevation are LIVE
            // (updateAudition3D re-derives gain/pan each frame from the source position);
            // min/max distance are fixed at source creation, hence "applies on next Play".
            if (audition3D)
            {
                ImGui::Separator();
                ImGui::TextDisabled("3D Audition");

                ImGui::Text("Distance");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##AudDist", &auditionDistance, 0.0f, auditionMaxDistance,
                                   "%.1f m", ImGuiSliderFlags_AlwaysClamp);

                ImGui::Text("Azimuth");
                ImGui::BeginDisabled(autoOrbit);
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##AudAzim", &auditionAzimuth, -180.0f, 180.0f,
                                   "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
                ImGui::EndDisabled();

                ImGui::Text("Elevation");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##AudElev", &auditionElevation, -85.0f, 85.0f,
                                   "%.0f deg", ImGuiSliderFlags_AlwaysClamp);

                ImGui::Checkbox("Auto-orbit", &autoOrbit);
                if (autoOrbit)
                {
                    ImGui::Text("Orbit speed");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("##AudOrbit", &orbitSpeedDegPerSec, 5.0f, 180.0f,
                                       "%.0f d/s", ImGuiSliderFlags_AlwaysClamp);
                }

                if (ImGui::TreeNode("Attenuation (applies on next Play)"))
                {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("##AudMin", &auditionMinDistance, 0.1f, 20.0f,
                                       "min %.1f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("##AudMax", &auditionMaxDistance, 5.0f, 500.0f,
                                       "max %.0f", ImGuiSliderFlags_AlwaysClamp);
                    if (auditionMaxDistance < auditionMinDistance + 1.0f)
                        auditionMaxDistance = auditionMinDistance + 1.0f;
                    if (auditionDistance > auditionMaxDistance)
                        auditionDistance = auditionMaxDistance;
                    ImGui::TreePop();
                }
            }
        }
    }

    void AudioPreviewWindow::stopCurrentPlayback()
    {
        if (currentAudioHandle.isValid())
        {
            events::audio::StopSoundCommand stopCmd;
            stopCmd.handle = currentAudioHandle;
            events::EventDispatcher::instance().execute(stopCmd);
            currentAudioHandle = {};
        }
        isPlaying = false;
        playbackPosition = 0.0f;
    }

    void AudioPreviewWindow::updateAudition3D()
    {
        if (!audition3D || !currentAudioHandle.isValid())
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Only move a source that is actually sounding — also stops us spamming a
        // finished/stopped (non-looping) source with transform commands.
        events::audio::IsSoundPlayingQuery playingQuery;
        playingQuery.handle = currentAudioHandle;
        if (!dispatcher.query(playingQuery))
        {
            return;
        }

        if (autoOrbit)
        {
            const float dt = ImGui::GetIO().DeltaTime;
            auditionAzimuth += orbitSpeedDegPerSec * dt;
            if (auditionAzimuth > 180.0f) auditionAzimuth -= 360.0f;
            else if (auditionAzimuth < -180.0f) auditionAzimuth += 360.0f;
        }

        const events::audio::ListenerState lis = currentListener();
        const glm::vec3 offset = math::polarOffsetInBasis(
            lis.forward, lis.up, auditionDistance, auditionAzimuth, auditionElevation);
        const glm::vec3 srcPos = lis.position + offset;
        const glm::vec3 toListener = lis.position - srcPos;

        events::audio::SetSoundTransformCommand xf;
        xf.handle = currentAudioHandle;
        xf.position = srcPos;
        xf.direction = (glm::dot(toListener, toListener) > 1e-8f)
            ? glm::normalize(toListener) : glm::vec3(0.0f, 0.0f, -1.0f);
        xf.velocity = glm::vec3(0.0f);   // VK-1506 doppler stays additive/zero on this path
        dispatcher.execute(xf);
    }

    void AudioPreviewWindow::drawWaveformPanel()
    {
        ImGui::Text("Waveform");
        ImGui::Separator();

        if (loadFailed || !audioLoaded || !audioData || frames == 0 || channels == 0)
        {
            ImGui::TextDisabled("No waveform data");
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        const float dt = io.DeltaTime;

        const ImVec2 availSize = ImGui::GetContentRegionAvail();
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();

        // Reserve a meter strip on the right and (when open) a spectrum footer below.
        const float meterW = static_cast<float>(channels) * METER_COL_W + static_cast<float>(channels + 1) * 3.0f;
        const float headerH = ImGui::GetFrameHeightWithSpacing();
        const float footer = spectrumOpen ? (SPECTRUM_HEIGHT + headerH) : headerH;
        const float canvasW = std::max(50.0f, availSize.x - meterW - 6.0f);
        const ImVec2 canvasSize(canvasW, std::max(40.0f, availSize.y - footer - 10.0f));

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Background + zero line.
        drawList->AddRectFilled(canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(30, 30, 30, 255));
        const float centerY = canvasPos.y + canvasSize.y * 0.5f;
        drawList->AddLine(ImVec2(canvasPos.x, centerY),
            ImVec2(canvasPos.x + canvasSize.x, centerY), IM_COL32(60, 60, 60, 255));

        // Playhead in whole-file frames (playbackPosition was refreshed by drawInfoPanel this frame).
        currentPlayheadFrame = static_cast<size_t>(std::llround(
            static_cast<double>(playbackPosition) * static_cast<double>(frames)));

        // Guard the view range (init at load; never a zero/inverted/out-of-range span).
        if (viewEndFrame <= viewStartFrame || viewEndFrame > frames)
        {
            viewStartFrame = 0;
            viewEndFrame = frames;
        }

        const ViewMap map{ canvasPos.x, canvasSize.x,
                           static_cast<double>(viewStartFrame),
                           static_cast<double>(viewEndFrame - viewStartFrame) };

        // Re-bucket only when the view range or bucket count changed (steady state = no work).
        const int bucketCount = std::clamp(static_cast<int>(std::floor(canvasSize.x)), 1, MAX_BUCKETS);
        if (viewStartFrame != cachedViewStart || viewEndFrame != cachedViewEnd || bucketCount != cachedBucketCount)
        {
            viewBuckets = resource::computeWaveformBuckets(
                audioData->data, channels, viewStartFrame, viewEndFrame, static_cast<size_t>(bucketCount));
            cachedViewStart = viewStartFrame;
            cachedViewEnd = viewEndFrame;
            cachedBucketCount = bucketCount;
        }

        // Waveform bars.
        const ImU32 waveColor = IM_COL32(100, 180, 255, 255);
        const float halfHeight = canvasSize.y * 0.45f;
        if (!viewBuckets.empty())
        {
            const float xScale = canvasSize.x / static_cast<float>(viewBuckets.size());
            for (size_t i = 0; i < viewBuckets.size(); ++i)
            {
                const auto& pt = viewBuckets[i];
                const float x = canvasPos.x + static_cast<float>(i) * xScale;
                const float y1 = centerY - pt.maxVal * halfHeight;
                const float y2 = centerY - pt.minVal * halfHeight;
                drawList->AddLine(ImVec2(x, y1), ImVec2(x, y2), waveColor);
            }
        }

        // Playhead (drawn only when inside the visible range — may scroll off when zoomed).
        if (playbackPosition > 0.0f || isPlaying)
        {
            const float px = map.frameToX(static_cast<double>(currentPlayheadFrame));
            if (px >= canvasPos.x && px <= canvasPos.x + canvasSize.x)
            {
                drawList->AddLine(ImVec2(px, canvasPos.y),
                    ImVec2(px, canvasPos.y + canvasSize.y), IM_COL32(255, 100, 100, 255), 2.0f);
            }
        }

        // Interaction surface over the waveform canvas only (excludes the meter strip).
        ImGui::SetCursorScreenPos(canvasPos);
        ImGui::InvisibleButton("##WaveformCanvas", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        // Wheel zoom, cursor-anchored (can't zoom out past the full view, nor in past the min span).
        if (hovered && io.MouseWheel != 0.0f)
        {
            const double span = static_cast<double>(viewEndFrame - viewStartFrame);
            const double minSpan = std::min(static_cast<double>(MIN_VIEW_FRAMES), static_cast<double>(frames));
            const double cursorFrame = map.xToFrame(io.MousePos.x);
            const double newSpan = std::clamp(span * std::pow(0.85, static_cast<double>(io.MouseWheel)),
                                              minSpan, static_cast<double>(frames));
            const double frac = (span > 0.0) ? (cursorFrame - static_cast<double>(viewStartFrame)) / span : 0.0;
            double ns = cursorFrame - frac * newSpan;
            double ne = ns + newSpan;
            if (ns < 0.0) { ne -= ns; ns = 0.0; }
            if (ne > static_cast<double>(frames)) { ns -= (ne - static_cast<double>(frames)); ne = static_cast<double>(frames); }
            viewStartFrame = static_cast<size_t>(std::max(0.0, ns));
            viewEndFrame = static_cast<size_t>(std::min(static_cast<double>(frames), ne));
            if (viewEndFrame <= viewStartFrame) { viewStartFrame = 0; viewEndFrame = frames; }
        }

        // Left-drag scrub (unchanged semantics) / middle-drag pan — mutually exclusive.
        if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            isScrubbing = true;
            const double f = std::clamp(map.xToFrame(io.MousePos.x), 0.0, static_cast<double>(frames));
            playbackPosition = (frames > 0) ? static_cast<float>(f / static_cast<double>(frames)) : 0.0f;
        }
        else if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            const double fpp = static_cast<double>(viewEndFrame - viewStartFrame) / static_cast<double>(canvasSize.x);
            const double shift = -static_cast<double>(io.MouseDelta.x) * fpp;
            double ns = static_cast<double>(viewStartFrame) + shift;
            double ne = static_cast<double>(viewEndFrame) + shift;
            if (ns < 0.0) { ne -= ns; ns = 0.0; }
            if (ne > static_cast<double>(frames)) { ns -= (ne - static_cast<double>(frames)); ne = static_cast<double>(frames); }
            viewStartFrame = static_cast<size_t>(std::max(0.0, ns));
            viewEndFrame = static_cast<size_t>(std::min(static_cast<double>(frames), ne));
        }

        // Scrub-on-release: seek only if we were scrubbing (not panning).
        if (ImGui::IsItemDeactivated() && isScrubbing)
        {
            isScrubbing = false;
            if (currentAudioHandle.isValid() && audioDurationSeconds > 0.0f)
            {
                events::audio::SetPlaybackPositionCommand seekCmd;
                seekCmd.handle = currentAudioHandle;
                seekCmd.seconds = playbackPosition * audioDurationSeconds;
                events::EventDispatcher::instance().execute(seekCmd);
            }
        }

        // Level meters: per-channel RMS + peak-hold at the polled playhead.
        if (meterRms.size() != channels) meterRms.assign(channels, 0.0f);
        if (meterPeakHold.size() != channels) meterPeakHold.assign(channels, 0.0f);
        if (isPlaying && !isScrubbing && sampleRate > 0)
        {
            const size_t win = static_cast<size_t>(METER_WINDOW_SEC * static_cast<float>(sampleRate));
            const auto levels = resource::computeWindowLevels(audioData->data, channels, currentPlayheadFrame, win);
            for (uint32_t c = 0; c < channels; ++c)
            {
                const float rms = (c < levels.size()) ? levels[c].rms : 0.0f;
                const float pk = (c < levels.size()) ? levels[c].peak : 0.0f;
                meterRms[c] = rms;
                meterPeakHold[c] = std::max(meterPeakHold[c] - PEAK_DECAY_PER_SEC * dt, pk);
            }
        }
        else
        {
            for (uint32_t c = 0; c < channels; ++c)
            {
                meterRms[c] = std::max(0.0f, meterRms[c] - PEAK_DECAY_PER_SEC * dt);
                meterPeakHold[c] = std::max(0.0f, meterPeakHold[c] - PEAK_DECAY_PER_SEC * dt);
            }
        }
        drawLevelMeters(ImVec2(canvasPos.x + canvasSize.x + 6.0f, canvasPos.y), ImVec2(meterW, canvasSize.y));

        // Spectrum footer (collapsible, default closed; no FFT work while collapsed).
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y + 4.0f));
        spectrumOpen = ImGui::CollapsingHeader("Spectrum");
        if (spectrumOpen)
        {
            const ImVec2 sp = ImGui::GetCursorScreenPos();
            drawSpectrumStrip(sp, canvasSize.x, SPECTRUM_HEIGHT);
            ImGui::Dummy(ImVec2(canvasSize.x, SPECTRUM_HEIGHT));
        }
    }

    void AudioPreviewWindow::drawLevelMeters(ImVec2 pos, ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(24, 24, 24, 255));

        if (channels == 0)
        {
            return;
        }

        const float gap = 3.0f;
        for (uint32_t c = 0; c < channels; ++c)
        {
            const float bx = pos.x + gap + static_cast<float>(c) * (METER_COL_W + gap);
            const float bTop = pos.y + gap;
            const float bBot = pos.y + size.y - gap;
            const float bh = std::max(1.0f, bBot - bTop);

            dl->AddRectFilled(ImVec2(bx, bTop), ImVec2(bx + METER_COL_W, bBot), IM_COL32(40, 40, 40, 255));

            const float rms = (c < meterRms.size()) ? std::clamp(meterRms[c], 0.0f, 1.0f) : 0.0f;
            const float fillH = rms * bh;
            const ImU32 col = (rms < 0.7f) ? IM_COL32(90, 200, 90, 255)
                            : (rms < 0.9f) ? IM_COL32(220, 200, 60, 255)
                                           : IM_COL32(230, 80, 60, 255);
            dl->AddRectFilled(ImVec2(bx, bBot - fillH), ImVec2(bx + METER_COL_W, bBot), col);

            const float pk = (c < meterPeakHold.size()) ? std::clamp(meterPeakHold[c], 0.0f, 1.0f) : 0.0f;
            const float py = bBot - pk * bh;
            dl->AddLine(ImVec2(bx, py), ImVec2(bx + METER_COL_W, py), IM_COL32(240, 240, 240, 255), 2.0f);
        }
    }

    void AudioPreviewWindow::drawSpectrumStrip(ImVec2 pos, float width, float height)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(20, 20, 20, 255));

        // Recompute only while playing; otherwise hold the last bars (frozen when paused).
        if (audioData && isPlaying && !isScrubbing && sampleRate > 0)
        {
            const size_t start = (currentPlayheadFrame > resource::kFftSize / 2)
                                     ? currentPlayheadFrame - resource::kFftSize / 2
                                     : 0;
            const auto mags = resource::computeSpectrum(audioData->data, channels, start, resource::WindowFn::Hann);
            const int barCount = std::clamp(static_cast<int>(width / 4.0f), 8, 128);
            spectrumBars = resource::binSpectrumLog(mags, sampleRate, static_cast<size_t>(barCount));
        }

        if (!spectrumBars.empty())
        {
            const float bw = width / static_cast<float>(spectrumBars.size());
            for (size_t i = 0; i < spectrumBars.size(); ++i)
            {
                const float m = std::clamp(spectrumBars[i], 0.0f, 1.0f);
                const float h = std::sqrt(m) * height; // sqrt lifts small magnitudes for visibility
                const float x0 = pos.x + static_cast<float>(i) * bw;
                dl->AddRectFilled(ImVec2(x0, pos.y + height - h),
                    ImVec2(x0 + std::max(1.0f, bw - 1.0f), pos.y + height), IM_COL32(120, 220, 140, 255));
            }
        }
    }
}
