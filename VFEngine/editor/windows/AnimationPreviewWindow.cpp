#include "AnimationPreviewWindow.hpp"
#include "imgui.h"
#include "ImSequencer.h"
#include "resource/AnimationResource.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace windows
{
    // ImSequencer adapter implementation
    class AnimationPreviewWindow::AnimationSequence : public ImSequencer::SequenceInterface
    {
    private:
        const resource::AnimationData* animData = nullptr;
        std::vector<int> channelStarts;
        std::vector<int> channelEnds;

    public:
        void setAnimationData(const resource::AnimationData* data)
        {
            animData = data;
            channelStarts.clear();
            channelEnds.clear();

            if (!data) return;

            for (const auto& channel : data->channels)
            {
                float minTime = 0.0f;
                float maxTime = 0.0f;

                for (const auto& key : channel.positionKeys)
                {
                    maxTime = std::max(maxTime, key.time);
                }
                for (const auto& key : channel.rotationKeys)
                {
                    maxTime = std::max(maxTime, key.time);
                }
                for (const auto& key : channel.scalingKeys)
                {
                    maxTime = std::max(maxTime, key.time);
                }

                channelStarts.push_back(static_cast<int>(minTime));
                channelEnds.push_back(static_cast<int>(maxTime));
            }
        }

        int GetFrameMin() const override { return 0; }

        int GetFrameMax() const override
        {
            return animData ? static_cast<int>(animData->duration) : 0;
        }

        int GetItemCount() const override
        {
            return animData ? static_cast<int>(animData->channels.size()) : 0;
        }

        const char* GetItemLabel(int index) const override
        {
            if (!animData || index < 0 || index >= static_cast<int>(animData->channels.size()))
                return "";
            return animData->channels[index].boneName.c_str();
        }

        void Get(int index, int** start, int** end, int* type, unsigned int* color) override
        {
            if (!animData || index < 0 || index >= static_cast<int>(channelStarts.size()))
                return;

            if (start) *start = &channelStarts[index];
            if (end) *end = &channelEnds[index];
            if (type) *type = 0;
            if (color) *color = 0xFF8080AA;
        }
    };

    AnimationPreviewWindow::AnimationPreviewWindow(const std::string& filePath)
        : animationPath(filePath)
        , sequenceAdapter(std::make_unique<AnimationSequence>())
    {
        std::filesystem::path path(filePath);
        windowTitle = "Animation Preview: " + path.filename().string();
    }

    AnimationPreviewWindow::~AnimationPreviewWindow()
    {
        loadingCancelled.store(true);
        if (loadFuture.valid())
        {
            loadFuture.wait();
        }
    }

    void AnimationPreviewWindow::draw()
    {
        if (!isOpen) return;

        if (needsInit)
        {
            startAsyncLoad();
            needsInit = false;
        }

        updateAsyncLoading();

        // Update playback
        float currentImGuiTime = static_cast<float>(ImGui::GetTime());
        float deltaTime = currentImGuiTime - lastFrameTime;
        lastFrameTime = currentImGuiTime;

        if (animationLoaded && isPlaying)
        {
            updatePlayback(deltaTime);
        }

        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                float leftPanelWidth = 200.0f;
                float rightPanelWidth = 220.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                float spacing = ImGui::GetStyle().ItemSpacing.x;

                // Left panel: Info + Playback controls
                ImGui::BeginChild("InfoPanel", ImVec2(leftPanelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                // Middle area: Mesh Preview + Timeline
                float middleWidth = contentSize.x - leftPanelWidth - rightPanelWidth - spacing * 2;
                ImGui::BeginChild("MiddlePanel", ImVec2(middleWidth, contentSize.y), false);

                if (loadingInProgress.load())
                {
                    drawLoadingIndicator();
                }
                else if (animationLoaded)
                {
                    // Mesh preview placeholder takes 60% height
                    float previewHeight = contentSize.y * 0.6f;
                    ImGui::BeginChild("MeshPreviewPanel", ImVec2(middleWidth - 5, previewHeight), true);
                    drawMeshPreviewPlaceholder();
                    ImGui::EndChild();

                    // Timeline takes remaining height
                    ImGui::BeginChild("TimelinePanel", ImVec2(middleWidth - 5, 0), true);
                    drawTimelinePanel();
                    ImGui::EndChild();
                }

                ImGui::EndChild();

                ImGui::SameLine();

                // Right panel: Skeleton hierarchy
                ImGui::BeginChild("SkeletonPanel", ImVec2(rightPanelWidth, contentSize.y), true);
                if (animationLoaded)
                {
                    drawSkeletonPanel();
                }
                else
                {
                    ImGui::TextDisabled("Loading...");
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void AnimationPreviewWindow::startAsyncLoad()
    {
        loadingInProgress.store(true);
        loadingCancelled.store(false);
        loadingStatus = "Loading animation file...";

        loadFuture = std::async(std::launch::async, [this]() {
            return loadAnimationBackground(animationPath);
        });
    }

    void AnimationPreviewWindow::updateAsyncLoading()
    {
        if (!loadingInProgress.load() || !loadFuture.valid())
        {
            return;
        }

        if (loadFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            try
            {
                AnimationLoadResult result = loadFuture.get();

                if (result.success)
                {
                    animationData = std::move(result.animationData);
                    sequenceAdapter->setAnimationData(&animationData);
                    animationLoaded = true;
                    evaluateAnimation(0.0f);
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

    AnimationLoadResult AnimationPreviewWindow::loadAnimationBackground(const std::string& path)
    {
        AnimationLoadResult result;

        try
        {
            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            result.animationData = resource::AnimationResource::loadAnimation(path);

            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

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

    void AnimationPreviewWindow::evaluateAnimation(float timeInTicks)
    {
        evaluatedBones.clear();
        evaluatedBones.reserve(animationData.skeleton.size());

        // Build bone name to channel index map
        std::unordered_map<std::string, size_t> channelMap;
        for (size_t i = 0; i < animationData.channels.size(); ++i)
        {
            channelMap[animationData.channels[i].boneName] = i;
        }

        // Evaluate each bone
        for (size_t i = 0; i < animationData.skeleton.size(); ++i)
        {
            const auto& bone = animationData.skeleton[i];
            EvaluatedBoneTransform eval;
            eval.boneName = bone.name;
            eval.parentIndex = bone.parentIndex;

            auto it = channelMap.find(bone.name);
            if (it != channelMap.end())
            {
                const auto& channel = animationData.channels[it->second];
                eval.position = interpolatePosition(channel, timeInTicks);
                eval.rotation = interpolateRotation(channel, timeInTicks);
                eval.scale = interpolateScale(channel, timeInTicks);
            }

            // Compute local transform
            glm::mat4 T = glm::translate(glm::mat4(1.0f), eval.position);
            glm::mat4 R = glm::mat4_cast(eval.rotation);
            glm::mat4 S = glm::scale(glm::mat4(1.0f), eval.scale);
            eval.localTransform = T * R * S;

            evaluatedBones.push_back(eval);
        }

        computeWorldTransforms();
    }

    glm::vec3 AnimationPreviewWindow::interpolatePosition(const resource::BoneAnimation& channel, float time)
    {
        if (channel.positionKeys.empty()) return glm::vec3(0.0f);
        if (channel.positionKeys.size() == 1) return channel.positionKeys[0].position;

        size_t i = 0;
        for (; i < channel.positionKeys.size() - 1; ++i)
        {
            if (time < channel.positionKeys[i + 1].time) break;
        }

        if (i >= channel.positionKeys.size() - 1)
        {
            return channel.positionKeys.back().position;
        }

        const auto& k0 = channel.positionKeys[i];
        const auto& k1 = channel.positionKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::mix(k0.position, k1.position, t);
    }

    glm::quat AnimationPreviewWindow::interpolateRotation(const resource::BoneAnimation& channel, float time)
    {
        if (channel.rotationKeys.empty())
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (channel.rotationKeys.size() == 1)
            return channel.rotationKeys[0].rotation;

        size_t i = 0;
        for (; i < channel.rotationKeys.size() - 1; ++i)
        {
            if (time < channel.rotationKeys[i + 1].time) break;
        }

        if (i >= channel.rotationKeys.size() - 1)
        {
            return channel.rotationKeys.back().rotation;
        }

        const auto& k0 = channel.rotationKeys[i];
        const auto& k1 = channel.rotationKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::slerp(k0.rotation, k1.rotation, t);
    }

    glm::vec3 AnimationPreviewWindow::interpolateScale(const resource::BoneAnimation& channel, float time)
    {
        if (channel.scalingKeys.empty()) return glm::vec3(1.0f);
        if (channel.scalingKeys.size() == 1) return channel.scalingKeys[0].scale;

        size_t i = 0;
        for (; i < channel.scalingKeys.size() - 1; ++i)
        {
            if (time < channel.scalingKeys[i + 1].time) break;
        }

        if (i >= channel.scalingKeys.size() - 1)
        {
            return channel.scalingKeys.back().scale;
        }

        const auto& k0 = channel.scalingKeys[i];
        const auto& k1 = channel.scalingKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::mix(k0.scale, k1.scale, t);
    }

    void AnimationPreviewWindow::computeWorldTransforms()
    {
        for (size_t i = 0; i < evaluatedBones.size(); ++i)
        {
            auto& bone = evaluatedBones[i];
            if (bone.parentIndex >= 0 &&
                bone.parentIndex < static_cast<int32_t>(evaluatedBones.size()))
            {
                bone.worldTransform =
                    evaluatedBones[bone.parentIndex].worldTransform * bone.localTransform;
            }
            else
            {
                bone.worldTransform = bone.localTransform;
            }
        }
    }

    void AnimationPreviewWindow::drawInfoPanel()
    {
        ImGui::Text("Animation Info");
        ImGui::Separator();

        if (loadFailed)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load");
            return;
        }

        if (!animationLoaded)
        {
            ImGui::TextDisabled("Loading...");
            return;
        }

        ImGui::Text("Name:");
        ImGui::TextWrapped("  %s", animationData.name.c_str());

        ImGui::Spacing();

        float durationSeconds = animationData.duration / animationData.ticksPerSecond;
        ImGui::Text("Duration:");
        ImGui::Text("  %.2f s", durationSeconds);
        ImGui::Text("  %.0f ticks", animationData.duration);

        ImGui::Spacing();

        ImGui::Text("Ticks/Second:");
        ImGui::Text("  %.1f", animationData.ticksPerSecond);

        ImGui::Spacing();

        ImGui::Text("Bones: %zu", animationData.skeleton.size());
        ImGui::Text("Channels: %zu", animationData.channels.size());

        ImGui::Separator();
        ImGui::Spacing();

        drawPlaybackControls();
    }

    void AnimationPreviewWindow::drawPlaybackControls()
    {
        ImGui::Text("Playback");
        ImGui::Separator();

        // Play/Pause/Stop buttons
        if (ImGui::Button(isPlaying ? "Pause" : "Play", ImVec2(60, 0)))
        {
            isPlaying = !isPlaying;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(60, 0)))
        {
            isPlaying = false;
            currentTime = 0.0f;
            currentFrame = 0;
            evaluateAnimation(currentTime);
        }

        ImGui::Spacing();

        // Timeline scrub
        float durationSeconds = animationData.duration / animationData.ticksPerSecond;
        float currentSeconds = currentTime / animationData.ticksPerSecond;

        ImGui::Text("Time:");
        if (ImGui::SliderFloat("##Time", &currentSeconds, 0.0f, durationSeconds, "%.2f s"))
        {
            seekToTime(currentSeconds * animationData.ticksPerSecond);
        }

        // Frame display
        ImGui::Text("Frame: %d / %d", currentFrame, static_cast<int>(animationData.duration));

        ImGui::Spacing();

        // Speed control
        ImGui::Text("Speed:");
        ImGui::SliderFloat("##Speed", &playbackSpeed, 0.1f, 3.0f, "%.1fx");

        ImGui::Spacing();

        // Loop toggle
        ImGui::Checkbox("Loop", &isLooping);
    }

    void AnimationPreviewWindow::drawMeshPreviewPlaceholder()
    {
        ImGui::Text("3D Preview");
        ImGui::Separator();

        ImVec2 availSize = ImGui::GetContentRegionAvail();
        ImVec2 windowPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Draw dark background
        drawList->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y),
            IM_COL32(25, 25, 30, 255)
        );

        // Draw grid pattern
        float gridSpacing = 30.0f;
        ImU32 gridColor = IM_COL32(50, 50, 55, 255);

        for (float x = windowPos.x; x < windowPos.x + availSize.x; x += gridSpacing)
        {
            drawList->AddLine(
                ImVec2(x, windowPos.y),
                ImVec2(x, windowPos.y + availSize.y),
                gridColor
            );
        }
        for (float y = windowPos.y; y < windowPos.y + availSize.y; y += gridSpacing)
        {
            drawList->AddLine(
                ImVec2(windowPos.x, y),
                ImVec2(windowPos.x + availSize.x, y),
                gridColor
            );
        }

        // Draw placeholder text in center
        const char* placeholderText = "Skeletal Mesh Preview";
        const char* subText = "(Requires VK-128: GPU Skinning)";

        ImVec2 textSize = ImGui::CalcTextSize(placeholderText);
        ImVec2 subTextSize = ImGui::CalcTextSize(subText);

        float centerX = windowPos.x + availSize.x * 0.5f;
        float centerY = windowPos.y + availSize.y * 0.5f;

        // Draw placeholder icon (bone symbol)
        float iconSize = 40.0f;
        ImVec2 iconCenter(centerX, centerY - 30.0f);
        ImU32 iconColor = IM_COL32(100, 100, 120, 200);

        // Simple bone icon using lines
        drawList->AddCircleFilled(ImVec2(iconCenter.x, iconCenter.y - iconSize * 0.4f), 8.0f, iconColor);
        drawList->AddCircleFilled(ImVec2(iconCenter.x, iconCenter.y + iconSize * 0.4f), 8.0f, iconColor);
        drawList->AddLine(
            ImVec2(iconCenter.x, iconCenter.y - iconSize * 0.35f),
            ImVec2(iconCenter.x, iconCenter.y + iconSize * 0.35f),
            iconColor, 4.0f
        );

        // Draw text
        ImVec2 textPos(centerX - textSize.x * 0.5f, centerY + 20.0f);
        ImVec2 subTextPos(centerX - subTextSize.x * 0.5f, centerY + 45.0f);

        drawList->AddText(textPos, IM_COL32(180, 180, 180, 255), placeholderText);
        drawList->AddText(subTextPos, IM_COL32(120, 120, 130, 255), subText);

        // Consume the space
        ImGui::Dummy(availSize);
    }

    void AnimationPreviewWindow::drawTimelinePanel()
    {
        ImGui::Text("Timeline");
        ImGui::Separator();

        if (!animationLoaded)
        {
            ImGui::TextDisabled("No animation loaded");
            return;
        }

        // ImSequencer
        int sequenceOptions = ImSequencer::SEQUENCER_CHANGE_FRAME;

        if (ImSequencer::Sequencer(sequenceAdapter.get(), &currentFrame, &sequencerExpanded,
                                   &selectedChannel, &firstFrame, sequenceOptions))
        {
            // Frame changed via sequencer click
            seekToTime(frameToTime(currentFrame));
        }
    }

    void AnimationPreviewWindow::drawSkeletonPanel()
    {
        ImGui::Text("Skeleton Hierarchy");
        ImGui::Separator();

        if (evaluatedBones.empty())
        {
            ImGui::TextDisabled("No bones evaluated");
            return;
        }

        // Draw root bones (parentIndex == -1)
        for (size_t i = 0; i < evaluatedBones.size(); ++i)
        {
            if (evaluatedBones[i].parentIndex == -1)
            {
                drawBoneNode(i);
            }
        }
    }

    void AnimationPreviewWindow::drawBoneNode(size_t index)
    {
        const auto& bone = evaluatedBones[index];

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selectedChannel == static_cast<int>(index))
            flags |= ImGuiTreeNodeFlags_Selected;

        // Check if bone has children
        bool hasChildren = false;
        for (const auto& other : evaluatedBones)
        {
            if (other.parentIndex == static_cast<int32_t>(index))
            {
                hasChildren = true;
                break;
            }
        }
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

        bool nodeOpen = ImGui::TreeNodeEx(bone.boneName.c_str(), flags);

        if (ImGui::IsItemClicked())
            selectedChannel = static_cast<int>(index);

        // Tooltip with transform info
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Position: %.2f, %.2f, %.2f",
                        bone.position.x, bone.position.y, bone.position.z);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(bone.rotation));
            ImGui::Text("Rotation: %.1f, %.1f, %.1f", euler.x, euler.y, euler.z);
            ImGui::Text("Scale: %.2f, %.2f, %.2f",
                        bone.scale.x, bone.scale.y, bone.scale.z);
            ImGui::EndTooltip();
        }

        if (nodeOpen)
        {
            // Draw children
            for (size_t i = 0; i < evaluatedBones.size(); ++i)
            {
                if (evaluatedBones[i].parentIndex == static_cast<int32_t>(index))
                {
                    drawBoneNode(i);
                }
            }
            ImGui::TreePop();
        }
    }

    void AnimationPreviewWindow::drawLoadingIndicator()
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

    void AnimationPreviewWindow::updatePlayback(float deltaTime)
    {
        currentTime += deltaTime * animationData.ticksPerSecond * playbackSpeed;

        if (currentTime >= animationData.duration)
        {
            if (isLooping)
            {
                currentTime = fmod(currentTime, animationData.duration);
            }
            else
            {
                currentTime = animationData.duration;
                isPlaying = false;
            }
        }

        currentFrame = timeToFrame(currentTime);
        evaluateAnimation(currentTime);
    }

    void AnimationPreviewWindow::seekToTime(float timeInTicks)
    {
        currentTime = glm::clamp(timeInTicks, 0.0f, animationData.duration);
        currentFrame = timeToFrame(currentTime);
        evaluateAnimation(currentTime);
    }

    int AnimationPreviewWindow::timeToFrame(float timeInTicks) const
    {
        return static_cast<int>(timeInTicks);
    }

    float AnimationPreviewWindow::frameToTime(int frame) const
    {
        return static_cast<float>(frame);
    }
}
