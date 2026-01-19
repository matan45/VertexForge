#include "AnimationInfoPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/AnimationPreviewEvents.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "print/EditorLogger.hpp"
#include "nfd/FileDialog.hpp"
#include "imgui.h"
#include <filesystem>
#include <cmath>

namespace windows::animation
{
    void AnimationInfoPanel::draw(State& state, const services::PreviewInstanceId& instanceId)
    {
        ImGui::Text("Animation Info");
        ImGui::Separator();

        if (state.loadFailed)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load");
            return;
        }

        if (!state.animationLoaded)
        {
            ImGui::TextDisabled("Loading...");
            return;
        }

        ImGui::Text("Name:");
        ImGui::TextWrapped("  %s", state.animationData->name.c_str());

        ImGui::Spacing();

        float ticksPerSec = state.animationData->ticksPerSecond > 0.0f ? state.animationData->ticksPerSecond : 24.0f;
        float durationSeconds = state.animationData->duration / ticksPerSec;
        ImGui::Text("Duration:");
        ImGui::Text("  %.2f s", durationSeconds);
        ImGui::Text("  %.0f ticks", state.animationData->duration);

        ImGui::Spacing();

        ImGui::Text("Ticks/Second:");
        ImGui::Text("  %.1f", state.animationData->ticksPerSecond);

        ImGui::Spacing();

        ImGui::Text("Channels: %zu", state.animationData->channels.size());

        ImGui::Separator();
        ImGui::Spacing();

        drawMeshFileInput(state, instanceId);

        ImGui::Separator();
        ImGui::Spacing();

        drawPlaybackControls(state, instanceId);
    }

    void AnimationInfoPanel::drawMeshFileInput(State& state, const services::PreviewInstanceId& instanceId)
    {
        ImGui::Text("3D Preview");
        ImGui::Spacing();

        ImGui::Text("Mesh:");
        if (state.meshPath.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No mesh loaded");
        }
        else
        {
            std::filesystem::path path(state.meshPath);
            ImGui::TextWrapped("  %s", path.filename().string().c_str());
        }

        if (ImGui::Button("Select Mesh..."))
        {
            nfd::FileDialog fileDialog;
            std::string selectedPath = fileDialog.openFileDialog(
                {{L"VF Mesh Files (*.vfMesh)", L"*.vfMesh"}});

            if (!selectedPath.empty())
            {
                state.meshPath = selectedPath;

                services::events::animpreview::LoadAnimationPreviewMeshCommand meshCmd;
                meshCmd.instanceId = instanceId;
                meshCmd.meshPath = state.meshPath;
                bool success = events::EventDispatcher::instance().execute(meshCmd);

                if (success)
                {
                    state.meshLoadedInPreview = true;
                    vfLogInfo("Loaded mesh for animation preview: {}", state.meshPath);
                }
                else
                {
                    state.meshLoadedInPreview = false;
                    vfLogError("Failed to load mesh for animation preview: {}", state.meshPath);
                }
            }
        }

        ImGui::Spacing();

        if (state.meshLoadedInPreview && state.animationLoadedInPreview)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Preview: Ready");
        }
        else if (state.meshLoadedInPreview)
        {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Preview: Mesh loaded");
        }
        else
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Preview: Select a mesh");
        }
    }

    void AnimationInfoPanel::drawPlaybackControls(State& state, const services::PreviewInstanceId& instanceId)
    {
        ImGui::Text("Playback");
        ImGui::Separator();

        if (ImGui::Button(state.isPlaying ? "Pause" : "Play", ImVec2(60, 0)))
        {
            state.isPlaying = !state.isPlaying;
            if (state.animationLoadedInPreview)
            {
                if (state.isPlaying)
                {
                    services::events::animpreview::PlayAnimationCommand playCmd;
                    playCmd.instanceId = instanceId;
                    events::EventDispatcher::instance().execute(playCmd);
                }
                else
                {
                    services::events::animpreview::PauseAnimationCommand pauseCmd;
                    pauseCmd.instanceId = instanceId;
                    events::EventDispatcher::instance().execute(pauseCmd);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(60, 0)))
        {
            state.isPlaying = false;

            if (state.animationLoadedInPreview)
            {
                services::events::animpreview::StopAnimationCommand stopCmd;
                stopCmd.instanceId = instanceId;
                events::EventDispatcher::instance().execute(stopCmd);
            }
        }

        ImGui::Spacing();

        ImGui::Text("Speed:");
        if (ImGui::SliderFloat("##Speed", &state.playbackSpeed, 0.1f, 3.0f, "%.1fx"))
        {
            if (state.animationLoadedInPreview)
            {
                services::events::animpreview::SetAnimationPlaybackSpeedCommand speedCmd;
                speedCmd.instanceId = instanceId;
                speedCmd.speed = state.playbackSpeed;
                events::EventDispatcher::instance().execute(speedCmd);
            }
        }

        ImGui::Spacing();

        if (ImGui::Checkbox("Loop", &state.isLooping))
        {
            if (state.animationLoadedInPreview)
            {
                services::events::animpreview::SetAnimationLoopingCommand loopCmd;
                loopCmd.instanceId = instanceId;
                loopCmd.looping = state.isLooping;
                events::EventDispatcher::instance().execute(loopCmd);
            }
        }
    }

    void AnimationInfoPanel::drawLoadingIndicator(const std::string& loadingStatus)
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
}
