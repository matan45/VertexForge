#include "RenderTextureViewPort.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/Utilities.hpp"
#include "../core/BufferUtilities.hpp"
#include "../core/RenderManager.hpp"
#include "../core/DynamicRenderingHelpers.hpp"
#include "RenderPassHandler.hpp"
#include "IBL.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "common/CameraTypes.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/GPUDrivenCameraBuffer.hpp"
#include "gpudriven/scene/GPUCullLODPipeline.hpp"
#include "math/Frustum.hpp"
#include "print/Log.hpp"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <cstring>

namespace render
{
    RenderTextureViewPort::RenderTextureViewPort(core::Device& device, core::SwapChain& swapChain)
        : device{device}
        , swapChain{swapChain}
        , commandPool{std::make_unique<core::CommandPool>(device, swapChain)}
    {
    }

    RenderTextureViewPort::~RenderTextureViewPort()
    {
        if (commandPool)
        {
            commandPool->cleanUp();
        }
    }

    void RenderTextureViewPort::init(uint32_t w, uint32_t h)
    {
        width = w;
        height = h;

        createSampler();
        createOffscreenResources();

        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        inFlightFences.resize(swapChain.getImageCount());
        for (auto& fence : inFlightFences)
        {
            fence = device.getLogicalDevice().createFence(fenceInfo);
        }

        initialized = true;
    }

    void RenderTextureViewPort::resize(uint32_t newWidth, uint32_t newHeight)
    {
        if (newWidth == width && newHeight == height)
            return;

        device.getLogicalDevice().waitIdle();

        width = newWidth;
        height = newHeight;

        if (ImGui::GetCurrentContext())
        {
            for (auto const& resources : offscreenResources.colorImages)
            {
                if (resources.descriptorSet)
                {
                    ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
                }
            }
        }

        cleanupOffscreenResources();
        createOffscreenResources();
    }

