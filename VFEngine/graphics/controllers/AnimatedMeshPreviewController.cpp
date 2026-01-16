#include "AnimatedMeshPreviewController.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
#include "../core/OffScreen.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/Utilities.hpp"
#include "../core/RenderManager.hpp"
#include "../render/mesh/SkinnedMeshPipeline.hpp"
#include "resource/AnimationResource.hpp"
#include "print/Logger.hpp"
#include <imgui_impl_vulkan.h>
#include <unordered_map>

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

            initialized = true;
        }
        catch (const std::exception& e)
        {
            loggerError("Failed to initialize AnimatedMeshPreviewController: {}", e.what());
            cleanUp();
        }
    }

    void AnimatedMeshPreviewController::cleanUp()
    {
        device.getLogicalDevice().waitIdle();

        unload();

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

        vk::Format colorFormat = swapChain.getSwapchainImageFormat();
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
        core::ImageUtilities::createImage(imageDepthInfo, depth.depthImage, depth.depthImageMemory);

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
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), transitionDepthImage);

        offscreenResources->depthImage = std::move(depth);

        offscreenResources->colorImages.reserve(swapChain.getImageCount());

        for (size_t i = 0; i < swapChain.getImageCount(); i++)
        {
            core::ColorImage color;
            core::ImageUtilities::createImage(imageColorInfo, color.colorImage, color.colorImageMemory);

            core::ImageViewInfoRequest imageColorViewRequest(device.getLogicalDevice(), color.colorImage);
            imageColorViewRequest.format = colorFormat;
            core::ImageUtilities::createImageView(imageColorViewRequest, color.colorImageView);

            vk::UniqueCommandBuffer transitionColorImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(transitionColorImage.get(), color.colorImage,
                                                        vk::ImageLayout::eUndefined,
                                                        vk::ImageLayout::eShaderReadOnlyOptimal,
                                                        vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), transitionColorImage);

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
            device.getLogicalDevice().freeMemory(resources.colorImageMemory);
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
        if (offscreenResources->depthImage.depthImageMemory)
        {
            device.getLogicalDevice().freeMemory(offscreenResources->depthImage.depthImageMemory);
        }

        offscreenResources.reset();
    }

    void AnimatedMeshPreviewController::updateDescriptorSet(vk::DescriptorSet& descriptorSet,
                                                            const vk::ImageView& imageView) const
    {
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    bool AnimatedMeshPreviewController::loadAnimation(const std::string& animationPath)
    {
        if (!initialized) init();

        animationLoaded = false;
        animEvaluator.clear();

        animationData = resource::AnimationResource::loadAnimation(animationPath);
        if (animationData.channels.empty())
        {
            loggerError("Failed to load animation: {}", animationPath);
            return false;
        }

        loadedAnimationPath = animationPath;
        animationLoaded = true;

        float ticksPerSec = animationData.ticksPerSecond > 0.0f ? animationData.ticksPerSecond : 24.0f;
        playbackState.duration = animationData.duration / ticksPerSec;
        playbackState.currentTime = 0.0f;

        if (animationData.hasMesh())
        {
            loggerInfo("Loading embedded mesh from animation: {} vertices, {} indices",
                       animationData.vertices.size(), animationData.indices.size());

            if (!skinnedPipeline->loadMeshFromAnimation(animationData))
            {
                loggerError("Failed to load embedded mesh from animation");
                return false;
            }

            loadedMeshPath = "embedded:" + animationPath;
        }

        if (animationData.hasInverseBindPoses())
        {
            animEvaluator.loadAnimation(animationData);
            meshToAnimBoneMapping.clear();

            auto initialBoneMatrices = animEvaluator.evaluatePose(0.0f);
            if (!initialBoneMatrices.empty() && skinnedPipeline)
            {
                skinnedPipeline->updateBoneMatrices(initialBoneMatrices);
            }
        }
        else
        {
            loggerWarning("Animation missing inverse bind poses - requires re-import");
        }

        return true;
    }

    void AnimatedMeshPreviewController::unload()
    {
        if (skinnedPipeline)
        {
            skinnedPipeline->unloadMesh();
        }

        loadedMeshPath.clear();
        loadedAnimationPath.clear();
        animationLoaded = false;
        animEvaluator.clear();
        animationData = resource::AnimationData{};
        playbackState = render::mesh::AnimationPlaybackState{};
        meshBounds = math::AABB{};
        meshToAnimBoneMapping.clear();
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
                auto remappedMatrices = remapBoneMatrices(boneMatrices);
                skinnedPipeline->updateBoneMatrices(remappedMatrices);
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
                auto remappedMatrices = remapBoneMatrices(boneMatrices);
                skinnedPipeline->updateBoneMatrices(remappedMatrices);
            }
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
        if (!initialized || loadedMeshPath.empty())
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

        skinnedPipeline->recordCommandBuffer(commandBuffer, imageIndex, renderData);

        commandBuffer.end();

        vk::SubmitInfo submitInfo(
            0, nullptr, nullptr,
            1, &commandBuffer,
            0, nullptr
        );

        device.getGraphicsQueue().submit(submitInfo, inFlightFences[imageIndex]);
        device.getGraphicsQueue().waitIdle();

        return static_cast<void*>(offscreenResources->colorImages[imageIndex].descriptorSet);
    }

    void AnimatedMeshPreviewController::buildBoneMapping()
    {
        meshToAnimBoneMapping.clear();

        if (!skinnedPipeline || !skinnedPipeline->getLoadedMesh())
        {
            return;
        }

        const auto& meshSkeleton = skinnedPipeline->getLoadedMesh()->skeleton;
        if (!meshSkeleton.hasBones())
        {
            return;
        }

        std::unordered_map<std::string, size_t> animBoneNameToIndex;
        for (size_t i = 0; i < animationData.skeleton.size(); ++i)
        {
            animBoneNameToIndex[animationData.skeleton[i].name] = i;
        }

        meshToAnimBoneMapping.resize(meshSkeleton.boneCount(), -1);
        size_t matchedCount = 0;

        for (size_t meshBoneIdx = 0; meshBoneIdx < meshSkeleton.boneCount(); ++meshBoneIdx)
        {
            const std::string& meshBoneName = meshSkeleton.boneNames[meshBoneIdx];
            auto it = animBoneNameToIndex.find(meshBoneName);

            if (it != animBoneNameToIndex.end())
            {
                meshToAnimBoneMapping[meshBoneIdx] = static_cast<int32_t>(it->second);
                matchedCount++;
            }
            else
            {
                loggerWarning("Mesh bone '{}' (index {}) not found in animation", meshBoneName, meshBoneIdx);
            }
        }

        loggerInfo("Bone mapping built: {}/{} mesh bones matched to animation bones",
                   matchedCount, meshSkeleton.boneCount());
    }

    std::vector<glm::mat4> AnimatedMeshPreviewController::remapBoneMatrices(
        const std::vector<glm::mat4>& animBoneMatrices) const
    {
        if (!skinnedPipeline || !skinnedPipeline->getLoadedMesh())
        {
            return animBoneMatrices;
        }

        const auto& meshSkeleton = skinnedPipeline->getLoadedMesh()->skeleton;
        size_t meshBoneCount = meshSkeleton.boneCount();

        if (meshBoneCount == animBoneMatrices.size() && meshToAnimBoneMapping.empty())
        {
            return animBoneMatrices;
        }

        if (!meshToAnimBoneMapping.empty())
        {
            std::vector<glm::mat4> remappedMatrices(meshBoneCount, glm::mat4(1.0f));
            static bool loggedOnce = false;

            for (size_t meshBoneIdx = 0; meshBoneIdx < meshBoneCount; ++meshBoneIdx)
            {
                int32_t animBoneIdx = meshToAnimBoneMapping[meshBoneIdx];
                if (animBoneIdx >= 0 && animBoneIdx < static_cast<int32_t>(animBoneMatrices.size()))
                {
                    remappedMatrices[meshBoneIdx] = animBoneMatrices[animBoneIdx];

                    if (!loggedOnce && meshBoneIdx < 3)
                    {
                        loggerInfo("Remap bone[{}] '{}': animBone={}, matrix[3]=({:.2f},{:.2f},{:.2f})",
                                   meshBoneIdx, meshSkeleton.boneNames[meshBoneIdx], animBoneIdx,
                                   animBoneMatrices[animBoneIdx][3][0],
                                   animBoneMatrices[animBoneIdx][3][1],
                                   animBoneMatrices[animBoneIdx][3][2]);
                    }
                }
            }

            if (!loggedOnce) loggedOnce = true;
            return remappedMatrices;
        }

        return animBoneMatrices;
    }
}
