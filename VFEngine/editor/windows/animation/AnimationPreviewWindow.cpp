#include "AnimationPreviewWindow.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "imgui.h"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "asset/AssetRef.hpp"
#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationPreviewEvents.hpp"
#include <filesystem>
#include <cmath>

namespace windows
{
    AnimationPreviewWindow::AnimationPreviewWindow(const std::string& filePath)
        : animationPath(filePath)
        , camera(std::make_unique<editor::OrbitCamera>())
    {
        std::filesystem::path path(filePath);
        windowTitle = "Animation Preview: " + path.filename().string();

        panelState.animationPath = animationPath;
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

        // Load sockets and IK chains from mesh when mesh path changes
        if (!panelState.meshPath.empty() && panelState.meshPath != lastLoadedMeshPath)
        {
            loadSocketsFromMesh();
            loadIKChainsFromMesh();
            lastLoadedMeshPath = panelState.meshPath;
        }

        float currentImGuiTime = static_cast<float>(ImGui::GetTime());
        float deltaTime = currentImGuiTime - lastFrameTime;
        lastFrameTime = currentImGuiTime;

        if (panelState.animationLoaded && panelState.isPlaying)
        {
            updatePlayback(deltaTime);
        }

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("AnimationPreview", ImVec2(1200, 750));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse | maximizer.windowFlags()))
        {
            if (isOpen)
            {
                maximizer.drawButton();

                static float leftPanelWidth = 220.0f;
                static float rightPanelWidth = 300.0f;
                const float splitterThickness = 5.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                leftPanelWidth = std::clamp(leftPanelWidth, 160.0f,
                                            std::max(160.0f, contentSize.x * 0.4f));
                rightPanelWidth = std::clamp(rightPanelWidth, 220.0f,
                                             std::max(220.0f, contentSize.x * 0.4f));

                ImGui::BeginChild("InfoPanel", ImVec2(leftPanelWidth, contentSize.y), true);
                infoPanel.draw(panelState, getPreviewInstanceId());
                ImGui::Spacing();
                ImGui::Checkbox("Physics Panel", &showPhysicsPanel);
                ImGui::Checkbox("Socket Panel", &showSocketPanel);
                ImGui::Checkbox("IK Chain Panel", &showIKChainPanel);
                ImGui::EndChild();

                float middleWidth = contentSize.x - leftPanelWidth - rightPanelWidth - splitterThickness * 2.0f;

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##animSplitLeft", splitterThickness, &leftPanelWidth,
                                           &middleWidth, 160.0f, 300.0f, contentSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::BeginChild("MiddlePanel", ImVec2(middleWidth, contentSize.y), false);

                if (loadingInProgress.load())
                {
                    infoPanel.drawLoadingIndicator(loadingStatus);
                }
                else if (panelState.animationLoaded)
                {
                    static float previewHeightFraction = 0.6f;
                    previewHeightFraction = std::clamp(previewHeightFraction, 0.25f, 0.85f);
                    float previewHeight = contentSize.y * previewHeightFraction;
                    ImGui::BeginChild("3DViewportPanel", ImVec2(middleWidth - 5, previewHeight), true,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                    viewport.draw({viewportSize.x, viewportSize.y,
                                   panelState.meshLoadedInPreview,
                                   panelState.animationLoadedInPreview,
                                   panelState.isPlaying,
                                   camera.get(),
                                   evaluatedBones,
                                   selectedChannel,
                                   showBoneVisualization,
                                   getPreviewInstanceId(),
                                   isDraggingPreview,
                                   isDraggingPan,
                                   environment,
                                   showPhysicsPanel && showColliderOverlay,
                                   &physicsConfig,
                                   &boneNameToIndex,
                                   showSocketPanel && showSocketVisualization,
                                   &socketDefinitions});
                    updateBoneTransformsFromService();
                    ImGui::EndChild();

                    float timelineHeight = ImGui::GetContentRegionAvail().y - splitterThickness
                        - ImGui::GetStyle().ItemSpacing.y * 2.0f;
                    if (editor::preview::splitterH("##animSplitTimeline", splitterThickness,
                                                   &previewHeight, &timelineHeight,
                                                   150.0f, 100.0f, middleWidth - 5))
                    {
                        previewHeightFraction = previewHeight / std::max(contentSize.y, 1.0f);
                    }

                    ImGui::BeginChild("TimelinePanel", ImVec2(middleWidth - 5, 0), true);
                    timelinePanel.draw({currentFrame, selectedChannel, sequencerExpanded, firstFrame,
                                       &animationData, getPreviewInstanceId(), &animationEvents,
                                       animationPath});
                    ImGui::EndChild();
                }

                ImGui::EndChild();

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##animSplitRight", splitterThickness, &middleWidth,
                                           &rightPanelWidth, 300.0f, 220.0f, contentSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::BeginChild("RightPanel", ImVec2(rightPanelWidth, contentSize.y), false);

                int activePanels = (showPhysicsPanel ? 1 : 0) + (showSocketPanel ? 1 : 0) + (showIKChainPanel ? 1 : 0);
                float skeletonHeight = activePanels > 0
                    ? contentSize.y * (activePanels > 1 ? 0.33f : 0.4f)
                    : contentSize.y;

                ImGui::BeginChild("SkeletonPanel", ImVec2(rightPanelWidth, skeletonHeight), true);
                if (panelState.animationLoaded)
                {
                    const std::unordered_set<std::string>* mappedNames = showPhysicsPanel ? &mappedBoneNames : nullptr;
                    skeletonPanel.draw(evaluatedBones, boneChildrenMap, selectedChannel,
                                       showBoneVisualization, panelState.meshLoadedInPreview, camera.get(),
                                       mappedNames);
                }
                else
                {
                    ImGui::TextDisabled("Loading...");
                }
                ImGui::EndChild();

                if (showPhysicsPanel)
                {
                    float physicsHeight = showSocketPanel ? contentSize.y * 0.33f : 0;
                    ImGui::BeginChild("PhysicsPanel", ImVec2(rightPanelWidth, physicsHeight), true);
                    if (physicsPanel.draw(physicsConfig, selectedChannel, evaluatedBones,
                                          boneNameToIndex, showColliderOverlay, physicsConfigPath))
                    {
                        buildMappedBoneNames();
                    }
                    ImGui::EndChild();
                }

                if (showSocketPanel)
                {
                    float socketHeight = showIKChainPanel ? contentSize.y * 0.33f : 0;
                    ImGui::BeginChild("SocketPanel", ImVec2(rightPanelWidth, socketHeight), true);
                    if (panelState.animationLoaded)
                    {
                        socketPanel.draw(socketDefinitions, selectedChannel, evaluatedBones,
                                         boneNameToIndex, showSocketVisualization,
                                         panelState.meshPath);
                    }
                    else
                    {
                        ImGui::TextDisabled("Loading...");
                    }
                    ImGui::EndChild();
                }

                if (showIKChainPanel)
                {
                    ImGui::BeginChild("IKChainPanel", ImVec2(rightPanelWidth, 0), true);
                    if (panelState.animationLoaded)
                    {
                        ikChainPanel.draw(ikChainConfigs, selectedChannel, evaluatedBones,
                                          boneNameToIndex, boneChildrenMap, showIKChainVisualization,
                                          panelState.meshPath);
                    }
                    else
                    {
                        ImGui::TextDisabled("Loading...");
                    }
                    ImGui::EndChild();
                }

                ImGui::EndChild();
            }
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("AnimationPreview", maximizer.effectiveSize());
            sizeSaved = true;
        }
    }

    void AnimationPreviewWindow::initPreviewRenderer()
    {
        services::events::animpreview::InitAnimationPreviewCommand initCmd;
        initCmd.instanceId = getPreviewInstanceId();
        events::EventDispatcher::instance().execute(initCmd);
        previewInitialized = true;
        panelState.previewInitialized = true;
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
            panelState.animationLoadedInPreview = true;

            services::events::animpreview::SetAnimationLoopingCommand loopCmd;
            loopCmd.instanceId = getPreviewInstanceId();
            loopCmd.looping = panelState.isLooping;
            events::EventDispatcher::instance().execute(loopCmd);

            services::events::animpreview::SetAnimationPlaybackSpeedCommand speedCmd;
            speedCmd.instanceId = getPreviewInstanceId();
            speedCmd.speed = panelState.playbackSpeed;
            events::EventDispatcher::instance().execute(speedCmd);

            updateBoneTransformsFromService();
            buildBoneHierarchyMaps();
        }
        else
        {
            vfLogError("Failed to load animation for preview: {}", animationPath);
        }
    }

    void AnimationPreviewWindow::startAsyncLoad()
    {
        loadingInProgress.store(true);
        loadingStatus = "Loading animation file...";

        loadFuture = resource::ResourceManager::loadAnimationAsync(asset::AssetRef::fromPath(animationPath));
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
                        timelinePanel.setAnimationData(&animationData);
                        panelState.animationLoaded = true;
                        panelState.animationData = &animationData;
                        loadAnimationForPreview();

                        // Load saved animation events from the .vfAnim file
                        animationEvents = animationData.events;
                    }
                    else
                    {
                        panelState.loadFailed = true;
                    }
                }
                else
                {
                    panelState.loadFailed = true;
                }
            }
            catch (const std::exception&)
            {
                panelState.loadFailed = true;
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

    void AnimationPreviewWindow::updatePlayback(float deltaTime)
    {
        if (panelState.animationLoadedInPreview)
        {
            services::events::animpreview::GetAnimationPlaybackTimeQuery timeQuery;
            timeQuery.instanceId = getPreviewInstanceId();
            float currentTimeSeconds = events::EventDispatcher::instance().query(timeQuery);

            float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
            float currentTimeInTicks = currentTimeSeconds * ticksPerSec;

            currentFrame = static_cast<int>(currentTimeInTicks);
            updateBoneTransformsFromService();

            services::events::animpreview::IsAnimationPlayingQuery playingQuery;
            playingQuery.instanceId = getPreviewInstanceId();
            panelState.isPlaying = events::EventDispatcher::instance().query(playingQuery);
        }
        else
        {
            float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
            float currentTimeInTicks = static_cast<float>(currentFrame);

            currentTimeInTicks += deltaTime * ticksPerSec * panelState.playbackSpeed;

            if (currentTimeInTicks >= animationData.duration)
            {
                if (panelState.isLooping)
                {
                    currentTimeInTicks = fmod(currentTimeInTicks, animationData.duration);
                }
                else
                {
                    currentTimeInTicks = animationData.duration;
                    panelState.isPlaying = false;
                }
            }

            currentFrame = static_cast<int>(currentTimeInTicks);
            updateBoneTransformsFromService();
        }
    }

    void AnimationPreviewWindow::buildMappedBoneNames()
    {
        mappedBoneNames.clear();
        for (const auto& mapping : physicsConfig.boneBodyMappings)
        {
            mappedBoneNames.insert(mapping.boneName);
        }
    }

    void AnimationPreviewWindow::loadSocketsFromMesh()
    {
        socketDefinitions.clear();

        auto stream = resource::MeshStreamResource::openStream(panelState.meshPath);
        if (!stream || !stream->hasSkeletonData())
        {
            return;
        }

        resource::SkeletonData skeleton;
        if (stream->readSkeleton(skeleton))
        {
            socketDefinitions = skeleton.sockets;
            if (!socketDefinitions.empty())
            {
                vfLogInfo("Loaded {} sockets from mesh: {}", socketDefinitions.size(), panelState.meshPath);
            }
        }
    }

    void AnimationPreviewWindow::loadIKChainsFromMesh()
    {
        ikChainConfigs.clear();

        auto stream = resource::MeshStreamResource::openStream(panelState.meshPath);
        if (!stream || !stream->hasSkeletonData())
        {
            return;
        }

        resource::SkeletonData skeleton;
        if (stream->readSkeleton(skeleton))
        {
            ikChainConfigs = skeleton.ikChains;
            if (!ikChainConfigs.empty())
            {
                vfLogInfo("Loaded {} IK chains from mesh: {}", ikChainConfigs.size(), panelState.meshPath);
            }
        }
    }
}