    vk::DescriptorSet RenderTextureViewPort::render(
        RenderPassHandler* mainPassHandler,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        float nearPlane,
        float farPlane)
    {
        if (!initialized || !mainPassHandler)
        {
            return nullptr;
        }

        auto* gpuRenderer = mainPassHandler->getGPUDrivenRenderer();
        if (!gpuRenderer || !gpuRenderer->isEnabled())
        {
            return nullptr;
        }

        auto* meshPipeline = mainPassHandler->getMeshPipeline();
        if (!meshPipeline)
        {
            return nullptr;
        }

        // VK-1334: lazy-init per-RTT camera buffers + descriptor sets the first time we have a
        // RenderPassHandler in hand. They live for the viewport's lifetime; no shared CameraUBO
        // is ever mutated by RTT.
        ensurePerRTTResources(mainPassHandler);

        uint32_t imageIndex = core::RenderManager::getImageIndex();
        lastRenderedImageIndex = imageIndex;

        // VK-1334 debug
        vfLogInfo("[VK-1334][RTT] enter render() viewport={} imageIndex={} camPos=({:.2f},{:.2f},{:.2f}) size={}x{}",
                  (void*)this, imageIndex, cameraPosition.x, cameraPosition.y, cameraPosition.z, width, height);

        vk::Result result = device.getLogicalDevice().waitForFences(
            1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);
        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        // Write per-RTT mesh-pipeline CameraUBO (set 0 / binding 0 of the per-RTT IBL set).
        // Mirrors the layout of `CameraUBO` consumed by mesh / task / fragment shaders.
        {
            common::CameraUBO ubo{};
            ubo.view = view;
            ubo.projection = projection;
            ubo.cameraPos = cameraPosition;
            ubo.time = 0.0f;
            math::extractFrustumPlanes(projection * view, ubo.frustumPlanes);

            void* mapped = rttMeshCameraUBOAllocs[imageIndex].mappedPtr;
            if (mapped)
            {
                std::memcpy(mapped, &ubo, sizeof(ubo));
            }
        }

        // Enter the GPU-driven renderer's RTT scope: writes the per-RTT GPU-cull camera buffer
        // and routes subsequent cull dispatches through the per-RTT cull descriptor set.
        gpuRenderer->beginRTTContext(
            { rttGPUDrivenCameraBuffers[imageIndex].get(), rttCullDescSets[imageIndex] },
            { .view = view,
              .projection = projection,
              .cameraPosition = cameraPosition,
              .nearPlane = nearPlane,
              .farPlane = farPlane,
              .screenWidth = width,
              .screenHeight = height });

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();
        commandBuffer.begin(vk::CommandBufferBeginInfo{});

        gpuRenderer->dispatchCompute(commandBuffer);

        // Transition RTT color image from the known post-pass layout
        // (eShaderReadOnlyOptimal — set by createOffscreenResources on first frame and by
        // the end-of-pass barrier below on subsequent frames) into eColorAttachmentOptimal
        // so the dynamic-rendering passes below can write to it. Without this barrier,
        // vkCmdBeginRendering would fail validation VUID-vkCmdBeginRendering-pRenderingInfo-09592.
        core::ImageUtilities::transitionImageLayout(
            commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Phase 1: Skybox / clear pass (color-only, eClear)
        // Renders the IBL skybox if available, otherwise just clears the color image.
        auto* ibl = mainPassHandler->getIBL();
        if (ibl && ibl->isInitialized())
        {
            ibl->renderSkyboxToTarget(commandBuffer, {
                .colorImageView = offscreenResources.colorImages[imageIndex].colorImageView,
                .width = width,
                .height = height,
                .view = view,
                .projection = projection,
                .clearColor = clearColor
            });
        }
        else
        {
            // No IBL — just clear the color image via dynamic rendering
            auto colorAttach = core::colorClear(
                offscreenResources.colorImages[imageIndex].colorImageView,
                vk::ClearColorValue{std::array{clearColor.r, clearColor.g, clearColor.b, clearColor.a}});

            core::DynamicRenderingInfo clearInfo{};
            clearInfo.extent = vk::Extent2D{width, height};
            clearInfo.colorAttachments = {colorAttach};

            core::beginDynamicRendering(commandBuffer, clearInfo);
            core::endDynamicRendering(commandBuffer);
        }

        // Phase 2: Mesh render pass (eLoad color from skybox/clear, eClear depth)
        auto colorAttach = core::colorLoad(offscreenResources.colorImages[imageIndex].colorImageView);
        auto depthAttach = core::depthClear(offscreenResources.depthImage.depthImageView, 1.0f, 0);

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = vk::Extent2D{width, height};
        dynInfo.colorAttachments = {colorAttach};
        dynInfo.depthAttachment = depthAttach;

        core::beginDynamicRendering(commandBuffer, dynInfo);

        vk::Viewport viewport{0.0f, 0.0f,
                               static_cast<float>(width), static_cast<float>(height),
                               0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);

        vk::Rect2D scissor{{0, 0}, {width, height}};
        commandBuffer.setScissor(0, scissor);

        // VK-1334: use per-RTT IBL descriptor set so set 0 / binding 0 references the per-RTT
        // CameraUBO. Shaders run with RTT matrices; main pass continues to read from the main
        // descriptor set bound elsewhere.
        vk::DescriptorSet iblDescriptorSet = rttMeshIBLDescSets[imageIndex];

        gpuRenderer->renderDraw(commandBuffer, iblDescriptorSet, width, height);
        gpuRenderer->renderTransparentDraw(commandBuffer, iblDescriptorSet, width, height);
        gpuRenderer->renderBlendDraw(commandBuffer, iblDescriptorSet, width, height);

        if (gpuRenderer->isTerrainRenderingEnabled())
        {
            gpuRenderer->renderTerrainDraw(commandBuffer, iblDescriptorSet, width, height);
        }

        if (gpuRenderer->isGrassRenderingEnabled())
        {
            gpuRenderer->renderGrassDraw(commandBuffer, width, height);
        }

        if (gpuRenderer->isWaterRenderingEnabled())
        {
            gpuRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);
        }

        core::endDynamicRendering(commandBuffer);

        // Transition back to eShaderReadOnlyOptimal so UI consumers (runtime UIImage with
        // renderTextureSourceName, or the editor ImGui preview descriptor) can sample
        // immediately, and so the next frame's pre-pass barrier observes the expected
        // oldLayout regardless of whether a consumer ran this frame.
        core::ImageUtilities::transitionImageLayout(
            commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        commandBuffer.end();

        vk::SubmitInfo submitInfo(
            0, nullptr, nullptr,
            1, &commandBuffer,
            0, nullptr
        );

        device.submitGraphics(submitInfo, inFlightFences[imageIndex]);

        // VK-1334: no fence-wait-after-submit and no shared-buffer restore — RTT wrote only to
        // its own per-RTT camera buffers, so the main pass's HOST_COHERENT buffers are still
        // intact for the previous frame's main GPU work and for this frame's upcoming main pass.
        // The top-of-function fence wait remains the slot-reuse barrier for this viewport.
        gpuRenderer->endRTTContext();

        vfLogInfo("[VK-1334][RTT] submitted+endRTTContext viewport={} imageIndex={}",
                  (void*)this, imageIndex);

        return offscreenResources.colorImages[imageIndex].descriptorSet;
    }

    void RenderTextureViewPort::cleanUp()
    {
        if (!initialized)
            return;

        device.getLogicalDevice().waitIdle();

        cleanupPerRTTResources();

        commandPool->cleanUp();

        for (auto& fence : inFlightFences)
        {
            if (fence)
            {
                device.getLogicalDevice().destroyFence(fence);
            }
        }
        inFlightFences.clear();

        if (ImGui::GetCurrentContext())
        {
            for (auto const& resources : offscreenResources.colorImages)
            {
                if (resources.descriptorSet)
                {
                    ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
                }
            }
        }

        device.getLogicalDevice().destroySampler(sampler);

        cleanupOffscreenResources();

        initialized = false;
    }

    vk::ImageView RenderTextureViewPort::getLastRenderedImageView() const
    {
        if (lastRenderedImageIndex < offscreenResources.colorImages.size())
            return offscreenResources.colorImages[lastRenderedImageIndex].colorImageView;
        return {};
    }

    vk::Image RenderTextureViewPort::getLastRenderedImage() const
    {
        if (lastRenderedImageIndex < offscreenResources.colorImages.size())
            return offscreenResources.colorImages[lastRenderedImageIndex].colorImage;
        return {};
    }

    void RenderTextureViewPort::ensurePerRTTResources(RenderPassHandler* mainPassHandler)
    {
        if (perRTTResourcesInitialized || !mainPassHandler)
        {
            return;
        }

        auto* gpuRenderer = mainPassHandler->getGPUDrivenRenderer();
        auto* meshPipeline = mainPassHandler->getMeshPipeline();
        if (!gpuRenderer || !meshPipeline)
        {
            return;
        }

        const uint32_t imageCount = swapChain.getImageCount();
        const auto logicalDevice = device.getLogicalDevice();

        // --- per-image mesh-pipeline CameraUBO ---
        rttMeshCameraUBOs.resize(imageCount);
        rttMeshCameraUBOAllocs.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            core::BufferInfoRequest request(logicalDevice, device.getPhysicalDevice());
            request.size = sizeof(common::CameraUBO);
            request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request,
                rttMeshCameraUBOs[i], rttMeshCameraUBOAllocs[i], device.getMemoryManager());
        }

