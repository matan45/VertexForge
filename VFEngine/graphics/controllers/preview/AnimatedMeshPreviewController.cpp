#include "AnimatedMeshPreviewController.hpp"
#include "../../core/VulkanContext.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/CommandPool.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/RenderManager.hpp"
#include "../../render/mesh/SkinnedMeshPipeline.hpp"
#include "../../render/ClearColor.hpp"
#include "../../render/preview/PreviewBackgroundRenderer.hpp"
#include "../../render/preview/PreviewGridRenderer.hpp"
#include "resource/AnimationResource.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Log.hpp"
#include <imgui_impl_vulkan.h>

namespace controllers
{
    AnimatedMeshPreviewController::AnimatedMeshPreviewController()
        : device{*core::VulkanContext::getDevice()}
          , swapChain{*core::VulkanContext::getSwapChain()}
          , commandPool{std::make_unique<core::CommandPool>(device, swapChain)}
    {
        renderData.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        renderData.metallic = 0.0f;
        renderData.roughness = 0.5f;
        renderData.ao = 1.0f;
        renderData.emission = 0.0f;
        renderData.modelMatrix = glm::mat4(1.0f);
    }

    AnimatedMeshPreviewController::~AnimatedMeshPreviewController()
    {
        device.getLogicalDevice().waitIdle();
        cleanUp();
    }

    void AnimatedMeshPreviewController::init()
    {
        if (initialized) return;

        try
        {
            createSampler();
            createOffscreenResources();

            vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
            inFlightFences.resize(swapChain.getImageCount());
            for (auto& fence : inFlightFences)
            {
                fence = device.getLogicalDevice().createFence(fenceInfo);
            }

            skinnedPipeline = std::make_unique<render::mesh::SkinnedMeshPipeline>(
                device, swapChain, *offscreenResources);
            skinnedPipeline->init();

            clearColor = std::make_unique<render::ClearColor>(device, swapChain, *offscreenResources);
            clearColor->init();

            previewBackground = std::make_unique<render::preview::PreviewBackgroundRenderer>(
                device, swapChain, *offscreenResources);
            previewBackground->init();

            previewGrid = std::make_unique<render::preview::PreviewGridRenderer>(
                device, swapChain, *offscreenResources);
            previewGrid->init();

            initialized = true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to initialize AnimatedMeshPreviewController: {}", e.what());
            cleanUp();
        }
    }

    void AnimatedMeshPreviewController::cleanUp()
    {
        device.getLogicalDevice().waitIdle();

        unload();

        if (previewGrid)
        {
            previewGrid->cleanUpShader();
            previewGrid->cleanUp();
            previewGrid.reset();
        }

        if (previewBackground)
        {
            previewBackground->cleanUpShader();
            previewBackground->cleanUp();
            previewBackground.reset();
        }

        if (clearColor)
        {
            clearColor->cleanUp();
            clearColor.reset();
        }

        if (skinnedPipeline)
        {
            skinnedPipeline->cleanUp();
            skinnedPipeline.reset();
        }

        for (auto& fence : inFlightFences)
        {
            if (fence)
            {
                device.getLogicalDevice().destroyFence(fence);
            }
        }
        inFlightFences.clear();

        if (offscreenResources)
        {
            for (auto const& resources : offscreenResources->colorImages)
            {
                if (resources.descriptorSet)
                {
                    ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
                }
            }
        }

        if (sampler)
        {
            device.getLogicalDevice().destroySampler(sampler);
            sampler = nullptr;
        }

        cleanupOffscreenResources();

        if (commandPool)
        {
            commandPool->cleanUp();
        }

        initialized = false;
    }

