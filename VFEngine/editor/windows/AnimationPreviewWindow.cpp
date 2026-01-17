#include "AnimationPreviewWindow.hpp"
#include "../camera/OrbitCamera.hpp"
#include "imgui.h"
#include "ImSequencer.h"
#include "resource/ResourceManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/AnimationPreviewEvents.hpp"
#include "print/EditorLogger.hpp"
#include "nfd/FileDialog.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace windows
{
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
        if (loadFuture.valid())
        {
            loadFuture.wait();
        }
        cleanUpPreviewRenderer();
    }

    void AnimationPreviewWindow::draw()
    {
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

                ImGui::BeginChild("InfoPanel", ImVec2(leftPanelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                float middleWidth = contentSize.x - leftPanelWidth - rightPanelWidth - spacing * 2;
                ImGui::BeginChild("MiddlePanel", ImVec2(middleWidth, contentSize.y), false);

                if (loadingInProgress.load())
                {
                    drawLoadingIndicator();
                }
                else if (animationLoaded)
                {
                    float previewHeight = contentSize.y * 0.6f;
                    ImGui::BeginChild("3DViewportPanel", ImVec2(middleWidth - 5, previewHeight), true,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                    draw3DViewport(viewportSize.x, viewportSize.y);
                    ImGui::EndChild();

                    ImGui::BeginChild("TimelinePanel", ImVec2(middleWidth - 5, 0), true);
                    drawTimelinePanel();
                    ImGui::EndChild();
                }

                ImGui::EndChild();

                ImGui::SameLine();

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

    void AnimationPreviewWindow::loadAnimationForPreview()
    {
        if (!previewInitialized) return;

        services::events::animpreview::LoadAnimationPreviewAnimationCommand loadCmd;
        loadCmd.instanceId = getPreviewInstanceId();
        loadCmd.animationPath = animationPath;
        bool success = events::EventDispatcher::instance().execute(loadCmd);

        if (success)
        {
            animationLoadedInPreview = true;

            services::events::animpreview::SetAnimationLoopingCommand loopCmd;
            loopCmd.instanceId = getPreviewInstanceId();
            loopCmd.looping = isLooping;
            events::EventDispatcher::instance().execute(loopCmd);

            services::events::animpreview::SetAnimationPlaybackSpeedCommand speedCmd;
            speedCmd.instanceId = getPreviewInstanceId();
            speedCmd.speed = playbackSpeed;
            events::EventDispatcher::instance().execute(speedCmd);

            // Update bone hierarchy from loaded skeleton
            updateBoneTransformsFromService();
            buildBoneHierarchyMaps();
        }
        else
        {
            vfLogError("Failed to load animation for preview: {}", animationPath);
        }
    }

    void AnimationPreviewWindow::tryAutoLoadMesh()
    {
        // No auto-load - user must select a mesh file manually
        // This method is now a no-op since animations no longer contain embedded meshes
    }

    void AnimationPreviewWindow::startAsyncLoad()
    {
        loadingInProgress.store(true);
        loadingStatus = "Loading animation file...";

        loadFuture = resource::ResourceManager::loadAnimationAsync(animationPath);
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
                auto loadedData = loadFuture.get();

                if (loadedData)
                {
                    animationData = *loadedData;

                    bool isValid = animationData.duration > 0.0f &&
                        !animationData.channels.empty();

                    if (isValid)
                    {
                        sequenceAdapter->setAnimationData(&animationData);
                        animationLoaded = true;
                        updateBoneTransformsFromService();
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

    void AnimationPreviewWindow::updateBoneTransformsFromService()
    {
        services::events::animpreview::GetAnimationPreviewEvaluatedBonesQuery query;
        query.instanceId = getPreviewInstanceId();
        evaluatedBones = events::EventDispatcher::instance().query(query);

        if (!evaluatedBones.empty())
        {
            buildBoneHierarchyMaps();
        }
    }

    void AnimationPreviewWindow::buildBoneHierarchyMaps()
    {
        boneNameToIndex.clear();
        boneChildrenMap.clear();

        for (size_t i = 0; i < evaluatedBones.size(); ++i)
        {
            const auto& bone = evaluatedBones[i];
            boneNameToIndex[bone.name] = i;
            boneChildrenMap[bone.parentIndex].push_back(i);
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

        // Mesh selection
        ImGui::Text("Mesh:");
        if (meshPath.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No mesh loaded");
        }
        else
        {
            std::filesystem::path path(meshPath);
            ImGui::TextWrapped("  %s", path.filename().string().c_str());
        }

        if (ImGui::Button("Select Mesh..."))
        {
            nfd::FileDialog fileDialog;
            std::string selectedPath = fileDialog.openFileDialog(
                {{L"VF Mesh Files (*.vfMesh)", L"*.vfMesh"}});

            if (!selectedPath.empty())
            {
                meshPath = selectedPath;

                // Load mesh into preview
                services::events::animpreview::LoadAnimationPreviewMeshCommand meshCmd;
                meshCmd.instanceId = getPreviewInstanceId();
                meshCmd.meshPath = meshPath;
                bool success = events::EventDispatcher::instance().execute(meshCmd);

                if (success)
                {
                    meshLoadedInPreview = true;
                    vfLogInfo("Loaded mesh for animation preview: {}", meshPath);

                    // Load animation if already available
                    if (animationLoaded && !animationLoadedInPreview)
                    {
                        loadAnimationForPreview();
                    }
                }
                else
                {
                    meshLoadedInPreview = false;
                    vfLogError("Failed to load mesh for animation preview: {}", meshPath);
                }
            }
        }

        ImGui::Spacing();

        // Status display
        if (meshLoadedInPreview && animationLoadedInPreview)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Preview: Ready");
        }
        else if (meshLoadedInPreview)
        {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Preview: Mesh loaded");
        }
        else
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Preview: Select a mesh");
        }
    }

    void AnimationPreviewWindow::drawPlaybackControls()
    {
        ImGui::Text("Playback");
        ImGui::Separator();

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

            updateBoneTransformsFromService();
        }

        ImGui::Spacing();

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

        if (io.MouseWheel != 0.0f)
        {
            float zoomFactor = 1.0f - io.MouseWheel * camera->zoomSensitivity * 0.1f;
            camera->setDistance(camera->distance * zoomFactor);
            camera->updateMatrices();
        }

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

        if (!meshLoadedInPreview || !animationLoadedInPreview)
        {
            ImVec2 windowPos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            drawList->AddRectFilled(
                windowPos,
                ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y),
                IM_COL32(25, 25, 30, 255)
            );

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

            const char* placeholderText = "3D Preview Unavailable";
            const char* subText = "Mesh required for 3D visualization";

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

        if (availSize.x <= 0 || availSize.y <= 0) return;

        camera->setAspectRatio(availSize.x / availSize.y);
        handlePreviewInput();

        if (isPlaying)
        {
            float deltaTime = static_cast<float>(ImGui::GetIO().DeltaTime);
            services::events::animpreview::UpdateAnimationPreviewCommand updateCmd;
            updateCmd.instanceId = getPreviewInstanceId();
            updateCmd.deltaTime = deltaTime;
            events::EventDispatcher::instance().execute(updateCmd);
        }

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

        services::events::animpreview::UpdateAnimationCameraCommand cameraCmd;
        cameraCmd.instanceId = getPreviewInstanceId();
        cameraCmd.view = camera->getViewMatrix();
        cameraCmd.projection = camera->getProjectionMatrix();
        cameraCmd.cameraPos = camera->getPosition();
        events::EventDispatcher::instance().execute(cameraCmd);

        services::events::animpreview::RenderAnimationPreviewQuery renderQuery;
        renderQuery.instanceId = getPreviewInstanceId();
        auto textureHandle = events::EventDispatcher::instance().query(renderQuery);
        updateBoneTransformsFromService();

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
        glm::vec4 clipPos = camera->getProjectionMatrix() * camera->getViewMatrix() * glm::vec4(worldPos, 1.0f);

        if (std::abs(clipPos.w) < 0.0001f)
        {
            return ImVec2(-10000, -10000); // Off-screen
        }

        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

        float screenX = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        float screenY = viewportPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y; // Flip Y

        return ImVec2(screenX, screenY);
    }

    void AnimationPreviewWindow::drawBoneVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImU32 boneColor = IM_COL32(255, 255, 0, 255);
        ImU32 jointColor = IM_COL32(255, 100, 100, 255);
        ImU32 selectedColor = IM_COL32(0, 255, 255, 255);

        for (size_t i = 0; i < evaluatedBones.size(); ++i)
        {
            const auto& bone = evaluatedBones[i];
            glm::vec3 boneWorldPos = bone.skinnedPosition;
            ImVec2 screenPos = worldToScreen(boneWorldPos, viewportPos, viewportSize);

            if (screenPos.x < viewportPos.x - 100 || screenPos.x > viewportPos.x + viewportSize.x + 100 ||
                screenPos.y < viewportPos.y - 100 || screenPos.y > viewportPos.y + viewportSize.y + 100)
            {
                continue;
            }

            if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(evaluatedBones.size()))
            {
                const auto& parentBone = evaluatedBones[bone.parentIndex];
                glm::vec3 parentWorldPos = parentBone.skinnedPosition;

                ImVec2 parentScreenPos = worldToScreen(parentWorldPos, viewportPos, viewportSize);

                ImU32 lineColor = (static_cast<int>(i) == selectedChannel) ? selectedColor : boneColor;
                drawList->AddLine(parentScreenPos, screenPos, lineColor, 2.0f);
            }

            float jointRadius = (static_cast<int>(i) == selectedChannel) ? 6.0f : 4.0f;
            ImU32 circleColor = (static_cast<int>(i) == selectedChannel) ? selectedColor : jointColor;
            drawList->AddCircleFilled(screenPos, jointRadius, circleColor);

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

        int sequenceOptions = ImSequencer::SEQUENCER_CHANGE_FRAME;

        if (ImSequencer::Sequencer(sequenceAdapter.get(), &currentFrame, &sequencerExpanded,
                                   &selectedChannel, &firstFrame, sequenceOptions))
        {
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

        ImGui::Checkbox("Show Bones", &showBoneVisualization);
        ImGui::Separator();

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

            ImGui::Separator();
        }

        ImGui::Text("Bones");
        ImGui::Separator();

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

        auto childIt = boneChildrenMap.find(static_cast<int32_t>(index));
        bool hasChildren = (childIt != boneChildrenMap.end() && !childIt->second.empty());
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

        bool nodeOpen = ImGui::TreeNodeEx(bone.name.c_str(), flags);

        if (ImGui::IsItemClicked())
            selectedChannel = static_cast<int>(index);

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
        if (animationLoadedInPreview)
        {
            services::events::animpreview::GetAnimationPlaybackTimeQuery timeQuery;
            timeQuery.instanceId = getPreviewInstanceId();
            float currentTimeSeconds = events::EventDispatcher::instance().query(timeQuery);

            float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
            float currentTimeInTicks = currentTimeSeconds * ticksPerSec;

            currentFrame = timeToFrame(currentTimeInTicks);
            updateBoneTransformsFromService();

            services::events::animpreview::IsAnimationPlayingQuery playingQuery;
            playingQuery.instanceId = getPreviewInstanceId();
            isPlaying = events::EventDispatcher::instance().query(playingQuery);
        }
        else
        {
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
            updateBoneTransformsFromService();
        }
    }

    void AnimationPreviewWindow::seekToTime(float timeInTicks)
    {
        float clampedTime = glm::clamp(timeInTicks, 0.0f, animationData.duration);
        currentFrame = timeToFrame(clampedTime);
        updateBoneTransformsFromService();

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
