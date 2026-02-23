#include "VFXPreviewController.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/RenderManager.hpp"
#include "../core/VulkanContext.hpp"
#include "../render/vfx/VFXBillboardPipeline.hpp"
#include "../render/vfx/VFXMeshPreviewPipeline.hpp"
#include "../render/vfx/VFXRibbonPreviewPipeline.hpp"
#include "../render/vfx/VFXParticleSystem.hpp"
#include "../render/mesh/MeshGPUCache.hpp"
#include "vfx/VFXModifierConfigLoader.hpp"
#include "print/Logger.hpp"
#include <imgui_impl_vulkan.h>

namespace controllers
{
    VFXPreviewController::VFXPreviewController()
        : swapChain{*core::VulkanContext::getSwapChain()}
        , device{*core::VulkanContext::getDevice()}
        , commandPool{std::make_unique<core::CommandPool>(device, swapChain)}
        , particleSystem{std::make_unique<render::vfx::VFXParticleSystem>()}
    {
    }

    VFXPreviewController::~VFXPreviewController()
    {
        cleanUp();
    }

    void VFXPreviewController::init()
    {
        if (initialized)
        {
            return;
        }

        createSampler();
        createOffscreenResources();

        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        inFlightFences.resize(swapChain.getImageCount());
        for (auto& fence : inFlightFences)
        {
            fence = device.getLogicalDevice().createFence(fenceInfo);
        }

        pipeline = std::make_unique<render::vfx::VFXBillboardPipeline>(device, swapChain, offscreenResources);
        pipeline->init();

        // VK-496: Mesh preview pipeline
        previewMeshCache = std::make_unique<render::mesh::MeshGPUCache>(device);
        meshPipeline = std::make_unique<render::vfx::VFXMeshPreviewPipeline>(device, swapChain, offscreenResources, *previewMeshCache);
        meshPipeline->init();

        // VK-624: Ribbon preview pipeline
        ribbonPipeline = std::make_unique<render::vfx::VFXRibbonPreviewPipeline>(device, swapChain, offscreenResources);
        ribbonPipeline->init();

        render::vfx::VFXEmitterConfig config;
        config.spawnRate = currentParams.spawnRate;
        config.lifetime = currentParams.lifetime;
        config.startSize = currentParams.startSize;
        config.startSpeed = currentParams.startSpeed;
        config.startColor = currentParams.startColor;
        config.emitDirection = currentParams.emitDirection;
        config.texturePath = currentParams.texturePath;
        config.looping = currentParams.looping;
        config.modifiers = currentParams.modifiers;
        config.forces = currentParams.forces;
        config.shape = currentParams.shape;
        config.flipbookRows = currentParams.flipbookRows;
        config.flipbookColumns = currentParams.flipbookColumns;
        config.flipbookFrameRate = currentParams.flipbookFrameRate;
        config.flipbookRandomStart = currentParams.flipbookRandomStart;
        config.alphaClipThreshold = currentParams.alphaClipThreshold;
        config.additiveBlend = currentParams.additiveBlend;
        config.renderMode = static_cast<render::vfx::VFXRenderMode>(currentParams.renderMode);
        config.softParticleDistance = currentParams.softParticleDistance;
        config.stretchMultiplier = currentParams.stretchMultiplier;
        config.maxTrailPoints = static_cast<uint32_t>(currentParams.maxTrailPoints);
        config.ribbonWidth = currentParams.ribbonWidth;
        config.ribbonMinDistance = currentParams.ribbonMinDistance;
        particleSystem->setEmitterConfig(config);

        if (!currentParams.texturePath.empty())
        {
            pipeline->setTexture(currentParams.texturePath);
        }
        glm::vec3 glowColor = ::vfx::VFXModifierConfigLoader::getGlowColorFromChain(currentParams.modifiers);
        pipeline->setFlipbookConfig(currentParams.flipbookRows, currentParams.flipbookColumns,
                                    currentParams.alphaClipThreshold, currentParams.additiveBlend,
                                    currentParams.renderMode, currentParams.stretchMultiplier, glowColor);

        // VK-496: Set mesh and texture on mesh preview pipeline
        if (!currentParams.meshPath.empty())
        {
            meshPipeline->setMesh(currentParams.meshPath);
        }
        if (!currentParams.texturePath.empty())
        {
            meshPipeline->setTexture(currentParams.texturePath);
        }
        meshPipeline->setRenderingConfig(currentParams.alphaClipThreshold, currentParams.additiveBlend, glowColor);

        // VK-624: Configure ribbon preview pipeline
        if (!currentParams.texturePath.empty())
        {
            ribbonPipeline->setTexture(currentParams.texturePath);
        }
        ribbonPipeline->setRenderingConfig(currentParams.alphaClipThreshold, currentParams.additiveBlend,
                                            currentParams.ribbonWidth, glowColor);

        lastExtent = swapChain.getSwapchainExtent();
        initialized = true;
        loggerInfo("VFX Preview Controller initialized");
    }

