#include "AnimationPreviewWindow.hpp"
#include "../camera/OrbitCamera.hpp"
#include "imgui.h"
#include "ImSequencer.h"
#include "resource/AnimationResource.hpp"
#include "events/EventDispatcher.hpp"
#include "events/AnimationPreviewEvents.hpp"
#include "print/EditorLogger.hpp"
#include "nfd/FileDialog.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

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
        , camera(std::make_unique<editor::OrbitCamera>())
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
        cleanUpPreviewRenderer();
    }

    void AnimationPreviewWindow::draw()
    {
        // Handle cleanup when window is closing
        if (!isOpen)
        {
            if (!previewCleanedUp)
            {
                cleanUpPreviewRenderer();
            }
            return;
        }

        if (needsInit)
        {
            startAsyncLoad();
            initPreviewRenderer();
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

        ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                float leftPanelWidth = 220.0f;
                float rightPanelWidth = 220.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                float spacing = ImGui::GetStyle().ItemSpacing.x;

                // Left panel: Info + Playback controls
                ImGui::BeginChild("InfoPanel", ImVec2(leftPanelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                // Middle area: 3D Preview + Timeline
                float middleWidth = contentSize.x - leftPanelWidth - rightPanelWidth - spacing * 2;
                ImGui::BeginChild("MiddlePanel", ImVec2(middleWidth, contentSize.y), false);

                if (loadingInProgress.load())
                {
                    drawLoadingIndicator();
                }
                else if (animationLoaded)
                {
                    // 3D viewport takes 60% height
                    float previewHeight = contentSize.y * 0.6f;
                    ImGui::BeginChild("3DViewportPanel", ImVec2(middleWidth - 5, previewHeight), true,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                    draw3DViewport(viewportSize.x, viewportSize.y);
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

    void AnimationPreviewWindow::initPreviewRenderer()
    {
        services::events::animpreview::InitAnimationPreviewCommand initCmd;
        initCmd.instanceId = getPreviewInstanceId();
        events::EventDispatcher::instance().execute(initCmd);
        previewInitialized = true;
    }

    void AnimationPreviewWindow::cleanUpPreviewRenderer()
    {
        if (previewCleanedUp) return;

        if (previewInitialized)
        {
            services::events::animpreview::CleanUpAnimationPreviewCommand cleanupCmd;
            cleanupCmd.instanceId = getPreviewInstanceId();
            events::EventDispatcher::instance().execute(cleanupCmd);
        }

        previewCleanedUp = true;
    }

    void AnimationPreviewWindow::loadMeshForPreview()
    {
        if (meshPath.empty() || !previewInitialized) return;

        services::events::animpreview::LoadAnimationPreviewMeshCommand loadCmd;
        loadCmd.instanceId = getPreviewInstanceId();
        loadCmd.meshPath = meshPath;
        bool success = events::EventDispatcher::instance().execute(loadCmd);

        if (success)
        {
            meshLoadedInPreview = true;

            // Get mesh bounds and fit camera
            services::events::animpreview::GetAnimationPreviewMeshBoundsQuery boundsQuery;
            boundsQuery.instanceId = getPreviewInstanceId();
            meshBounds = events::EventDispatcher::instance().query(boundsQuery);
            camera->fitToBounds(meshBounds);
        }
        else
        {
            vfLogError("Failed to load mesh for animation preview: {}", meshPath);
        }
    }

    void AnimationPreviewWindow::loadAnimationForPreview()
    {
        if (!meshLoadedInPreview || !previewInitialized) return;

        services::events::animpreview::LoadAnimationPreviewAnimationCommand loadCmd;
        loadCmd.instanceId = getPreviewInstanceId();
        loadCmd.animationPath = animationPath;
        bool success = events::EventDispatcher::instance().execute(loadCmd);

        if (success)
        {
            animationLoadedInPreview = true;

            // Sync looping state
            services::events::animpreview::SetAnimationLoopingCommand loopCmd;
            loopCmd.instanceId = getPreviewInstanceId();
            loopCmd.looping = isLooping;
            events::EventDispatcher::instance().execute(loopCmd);

            // Sync playback speed
            services::events::animpreview::SetAnimationPlaybackSpeedCommand speedCmd;
            speedCmd.instanceId = getPreviewInstanceId();
            speedCmd.speed = playbackSpeed;
            events::EventDispatcher::instance().execute(speedCmd);
        }
        else
        {
            vfLogError("Failed to load animation for preview: {}", animationPath);
        }
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

                    // Validate animation data has meaningful content
                    bool isValid = animationData.duration > 0.0f &&
                                   !animationData.channels.empty();

                    if (isValid)
                    {
                        // Build bone name to channel index map once
                        boneNameToChannelIndex.clear();
                        for (size_t i = 0; i < animationData.channels.size(); ++i)
                        {
                            boneNameToChannelIndex[animationData.channels[i].boneName] = i;
                        }

                        // Build bone children map once for O(1) hierarchy lookup
                        boneChildrenMap.clear();
                        for (size_t i = 0; i < animationData.skeleton.size(); ++i)
                        {
                            int32_t parentIdx = animationData.skeleton[i].parentIndex;
                            boneChildrenMap[parentIdx].push_back(i);
                        }

                        sequenceAdapter->setAnimationData(&animationData);
                        animationLoaded = true;
                        evaluateAnimationLocal(0.0f);
                    }
                    else
                    {
                        loadFailed = true;
                    }
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

    void AnimationPreviewWindow::evaluateAnimationLocal(float timeInTicks)
    {
        evaluatedBones.clear();
        evaluatedBones.reserve(animationData.skeleton.size());

        // Evaluate each bone using cached channel map
        for (size_t i = 0; i < animationData.skeleton.size(); ++i)
        {
            const auto& bone = animationData.skeleton[i];
            EvaluatedBoneTransform eval;
            eval.boneName = bone.name;
            eval.parentIndex = bone.parentIndex;

            auto it = boneNameToChannelIndex.find(bone.name);
            if (it != boneNameToChannelIndex.end())
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

        float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
        float durationSeconds = animationData.duration / ticksPerSec;
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

        drawMeshFileInput();

        ImGui::Separator();
        ImGui::Spacing();

        drawPlaybackControls();
    }

    void AnimationPreviewWindow::drawMeshFileInput()
    {
        ImGui::Text("3D Preview");
        ImGui::Spacing();

        // Display current mesh path
        ImGui::Text("Mesh File:");
        if (!meshPath.empty())
        {
            std::filesystem::path p(meshPath);
            ImGui::TextWrapped("%s", p.filename().string().c_str());
        }
        else
        {
            ImGui::TextDisabled("No mesh selected");
        }

        ImGui::Spacing();

        if (ImGui::Button("Select Mesh...", ImVec2(-1, 0)))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Mesh Files (*.vfmesh)", L"*.vfmesh"}});

            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    meshPath = path;
                    loadMeshForPreview();
                    if (meshLoadedInPreview && animationLoaded)
                    {
                        loadAnimationForPreview();
                    }
                }
                else
                {
                    vfLogError("Selected mesh file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::Spacing();

        // Status indicators
        if (meshLoadedInPreview)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Mesh: Loaded");
        }
        else
        {
            ImGui::TextDisabled("Mesh: Not loaded");
        }

        if (animationLoadedInPreview)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Anim: Loaded");
        }
        else if (meshLoadedInPreview)
        {
            ImGui::TextDisabled("Anim: Not loaded");
        }
    }

    void AnimationPreviewWindow::drawPlaybackControls()
    {
        ImGui::Text("Playback");
        ImGui::Separator();

        // Play/Pause/Stop buttons
        if (ImGui::Button(isPlaying ? "Pause" : "Play", ImVec2(60, 0)))
        {
            isPlaying = !isPlaying;
            if (animationLoadedInPreview)
            {
                if (isPlaying)
                {
                    services::events::animpreview::PlayAnimationCommand playCmd;
                    playCmd.instanceId = getPreviewInstanceId();
                    events::EventDispatcher::instance().execute(playCmd);
                }
                else
                {
                    services::events::animpreview::PauseAnimationCommand pauseCmd;
                    pauseCmd.instanceId = getPreviewInstanceId();
                    events::EventDispatcher::instance().execute(pauseCmd);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(60, 0)))
        {
            isPlaying = false;
            currentFrame = 0;

            if (animationLoadedInPreview)
            {
                services::events::animpreview::StopAnimationCommand stopCmd;
                stopCmd.instanceId = getPreviewInstanceId();
                events::EventDispatcher::instance().execute(stopCmd);
            }

            evaluateAnimationLocal(0.0f);
        }

        ImGui::Spacing();

        // Timeline scrub
        float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
        float durationSeconds = animationData.duration / ticksPerSec;

        // Get current time from service if animation is loaded
        float currentTimeInTicks = 0.0f;
        if (animationLoadedInPreview)
        {
            services::events::animpreview::GetAnimationPlaybackTimeQuery timeQuery;
            timeQuery.instanceId = getPreviewInstanceId();
            currentTimeInTicks = events::EventDispatcher::instance().query(timeQuery) * ticksPerSec;
        }
        else
        {
            currentTimeInTicks = static_cast<float>(currentFrame);
        }

        float currentSeconds = currentTimeInTicks / ticksPerSec;

        ImGui::Text("Time:");
        if (ImGui::SliderFloat("##Time", &currentSeconds, 0.0f, durationSeconds, "%.2f s"))
        {
            seekToTime(currentSeconds * ticksPerSec);
        }

        // Frame display
        ImGui::Text("Frame: %d / %d", currentFrame, static_cast<int>(animationData.duration));

        ImGui::Spacing();

        // Speed control
        ImGui::Text("Speed:");
        if (ImGui::SliderFloat("##Speed", &playbackSpeed, 0.1f, 3.0f, "%.1fx"))
        {
            if (animationLoadedInPreview)
            {
                services::events::animpreview::SetAnimationPlaybackSpeedCommand speedCmd;
                speedCmd.instanceId = getPreviewInstanceId();
                speedCmd.speed = playbackSpeed;
                events::EventDispatcher::instance().execute(speedCmd);
            }
        }

        ImGui::Spacing();

        // Loop toggle
        if (ImGui::Checkbox("Loop", &isLooping))
        {
            if (animationLoadedInPreview)
            {
                services::events::animpreview::SetAnimationLoopingCommand loopCmd;
                loopCmd.instanceId = getPreviewInstanceId();
                loopCmd.looping = isLooping;
                events::EventDispatcher::instance().execute(loopCmd);
            }
        }
    }

    void AnimationPreviewWindow::handlePreviewInput()
    {
        bool isHovered = ImGui::IsWindowHovered();

        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            isDraggingPreview = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            isDraggingPreview = false;
        }

        if (!isHovered) return;

        ImGuiIO& io = ImGui::GetIO();

        // Scroll to zoom
        if (io.MouseWheel != 0.0f)
        {
            float zoomFactor = 1.0f - io.MouseWheel * camera->zoomSensitivity * 0.1f;
            camera->setDistance(camera->distance * zoomFactor);
            camera->updateMatrices();
        }

        // Left mouse drag to orbit
        if (isDraggingPreview && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 delta = io.MouseDelta;

            if (delta.x != 0.0f || delta.y != 0.0f)
            {
                camera->yaw += delta.x * camera->orbitSensitivity;
                camera->pitch -= delta.y * camera->orbitSensitivity;
                camera->pitch = glm::clamp(camera->pitch, -89.0f, 89.0f);
                camera->updateMatrices();
            }
        }
    }

    void AnimationPreviewWindow::draw3DViewport(float width, float height)
    {
        ImGui::Text("3D Preview");
        ImGui::Separator();

        ImVec2 availSize = ImGui::GetContentRegionAvail();

        // If mesh not loaded, show placeholder
        if (!meshLoadedInPreview || !animationLoadedInPreview)
        {
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
            const char* placeholderText = "Load a mesh file to preview";
            const char* subText = "Enter path in 'Mesh File' field";

            ImVec2 textSize = ImGui::CalcTextSize(placeholderText);
            ImVec2 subTextSize = ImGui::CalcTextSize(subText);

            float centerX = windowPos.x + availSize.x * 0.5f;
            float centerY = windowPos.y + availSize.y * 0.5f;

            ImVec2 textPos(centerX - textSize.x * 0.5f, centerY - 10.0f);
            ImVec2 subTextPos(centerX - subTextSize.x * 0.5f, centerY + 15.0f);

            drawList->AddText(textPos, IM_COL32(180, 180, 180, 255), placeholderText);
            drawList->AddText(subTextPos, IM_COL32(120, 120, 130, 255), subText);

            ImGui::Dummy(availSize);
            return;
        }

        // Render actual 3D preview
        if (availSize.x <= 0 || availSize.y <= 0) return;

        camera->setAspectRatio(availSize.x / availSize.y);
        handlePreviewInput();

        // Update animation if playing
        if (isPlaying)
        {
            float deltaTime = static_cast<float>(ImGui::GetIO().DeltaTime);
            services::events::animpreview::UpdateAnimationPreviewCommand updateCmd;
            updateCmd.instanceId = getPreviewInstanceId();
            updateCmd.deltaTime = deltaTime;
            events::EventDispatcher::instance().execute(updateCmd);
        }

        // Set preview params
        glm::mat4 model = glm::mat4(1.0f);
        services::AnimationPreviewParams params;
        params.modelMatrix = model;
        params.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        params.metallic = 0.0f;
        params.roughness = 0.5f;

        services::events::animpreview::SetAnimationPreviewParamsCommand paramsCmd;
        paramsCmd.instanceId = getPreviewInstanceId();
        paramsCmd.params = params;
        events::EventDispatcher::instance().execute(paramsCmd);

        // Update camera
        services::events::animpreview::UpdateAnimationCameraCommand cameraCmd;
        cameraCmd.instanceId = getPreviewInstanceId();
        cameraCmd.view = camera->getViewMatrix();
        cameraCmd.projection = camera->getProjectionMatrix();
        cameraCmd.cameraPos = camera->getPosition();
        events::EventDispatcher::instance().execute(cameraCmd);

        // Render
        services::events::animpreview::RenderAnimationPreviewQuery renderQuery;
        renderQuery.instanceId = getPreviewInstanceId();
        auto textureHandle = events::EventDispatcher::instance().query(renderQuery);

        if (textureHandle.imguiDescriptorSet)
        {
            ImVec2 viewportPos = ImGui::GetCursorScreenPos();
            ImGui::Image(textureHandle.imguiDescriptorSet, availSize);

            // Draw bone visualization overlay
            if (showBoneVisualization && !evaluatedBones.empty())
            {
                drawBoneVisualization(viewportPos, availSize);
            }
        }
        else
        {
            ImGui::Dummy(availSize);
        }
    }

    ImVec2 AnimationPreviewWindow::worldToScreen(const glm::vec3& worldPos, const ImVec2& viewportPos,
                                                   const ImVec2& viewportSize) const
    {
        // Transform world position to clip space
        glm::vec4 clipPos = camera->getProjectionMatrix() * camera->getViewMatrix() * glm::vec4(worldPos, 1.0f);

        // Perspective divide
        if (std::abs(clipPos.w) < 0.0001f)
        {
            return ImVec2(-10000, -10000);  // Off-screen
        }

        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

        // Convert from NDC [-1,1] to screen coordinates
        float screenX = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        float screenY = viewportPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;  // Flip Y

        return ImVec2(screenX, screenY);
    }

    void AnimationPreviewWindow::drawBoneVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Colors
        ImU32 boneColor = IM_COL32(255, 255, 0, 255);       // Yellow for bones
        ImU32 jointColor = IM_COL32(255, 100, 100, 255);    // Red for joints
        ImU32 selectedColor = IM_COL32(0, 255, 255, 255);   // Cyan for selected

        // Draw each bone as a line from parent to child
        for (size_t i = 0; i < evaluatedBones.size(); ++i)
        {
            const auto& bone = evaluatedBones[i];

            // Get bone world position (translation from world transform)
            glm::vec3 boneWorldPos(bone.worldTransform[3][0], bone.worldTransform[3][1], bone.worldTransform[3][2]);

            // Project to screen
            ImVec2 screenPos = worldToScreen(boneWorldPos, viewportPos, viewportSize);

            // Check if on screen
            if (screenPos.x < viewportPos.x - 100 || screenPos.x > viewportPos.x + viewportSize.x + 100 ||
                screenPos.y < viewportPos.y - 100 || screenPos.y > viewportPos.y + viewportSize.y + 100)
            {
                continue;
            }

            // Draw line to parent
            if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(evaluatedBones.size()))
            {
                const auto& parentBone = evaluatedBones[bone.parentIndex];
                glm::vec3 parentWorldPos(parentBone.worldTransform[3][0], parentBone.worldTransform[3][1],
                                         parentBone.worldTransform[3][2]);

                ImVec2 parentScreenPos = worldToScreen(parentWorldPos, viewportPos, viewportSize);

                ImU32 lineColor = (static_cast<int>(i) == selectedChannel) ? selectedColor : boneColor;
                drawList->AddLine(parentScreenPos, screenPos, lineColor, 2.0f);
            }

            // Draw joint circle
            float jointRadius = (static_cast<int>(i) == selectedChannel) ? 6.0f : 4.0f;
            ImU32 circleColor = (static_cast<int>(i) == selectedChannel) ? selectedColor : jointColor;
            drawList->AddCircleFilled(screenPos, jointRadius, circleColor);

            // Draw bone index for first 10 bones
            if (i < 10)
            {
                char label[8];
                snprintf(label, sizeof(label), "%zu", i);
                drawList->AddText(ImVec2(screenPos.x + 8, screenPos.y - 8), IM_COL32(255, 255, 255, 255), label);
            }
        }
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

        // Debug visualization toggle
        ImGui::Checkbox("Show Bones", &showBoneVisualization);
        ImGui::Separator();

        // Camera controls
        if (meshLoadedInPreview && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float itemWidth = ImGui::GetContentRegionAvail().x - 50.0f;

            float dist = camera->getDistance();
            float minDist = camera->getMinDistance();
            float maxDist = camera->getMaxDistance();

            ImGui::Text("Zoom");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            if (ImGui::SliderFloat("##Zoom", &dist, minDist, maxDist, "%.1f", ImGuiSliderFlags_Logarithmic))
            {
                camera->setDistance(dist);
            }

            if (ImGui::Button("Fit to Mesh", ImVec2(-1, 0)))
            {
                camera->fitToBounds(meshBounds);
            }

            ImGui::Separator();
        }

        ImGui::Text("Bones");
        ImGui::Separator();

        // Draw root bones (parentIndex == -1) using precomputed map
        auto rootIt = boneChildrenMap.find(-1);
        if (rootIt != boneChildrenMap.end())
        {
            for (size_t idx : rootIt->second)
            {
                drawBoneNode(idx);
            }
        }
    }

    void AnimationPreviewWindow::drawBoneNode(size_t index)
    {
        const auto& bone = evaluatedBones[index];

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selectedChannel == static_cast<int>(index))
            flags |= ImGuiTreeNodeFlags_Selected;

        // Check if bone has children using precomputed map (O(1) lookup)
        auto childIt = boneChildrenMap.find(static_cast<int32_t>(index));
        bool hasChildren = (childIt != boneChildrenMap.end() && !childIt->second.empty());
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
            // Draw children using precomputed map (O(1) lookup)
            if (hasChildren)
            {
                for (size_t childIdx : childIt->second)
                {
                    drawBoneNode(childIdx);
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
        // Get current time from service if animation is loaded in preview
        if (animationLoadedInPreview)
        {
            services::events::animpreview::GetAnimationPlaybackTimeQuery timeQuery;
            timeQuery.instanceId = getPreviewInstanceId();
            float currentTimeSeconds = events::EventDispatcher::instance().query(timeQuery);

            float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
            float currentTimeInTicks = currentTimeSeconds * ticksPerSec;

            currentFrame = timeToFrame(currentTimeInTicks);
            evaluateAnimationLocal(currentTimeInTicks);

            // Check if animation finished (for non-looping)
            services::events::animpreview::IsAnimationPlayingQuery playingQuery;
            playingQuery.instanceId = getPreviewInstanceId();
            isPlaying = events::EventDispatcher::instance().query(playingQuery);
        }
        else
        {
            // Local playback for timeline only
            float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
            float currentTimeInTicks = static_cast<float>(currentFrame);

            currentTimeInTicks += deltaTime * ticksPerSec * playbackSpeed;

            if (currentTimeInTicks >= animationData.duration)
            {
                if (isLooping)
                {
                    currentTimeInTicks = fmod(currentTimeInTicks, animationData.duration);
                }
                else
                {
                    currentTimeInTicks = animationData.duration;
                    isPlaying = false;
                }
            }

            currentFrame = timeToFrame(currentTimeInTicks);
            evaluateAnimationLocal(currentTimeInTicks);
        }
    }

    void AnimationPreviewWindow::seekToTime(float timeInTicks)
    {
        float clampedTime = glm::clamp(timeInTicks, 0.0f, animationData.duration);
        currentFrame = timeToFrame(clampedTime);
        evaluateAnimationLocal(clampedTime);

        // Sync to service if preview is active
        if (animationLoadedInPreview)
        {
            float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
            float timeSeconds = clampedTime / ticksPerSec;

            services::events::animpreview::SetAnimationPlaybackTimeCommand timeCmd;
            timeCmd.instanceId = getPreviewInstanceId();
            timeCmd.timeSeconds = timeSeconds;
            events::EventDispatcher::instance().execute(timeCmd);
        }
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