    void AnimatedMeshPreviewController::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        vk::PhysicalDeviceProperties properties = device.getPhysicalDevice().getProperties();
        float maxAnisotropy = properties.limits.maxSamplerAnisotropy;

        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = maxAnisotropy;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void AnimatedMeshPreviewController::createOffscreenResources()
    {
        offscreenResources = std::make_unique<core::OffscreenResources>();

        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

        core::ImageInfoRequest imageColorInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageColorInfo.width = swapChain.getSwapchainExtent().width;
        imageColorInfo.height = swapChain.getSwapchainExtent().height;
        imageColorInfo.format = colorFormat;
        imageColorInfo.tiling = vk::ImageTiling::eOptimal;
        imageColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        imageColorInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageInfoRequest imageDepthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageDepthInfo.width = swapChain.getSwapchainExtent().width;
        imageDepthInfo.height = swapChain.getSwapchainExtent().height;
        imageDepthInfo.format = depthFormat;
        imageDepthInfo.tiling = vk::ImageTiling::eOptimal;
        imageDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
        imageDepthInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::DepthImage depth;
        core::ImageUtilities::createImage(imageDepthInfo, depth.depthImage, depth.depthImageAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), depth.depthImage);
        imageDepthRequest.format = depthFormat;
        imageDepthRequest.aspectFlags = vk::ImageAspectFlagBits::eDepth;
        core::ImageUtilities::createImageView(imageDepthRequest, depth.depthImageView);