    void VFXPreviewController::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        if (ribbonPipeline)
        {
            ribbonPipeline->cleanUp();
            ribbonPipeline.reset();
        }

        if (meshPipeline)
        {
            meshPipeline->cleanUp();
            meshPipeline.reset();
        }

        if (previewMeshCache)
        {
            previewMeshCache->unloadAllMeshes();
            previewMeshCache.reset();
        }

        if (pipeline)
        {
            pipeline->cleanUp();
            pipeline.reset();
        }

        commandPool->cleanUp();

        for (auto& fence : inFlightFences)
        {
            if (fence)
            {
                device.getLogicalDevice().destroyFence(fence);
            }
        }
        inFlightFences.clear();

        for (auto const& resources : offscreenResources.colorImages)
        {
            if (resources.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }

        if (sampler)
        {
            device.getLogicalDevice().destroySampler(sampler);
            sampler = nullptr;
        }

        cleanupOffscreenResources();

        initialized = false;
        loggerInfo("VFX Preview Controller cleaned up");
    }

    void VFXPreviewController::setParams(const VFXPreviewParams& params)
    {
        currentParams = params;

        if (particleSystem)
        {
            render::vfx::VFXEmitterConfig config;
            config.spawnRate = params.spawnRate;
            config.lifetime = params.lifetime;
            config.startSize = params.startSize;
            config.startSpeed = params.startSpeed;
            config.startColor = params.startColor;
            config.emitDirection = params.emitDirection;
            config.texturePath = params.texturePath;
            config.looping = params.looping;
            config.modifiers = params.modifiers;
            config.forces = params.forces;
            config.shape = params.shape;
            config.flipbookRows = params.flipbookRows;
            config.flipbookColumns = params.flipbookColumns;
            config.flipbookFrameRate = params.flipbookFrameRate;
            config.flipbookRandomStart = params.flipbookRandomStart;
            config.alphaClipThreshold = params.alphaClipThreshold;
            config.additiveBlend = params.additiveBlend;
            config.renderMode = static_cast<render::vfx::VFXRenderMode>(params.renderMode);
            config.softParticleDistance = params.softParticleDistance;
            config.stretchMultiplier = params.stretchMultiplier;
            config.maxTrailPoints = static_cast<uint32_t>(params.maxTrailPoints);
            config.ribbonWidth = params.ribbonWidth;
            config.ribbonMinDistance = params.ribbonMinDistance;
            particleSystem->setEmitterConfig(config);
        }

        glm::vec3 glowColor = ::vfx::VFXModifierConfigLoader::getGlowColorFromChain(params.modifiers);

        if (pipeline && pipeline->isInitialized())
        {
            pipeline->setTexture(params.texturePath);
            pipeline->setFlipbookConfig(params.flipbookRows, params.flipbookColumns,
                                        params.alphaClipThreshold, params.additiveBlend,
                                        params.renderMode, params.stretchMultiplier, glowColor);
        }

        // VK-496: Update mesh preview pipeline
        if (meshPipeline && meshPipeline->isInitialized())
        {
            meshPipeline->setMesh(params.meshPath);
            meshPipeline->setTexture(params.texturePath);
            meshPipeline->setRenderingConfig(params.alphaClipThreshold, params.additiveBlend, glowColor);
        }

        // VK-624: Update ribbon preview pipeline
        if (ribbonPipeline && ribbonPipeline->isInitialized())
        {
            ribbonPipeline->setTexture(params.texturePath);
            ribbonPipeline->setRenderingConfig(params.alphaClipThreshold, params.additiveBlend,
                                                params.ribbonWidth, glowColor);
        }
    }