        // --- IBL descriptor pool sized for `imageCount` mirror sets (1 UBO + 3 image-samplers each) ---
        {
            std::array<vk::DescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
            poolSizes[0].descriptorCount = imageCount;
            poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
            poolSizes[1].descriptorCount = imageCount * 3;

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = imageCount;
            rttMeshIBLDescPool = logicalDevice.createDescriptorPool(poolInfo);
        }

        rttMeshIBLDescSets.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            rttMeshIBLDescSets[i] = meshPipeline->createExternalIBLDescriptorSet(
                rttMeshCameraUBOs[i], rttMeshIBLDescPool);
        }

        // --- per-image GPU-driven cull camera buffer + per-image cull descriptor sets ---
        rttGPUDrivenCameraBuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            rttGPUDrivenCameraBuffers[i] = std::make_unique<gpudriven::GPUDrivenCameraBuffer>(device, swapChain);
            rttGPUDrivenCameraBuffers[i]->init();
        }

        // Cull descriptor pool sized to match GPUCullLODPipeline's layout: bindings 0/2/3/4/6 are
        // storage buffers, binding 1 is a uniform buffer, binding 5 is a combined image sampler.
        {
            std::array<vk::DescriptorPoolSize, 3> poolSizes{};
            poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
            poolSizes[0].descriptorCount = imageCount * 5;
            poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
            poolSizes[1].descriptorCount = imageCount;
            poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
            poolSizes[2].descriptorCount = imageCount;

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
                             vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = imageCount;
            rttCullDescPool = logicalDevice.createDescriptorPool(poolInfo);
        }

        rttCullDescSets.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            rttCullDescSets[i] = gpuRenderer->allocateRTTCullDescriptorSet(
                rttCullDescPool, rttGPUDrivenCameraBuffers[i]->getBuffer());
        }

        rttCullDescPoolOwnerRenderer = gpuRenderer;
        perRTTResourcesInitialized = true;
    }

    void RenderTextureViewPort::cleanupPerRTTResources()
    {
        if (!perRTTResourcesInitialized)
        {
            return;
        }

        const auto logicalDevice = device.getLogicalDevice();

        // Untrack the cull descriptor sets so the cull pipeline stops rewriting them when its
        // cached buffers change. Descriptor sets themselves are freed implicitly when their pool
        // is destroyed below.
        if (rttCullDescPoolOwnerRenderer)
        {
            for (auto set : rttCullDescSets)
            {
                if (set)
                {
                    rttCullDescPoolOwnerRenderer->releaseRTTCullDescriptorSet(set);
                }
            }
        }
        rttCullDescSets.clear();
        rttCullDescPoolOwnerRenderer = nullptr;

        if (rttCullDescPool)
        {
            logicalDevice.destroyDescriptorPool(rttCullDescPool);
            rttCullDescPool = nullptr;
        }

        for (auto& buf : rttGPUDrivenCameraBuffers)
        {
            if (buf) buf->cleanup();
        }
        rttGPUDrivenCameraBuffers.clear();

        rttMeshIBLDescSets.clear();
        if (rttMeshIBLDescPool)
        {
            logicalDevice.destroyDescriptorPool(rttMeshIBLDescPool);
            rttMeshIBLDescPool = nullptr;
        }

        for (uint32_t i = 0; i < rttMeshCameraUBOs.size(); ++i)
        {
            if (rttMeshCameraUBOs[i])
            {
                core::BufferUtilities::destroyBuffer(logicalDevice,
                    rttMeshCameraUBOs[i], rttMeshCameraUBOAllocs[i], device.getMemoryManager());
            }
        }
        rttMeshCameraUBOs.clear();
        rttMeshCameraUBOAllocs.clear();

        perRTTResourcesInitialized = false;
    }

    void RenderTextureViewPort::createOffscreenResources()
    {
        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

        core::ImageInfoRequest imageColorInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageColorInfo.width = width;
        imageColorInfo.height = height;
        imageColorInfo.format = colorFormat;
        imageColorInfo.tiling = vk::ImageTiling::eOptimal;
        imageColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment
                             | vk::ImageUsageFlagBits::eSampled
                             | vk::ImageUsageFlagBits::eTransferDst
                             | vk::ImageUsageFlagBits::eTransferSrc;
        imageColorInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageInfoRequest imageDepthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageDepthInfo.width = width;
        imageDepthInfo.height = height;
        imageDepthInfo.format = depthFormat;
        imageDepthInfo.tiling = vk::ImageTiling::eOptimal;
        imageDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment
                             | vk::ImageUsageFlagBits::eSampled;
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
                                                    vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
        core::Utilities::endSingleTimeCommands(device, transitionDepthImage);

        offscreenResources.depthImage = std::move(depth);

        offscreenResources.colorImages.reserve(swapChain.getImageCount());

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

            updateDescriptorSets(color.descriptorSet, color.colorImageView);

            offscreenResources.colorImages.push_back(std::move(color));
        }
    }

    void RenderTextureViewPort::cleanupOffscreenResources()
    {
        for (auto const& resources : offscreenResources.colorImages)
        {
            device.getLogicalDevice().destroyImageView(resources.colorImageView);
            device.getLogicalDevice().destroyImage(resources.colorImage);
            device.getMemoryManager().free(resources.colorImageAllocation);
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
        if (offscreenResources.depthImage.depthImageAllocation)
        {
            device.getMemoryManager().free(offscreenResources.depthImage.depthImageAllocation);
            offscreenResources.depthImage.depthImageAllocation = {};
        }
    }

    void RenderTextureViewPort::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

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

    void RenderTextureViewPort::updateDescriptorSets(vk::DescriptorSet& descriptorSet,
                                                      const vk::ImageView& imageView) const
    {
        if (ImGui::GetCurrentContext())
        {
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
}