        vk::UniqueCommandBuffer transitionDepthImage = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool->getCommandPool());
        core::ImageUtilities::transitionImageLayout(transitionDepthImage.get(), depth.depthImage,
                                                    vk::ImageLayout::eUndefined,
                                                    vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                                    vk::ImageAspectFlagBits::eDepth |
                                                    vk::ImageAspectFlagBits::eStencil);
        core::Utilities::endSingleTimeCommands(device, transitionDepthImage);

        offscreenResources->depthImage = std::move(depth);

        offscreenResources->colorImages.reserve(swapChain.getImageCount());

        for (size_t i = 0; i < swapChain.getImageCount(); i++)
        {
            core::ColorImage color;
            core::ImageUtilities::createImage(imageColorInfo, color.colorImage, color.colorImageAllocation, device.getMemoryManager());

            core::ImageViewInfoRequest imageColorViewRequest(device.getLogicalDevice(), color.colorImage);
            imageColorViewRequest.format = colorFormat;
            core::ImageUtilities::createImageView(imageColorViewRequest, color.colorImageView);

            vk::UniqueCommandBuffer transitionColorImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(transitionColorImage.get(), color.colorImage,
                                                        vk::ImageLayout::eUndefined,
                                                        vk::ImageLayout::eShaderReadOnlyOptimal,
                                                        vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device, transitionColorImage);

            updateDescriptorSet(color.descriptorSet, color.colorImageView);

            offscreenResources->colorImages.push_back(std::move(color));
        }
    }

    void AnimatedMeshPreviewController::cleanupOffscreenResources()
    {
        if (!offscreenResources) return;

        for (auto const& resources : offscreenResources->colorImages)
        {
            device.getLogicalDevice().destroyImageView(resources.colorImageView);
            device.getLogicalDevice().destroyImage(resources.colorImage);
            device.getMemoryManager().free(resources.colorImageAllocation);
        }
        offscreenResources->colorImages.clear();

        if (offscreenResources->depthImage.depthImageView)
        {
            device.getLogicalDevice().destroyImageView(offscreenResources->depthImage.depthImageView);
        }
        if (offscreenResources->depthImage.depthImage)
        {
            device.getLogicalDevice().destroyImage(offscreenResources->depthImage.depthImage);
        }
        if (offscreenResources->depthImage.depthImageAllocation)
        {
            device.getMemoryManager().free(offscreenResources->depthImage.depthImageAllocation);
            offscreenResources->depthImage.depthImageAllocation = {};
        }

        offscreenResources.reset();
    }

    void AnimatedMeshPreviewController::updateDescriptorSet(vk::DescriptorSet& descriptorSet,
                                                            const vk::ImageView& imageView) const
    {
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    bool AnimatedMeshPreviewController::loadMesh(const std::string& meshPath)
    {
        if (!initialized) init();

        meshLoaded = false;
        skeletonData = resource::SkeletonData{};
        retargetActive = false; // target skeleton changed; any retarget context is stale

        if (skinnedPipeline)
        {
            skinnedPipeline->unloadMesh();
        }

        resource::MeshStreamHandle meshHandle;
        if (!meshHandle.openStream(meshPath))
        {
            vfLogError("Failed to open mesh file: {}", meshPath);
            return false;
        }

        if (!meshHandle.hasSkeletonData())
        {
            vfLogError("Mesh file has no skeleton data: {}", meshPath);
            return false;
        }

        if (!meshHandle.readSkeleton(skeletonData))
        {
            vfLogError("Failed to read skeleton from mesh: {}", meshPath);
            return false;
        }

        if (!skinnedPipeline->loadMeshFromFile(meshPath))
        {
            vfLogError("Failed to load mesh into pipeline: {}", meshPath);
            skeletonData = resource::SkeletonData{};
            return false;
        }

        loadedMeshPath = meshPath;
        meshLoaded = true;

        vfLogInfo("Loaded mesh with {} bones from: {}", skeletonData.bones.size(), meshPath);

        if (animationLoaded && !animationData.channels.empty())
        {
            animEvaluator.loadAnimation(animationData, skeletonData);

            auto initialBoneMatrices = animEvaluator.evaluatePose(0.0f);
            if (!initialBoneMatrices.empty())
            {
                skinnedPipeline->updateBoneMatrices(initialBoneMatrices);
            }
        }

        return true;
    }

    bool AnimatedMeshPreviewController::loadAnimation(const std::string& animationPath)
    {
        if (!initialized) init();

        animationLoaded = false;
        retargetActive = false; // native clip plays on the mesh's own skeleton
        animEvaluator.clear();

        animationData = resource::AnimationResource::loadAnimation(animationPath);
        if (animationData.channels.empty())
        {
            vfLogError("Failed to load animation: {}", animationPath);
            return false;
        }

        loadedAnimationPath = animationPath;
        animationLoaded = true;

        float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
        playbackState.duration = animationData.duration / ticksPerSec;
        playbackState.currentTime = 0.0f;

        if (meshLoaded && !skeletonData.bones.empty())
        {
            animEvaluator.loadAnimation(animationData, skeletonData);

            auto initialBoneMatrices = animEvaluator.evaluatePose(0.0f);
            if (!initialBoneMatrices.empty() && skinnedPipeline)
            {
                skinnedPipeline->updateBoneMatrices(initialBoneMatrices);
            }
        }
        else
        {
            vfLogInfo("Animation loaded, waiting for mesh with skeleton to be loaded");
        }

        return true;
    }

    bool AnimatedMeshPreviewController::loadRetargetedAnimation(
        const std::string& sourceAnimPath,
        const resource::SkeletonData& sourceSkeleton,
        const retargeting::HumanoidRigData& sourceRig,
        const retargeting::HumanoidRigData& targetRig,
        const retargeting::RetargetMapData& map)
    {
        if (!initialized) init();

        if (!meshLoaded || skeletonData.bones.empty())
        {
            vfLogError("Cannot retarget: no target mesh/skeleton loaded");
            return false;
        }

        animationLoaded = false;
        retargetActive = false;
        animEvaluator.clear();

        animationData = resource::AnimationResource::loadAnimation(sourceAnimPath);
        if (animationData.channels.empty())
        {
            vfLogError("Failed to load source animation: {}", sourceAnimPath);
            return false;
        }

        sourceSkeletonData = sourceSkeleton;
        retargetContext = animation::RetargetContext::build(skeletonData, &sourceSkeletonData,
                                                            sourceRig, targetRig, map);
        retargetActive = !retargetContext.empty();
        if (!retargetActive)
        {
            vfLogError("Retarget context is empty (no role overlap) for: {}", sourceAnimPath);
            return false;
        }

        loadedAnimationPath = sourceAnimPath;
        animationLoaded = true;

        float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
        playbackState.duration = animationData.duration / ticksPerSec;
        playbackState.currentTime = 0.0f;

        animEvaluator.loadAnimation(animationData, skeletonData, &retargetContext);
        auto initialBoneMatrices = animEvaluator.evaluatePose(0.0f);
        if (!initialBoneMatrices.empty() && skinnedPipeline)
        {
            skinnedPipeline->updateBoneMatrices(initialBoneMatrices);
        }
        return true;
    }

    void AnimatedMeshPreviewController::unload()
    {
        retargetActive = false;
        sourceSkeletonData = resource::SkeletonData{};

        if (skinnedPipeline)
        {
            skinnedPipeline->unloadMesh();
        }

        loadedMeshPath.clear();
        loadedAnimationPath.clear();
        animationLoaded = false;
        meshLoaded = false;
        animEvaluator.clear();
        animationData = resource::AnimationData{};
        skeletonData = resource::SkeletonData{};
        playbackState = render::mesh::AnimationPlaybackState{};
        meshBounds = math::AABB{};
    }

    void AnimatedMeshPreviewController::setPlaybackTime(float timeSeconds)
    {
        playbackState.setTime(timeSeconds);

        if (animationLoaded && animEvaluator.isLoaded())
        {
            float timeInTicks = animEvaluator.secondsToTicks(playbackState.currentTime);
            auto boneMatrices = animEvaluator.evaluatePose(timeInTicks);

            if (!boneMatrices.empty() && skinnedPipeline)
            {
                skinnedPipeline->updateBoneMatrices(boneMatrices);
            }
        }
    }

    void AnimatedMeshPreviewController::update(float deltaTime)
    {
        playbackState.update(deltaTime);

        if (animationLoaded && animEvaluator.isLoaded())
        {
            float timeInTicks = animEvaluator.secondsToTicks(playbackState.currentTime);
            auto boneMatrices = animEvaluator.evaluatePose(timeInTicks);

            if (!boneMatrices.empty() && skinnedPipeline)
            {
                skinnedPipeline->updateBoneMatrices(boneMatrices);
            }
        }
    }

    void AnimatedMeshPreviewController::setClearColor(const glm::vec4& color)
    {
        if (skinnedPipeline)
        {
            skinnedPipeline->setClearColor(color);
        }
    }

    void AnimatedMeshPreviewController::updateCamera(const glm::mat4& view, const glm::mat4& projection,
                                                     const glm::vec3& cameraPos)
    {
        currentView = view;
        currentProjection = projection;
        currentCameraPos = cameraPos;

        if (skinnedPipeline)
        {
            skinnedPipeline->updateCameraUBO(view, projection, cameraPos);
        }
    }

    void* AnimatedMeshPreviewController::render()
    {
        if (!initialized || !meshLoaded)
        {
            return nullptr;
        }

        uint32_t imageIndex = core::RenderManager::getImageIndex();

        vk::Result result = device.getLogicalDevice().waitForFences(
            1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);
        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();
        commandBuffer.begin(vk::CommandBufferBeginInfo{});

        // Step 1: Clear color + depth via the shared ClearColor helper.
        // For solid mode use the env background color; for gradient mode use the
        // gradient bottom as the clear base (the gradient pass will overwrite it).
        const glm::vec4 clearVal = (environmentParams.backgroundMode == 1)
            ? environmentParams.gradientBottomColor
            : environmentParams.backgroundColor;
        clearColor->setClearColor(clearVal);
        clearColor->recordCommandBuffer(commandBuffer, imageIndex);

        // Step 2: Draw gradient overlay (only in gradient mode).
        if (environmentParams.backgroundMode == 1 && previewBackground && previewBackground->isInitialized())
        {
            previewBackground->render(commandBuffer, imageIndex,
                                      environmentParams.gradientTopColor,
                                      environmentParams.gradientBottomColor);
        }

        // Step 3: Draw the skinned mesh, loading the existing color/depth
        // (so the background we just drew is preserved).
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources->colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        skinnedPipeline->recordCommandBuffer(commandBuffer, imageIndex, renderData, /*clearAttachments=*/false);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources->colorImages[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Step 4: Grid overlay (handles its own transitions; ends in ShaderReadOnlyOptimal).
        if (previewGrid && previewGrid->isInitialized() && environmentParams.showGrid)
        {
            previewGrid->render(commandBuffer, imageIndex, currentView, currentProjection, true);
        }

        commandBuffer.end();

        vk::SubmitInfo submitInfo(
            0, nullptr, nullptr,
            1, &commandBuffer,
            0, nullptr
        );

        device.submitGraphics(submitInfo, inFlightFences[imageIndex]);

        return static_cast<void*>(offscreenResources->colorImages[imageIndex].descriptorSet);
    }
}