    void VFXPreviewController::updateCamera(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos, float time)
    {
        if (pipeline && pipeline->isInitialized())
        {
            pipeline->updateCameraUBO(view, projection, cameraPos, time);
        }

        if (meshPipeline && meshPipeline->isInitialized())
        {
            meshPipeline->updateCameraUBO(view, projection, cameraPos, time);
        }

        if (ribbonPipeline && ribbonPipeline->isInitialized())
        {
            ribbonPipeline->updateCameraUBO(view, projection, cameraPos, time);
        }
    }

    void VFXPreviewController::update(float deltaTime)
    {
        if (particleSystem)
        {
            particleSystem->update(deltaTime);
        }
    }

    void VFXPreviewController::play()
    {
        if (particleSystem)
        {
            particleSystem->setPlaying(true);
        }
    }

    void VFXPreviewController::pause()
    {
        if (particleSystem)
        {
            particleSystem->setPlaying(false);
        }
    }

    void VFXPreviewController::stop()
    {
        if (particleSystem)
        {
            particleSystem->setPlaying(false);
            particleSystem->reset();
        }
    }

    bool VFXPreviewController::isPlaying() const
    {
        return particleSystem ? particleSystem->isPlaying() : false;
    }

    void VFXPreviewController::recreateOffscreenResources()
    {
        device.getLogicalDevice().waitIdle();

        // Remove old ImGui descriptor sets before destroying image views
        for (auto const& resources : offscreenResources.colorImages)
        {
            if (resources.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }

        cleanupOffscreenResources();
        createOffscreenResources();

        pipeline->recreate();

        if (meshPipeline)
        {
            meshPipeline->recreate();
        }

        if (ribbonPipeline)
        {
            ribbonPipeline->recreate();
        }

        lastExtent = swapChain.getSwapchainExtent();
    }

    void* VFXPreviewController::render()
    {
        if (!initialized || !pipeline || !pipeline->isInitialized())
        {
            return nullptr;
        }

        vk::Extent2D currentExtent = swapChain.getSwapchainExtent();
        if (currentExtent.width != lastExtent.width || currentExtent.height != lastExtent.height)
        {
            recreateOffscreenResources();
        }

        uint32_t imageIndex = core::RenderManager::getImageIndex();

        vk::Result result = device.getLogicalDevice().waitForFences(
            1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);
        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        // Route to appropriate pipeline based on render mode
        bool useMeshPipeline = currentParams.renderMode == 3 &&
                               meshPipeline && meshPipeline->isInitialized() &&
                               meshPipeline->hasMesh();

        // VK-624: Ribbon pipeline
        bool useRibbonPipeline = currentParams.renderMode == 4 &&
                                 ribbonPipeline && ribbonPipeline->isInitialized();

        if (particleSystem)
        {
            if (useRibbonPipeline)
            {
                auto segments = particleSystem->getRibbonSegments();
                ribbonPipeline->setRibbonSegments(segments);
            }
            else
            {
                auto instances = particleSystem->getInstanceData();
                if (useMeshPipeline)
                {
                    meshPipeline->setParticleInstances(instances);
                }
                else
                {
                    pipeline->setParticleInstances(instances);
                }
            }
        }

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();

        commandBuffer.begin(vk::CommandBufferBeginInfo{});
        if (useRibbonPipeline)
        {
            ribbonPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }
        else if (useMeshPipeline)
        {
            meshPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }
        else
        {
            pipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }
        commandBuffer.end();

        vk::SubmitInfo submitInfo(
            0, nullptr, nullptr,
            1, &commandBuffer,
            0, nullptr
        );

        device.getGraphicsQueue().submit(submitInfo, inFlightFences[imageIndex]);

        return static_cast<void*>(offscreenResources.colorImages[imageIndex].descriptorSet);
    }

    void VFXPreviewController::createOffscreenResources()
    {
        uint32_t imageCount = swapChain.getImageCount();
        offscreenResources.colorImages.resize(imageCount);

        vk::Extent2D extent = swapChain.getSwapchainExtent();

        for (uint32_t i = 0; i < imageCount; i++)
        {
            core::ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
            imageInfo.width = extent.width;
            imageInfo.height = extent.height;
            imageInfo.format = swapChain.getSwapchainImageFormat();
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
            imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::ImageUtilities::createImage(imageInfo, offscreenResources.colorImages[i].colorImage,
                                               offscreenResources.colorImages[i].colorImageMemory);

            core::ImageViewInfoRequest viewInfo(device.getLogicalDevice(),
                                                 offscreenResources.colorImages[i].colorImage);
            viewInfo.format = swapChain.getSwapchainImageFormat();
            core::ImageUtilities::createImageView(viewInfo, offscreenResources.colorImages[i].colorImageView);

            updateDescriptorSets(offscreenResources.colorImages[i].descriptorSet,
                                 offscreenResources.colorImages[i].colorImageView);
        }

        core::ImageInfoRequest depthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        depthInfo.width = extent.width;
        depthInfo.height = extent.height;
        depthInfo.format = swapChain.getSwapchainDepthStencilFormat();
        depthInfo.tiling = vk::ImageTiling::eOptimal;
        depthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        depthInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::ImageUtilities::createImage(depthInfo, offscreenResources.depthImage.depthImage,
                                           offscreenResources.depthImage.depthImageMemory);

        core::ImageViewInfoRequest depthViewInfo(device.getLogicalDevice(),
                                                  offscreenResources.depthImage.depthImage);
        depthViewInfo.format = swapChain.getSwapchainDepthStencilFormat();
        depthViewInfo.aspectFlags = vk::ImageAspectFlagBits::eDepth;
        core::ImageUtilities::createImageView(depthViewInfo, offscreenResources.depthImage.depthImageView);
    }

    void VFXPreviewController::cleanupOffscreenResources()
    {
        for (auto const& resources : offscreenResources.colorImages)
        {
            device.getLogicalDevice().destroyImageView(resources.colorImageView);
            device.getLogicalDevice().destroyImage(resources.colorImage);
            device.getLogicalDevice().freeMemory(resources.colorImageMemory);
        }
        offscreenResources.colorImages.clear();

        if (offscreenResources.depthImage.depthImageView)
        {
            device.getLogicalDevice().destroyImageView(offscreenResources.depthImage.depthImageView);
            offscreenResources.depthImage.depthImageView = nullptr;
        }
        if (offscreenResources.depthImage.depthImage)
        {
            device.getLogicalDevice().destroyImage(offscreenResources.depthImage.depthImage);
            offscreenResources.depthImage.depthImage = nullptr;
        }
        if (offscreenResources.depthImage.depthImageMemory)
        {
            device.getLogicalDevice().freeMemory(offscreenResources.depthImage.depthImageMemory);
            offscreenResources.depthImage.depthImageMemory = nullptr;
        }
    }

    void VFXPreviewController::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void VFXPreviewController::updateDescriptorSets(vk::DescriptorSet& descriptorSet,
                                                     const vk::ImageView& imageView) const
    {
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}
