#include "PostProcessPipeline.hpp"
#include "effects/ToneMappingEffect.hpp"
#include "effects/BloomEffect.hpp"
#include "effects/VignetteEffect.hpp"
#include "effects/ChromaticAberrationEffect.hpp"
#include "effects/FilmGrainEffect.hpp"
#include "effects/DepthOfFieldEffect.hpp"
#include "effects/SSAOEffect.hpp"
#include "effects/EdgeDetectionEffect.hpp"
#include "effects/AutoExposureEffect.hpp"
#include "effects/ColorGradingEffect.hpp"
#include "effects/UnderwaterEffect.hpp"
#include "effects/RainDropletsEffect.hpp"
#include "effects/PluginPostProcessEffect.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../upscaling/UpscaleManager.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include <algorithm>

namespace render::postprocess
{
    PostProcessPipeline::PostProcessPipeline(core::Device& device, core::SwapChain& swapChain,
                                             core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
    }

    PostProcessPipeline::~PostProcessPipeline()
    {
        cleanup();
    }

    void PostProcessPipeline::setSunData(const glm::vec2& screenPos, bool hasSun)
    {
        sunInfo.screenPos = screenPos;
        sunInfo.hasSun = hasSun;
    }

    void PostProcessPipeline::setCameraData(const CameraInfo& incoming)
    {
        cameraInfo.prevViewMatrix = cameraInfo.viewMatrix;
        cameraInfo.prevProjectionMatrix = cameraInfo.unjitteredProjectionMatrix;

        cameraInfo.nearPlane = incoming.nearPlane;
        cameraInfo.farPlane = incoming.farPlane;
        cameraInfo.cameraPosition = incoming.cameraPosition;
        cameraInfo.viewMatrix = incoming.viewMatrix;
        cameraInfo.projectionMatrix = incoming.projectionMatrix;
        cameraInfo.unjitteredProjectionMatrix = incoming.unjitteredProjectionMatrix;
        cameraInfo.jitterOffset = incoming.jitterOffset;
        cameraInfo.frameIndex = incoming.frameIndex;
        cameraInfo.time = incoming.time;
        cameraInfo.isUnderwater = incoming.isUnderwater;
        cameraInfo.submersionFactor = incoming.submersionFactor;
        cameraInfo.waterHeight = incoming.waterHeight;
    }

    bool PostProcessPipeline::hasEnabledEffects() const
    {
        return std::any_of(effects.begin(), effects.end(),
            [](const auto& e) { return e->isEnabled(); });
    }

    void PostProcessPipeline::execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        drainPendingOps();

        if (!hasEnabledEffects())
            return;

        if (!initialized)
            lazyInit();

        std::vector<PostProcessEffect*> activeEffects;
        for (auto& e : effects)
        {
            if (e->isEnabled() && e->isInitialized())
                activeEffects.push_back(e.get());
        }
        if (activeEffects.empty())
            return;

        auto extent = swapChain.getSwapchainExtent();

        vk::DescriptorSet currentInputDescSet = sceneDescriptorSets[imageIndex];
        PingPongTarget* currentOutput = &targetA;
        bool outputIsA = true;

        autoExposureOverride.reset();

        for (size_t i = 0; i < activeEffects.size(); ++i)
        {
            activeEffects[i]->preRecord(commandBuffer, currentInputDescSet);

            if (activeEffects[i]->getType() == ::postprocess::EffectType::AutoExposure)
            {
                autoExposureOverride = static_cast<AutoExposureEffect*>(activeEffects[i])->getComputedExposure();
            }

            if (activeEffects[i]->getType() == ::postprocess::EffectType::ToneMapping && autoExposureOverride.has_value())
            {
                static_cast<ToneMappingEffect*>(activeEffects[i])->setExposureOverride(autoExposureOverride.value());
            }

            core::ImageUtilities::transitionImageLayout(commandBuffer, currentOutput->image,
                currentOutput->currentLayout, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);
            currentOutput->currentLayout = vk::ImageLayout::eColorAttachmentOptimal;

            core::DynamicRenderingInfo dynInfo{};
            dynInfo.extent = extent;
            dynInfo.colorAttachments = {core::colorDontCare(currentOutput->imageView)};
            core::beginDynamicRendering(commandBuffer, dynInfo);

            activeEffects[i]->record(commandBuffer, currentInputDescSet);

            core::endDynamicRendering(commandBuffer);

            core::ImageUtilities::transitionImageLayout(commandBuffer, currentOutput->image,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
            currentOutput->currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            if (outputIsA)
            {
                currentInputDescSet = descriptorSetA;
                currentOutput = &targetB;
                outputIsA = false;
            }
            else
            {
                currentInputDescSet = descriptorSetB;
                currentOutput = &targetA;
                outputIsA = true;
            }
        }

        PingPongTarget* lastWritten = outputIsA ? &targetB : &targetA;

        core::ImageUtilities::transitionImageLayout(commandBuffer, lastWritten->image,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageAspectFlagBits::eColor);
        lastWritten->currentLayout = vk::ImageLayout::eTransferSrcOptimal;

        vk::Image sceneImage = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneImage,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::ImageCopy region{};
        region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.dstSubresource.layerCount = 1;
        region.extent.width = extent.width;
        region.extent.height = extent.height;
        region.extent.depth = 1;

        commandBuffer.copyImage(
            lastWritten->image, vk::ImageLayout::eTransferSrcOptimal,
            sceneImage, vk::ImageLayout::eTransferDstOptimal,
            region);

        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    void PostProcessPipeline::executePreUpscale(const vk::CommandBuffer& commandBuffer,
                                                  uint32_t imageIndex)
    {
        drainPendingOps();

        if (!hasEnabledEffects())
            return;

        if (!initialized)
            lazyInit();

        std::vector<PostProcessEffect*> activeEffects;
        for (auto& e : effects)
        {
            if (!e->isEnabled() || !e->isInitialized()) continue;
            if (!e->isPreUpscale()) continue;
            activeEffects.push_back(e.get());
        }
        if (activeEffects.empty())
            return;

        auto extent = swapChain.getSwapchainExtent();

        vk::DescriptorSet currentInputDescSet = sceneDescriptorSets[imageIndex];
        PingPongTarget* currentOutput = &targetA;
        bool outputIsA = true;

        autoExposureOverride.reset();

        for (size_t i = 0; i < activeEffects.size(); ++i)
        {
            activeEffects[i]->preRecord(commandBuffer, currentInputDescSet);

            if (activeEffects[i]->getType() == ::postprocess::EffectType::AutoExposure)
                autoExposureOverride = static_cast<AutoExposureEffect*>(activeEffects[i])->getComputedExposure();

            core::ImageUtilities::transitionImageLayout(commandBuffer, currentOutput->image,
                currentOutput->currentLayout, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);
            currentOutput->currentLayout = vk::ImageLayout::eColorAttachmentOptimal;

            core::DynamicRenderingInfo dynInfo{};
            dynInfo.extent = extent;
            dynInfo.colorAttachments = {core::colorDontCare(currentOutput->imageView)};
            core::beginDynamicRendering(commandBuffer, dynInfo);

            activeEffects[i]->record(commandBuffer, currentInputDescSet);

            core::endDynamicRendering(commandBuffer);

            core::ImageUtilities::transitionImageLayout(commandBuffer, currentOutput->image,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
            currentOutput->currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            if (outputIsA)
            {
                currentInputDescSet = descriptorSetA;
                currentOutput = &targetB;
                outputIsA = false;
            }
            else
            {
                currentInputDescSet = descriptorSetB;
                currentOutput = &targetA;
                outputIsA = true;
            }
        }

        // Copy result back to scene color image (upscaler reads from it)
        PingPongTarget* lastWritten = outputIsA ? &targetB : &targetA;

        core::ImageUtilities::transitionImageLayout(commandBuffer, lastWritten->image,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageAspectFlagBits::eColor);
        lastWritten->currentLayout = vk::ImageLayout::eTransferSrcOptimal;

        vk::Image sceneImage = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneImage,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::ImageCopy region{};
        region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.dstSubresource.layerCount = 1;
        region.extent.width = extent.width;
        region.extent.height = extent.height;
        region.extent.depth = 1;

        commandBuffer.copyImage(
            lastWritten->image, vk::ImageLayout::eTransferSrcOptimal,
            sceneImage, vk::ImageLayout::eTransferDstOptimal,
            region);

        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    void PostProcessPipeline::executePostUpscale(const vk::CommandBuffer& commandBuffer,
                                                  uint32_t imageIndex,
                                                  vk::Image sourceImage, vk::ImageView sourceView)
    {
        drainPendingOps();

        if (!initialized)
            lazyInit();

        auto displayExtent = swapChain.getDisplayExtent();
        bool hasDisplayImages = !offscreenResources.displayColorImages.empty();
        vk::Image outputImage = hasDisplayImages
            ? offscreenResources.displayColorImages[imageIndex].colorImage
            : offscreenResources.colorImages[imageIndex].colorImage;

        // Always blit upscale output (HDR R16G16B16A16) → displayTargetA (LDR swapchain format)
        // This handles format conversion and avoids descriptor set issues.
        // Use eUndefined as old layout — we overwrite the entire image, and it handles first-frame too.
        core::ImageUtilities::transitionImageLayout(commandBuffer, sourceImage,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageAspectFlagBits::eColor);
        core::ImageUtilities::transitionImageLayout(commandBuffer, displayTargetA.image,
            displayTargetA.currentLayout, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::ImageBlit blitRegion{};
        blitRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[1] = vk::Offset3D{static_cast<int32_t>(displayExtent.width),
                                                 static_cast<int32_t>(displayExtent.height), 1};
        blitRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[1] = vk::Offset3D{static_cast<int32_t>(displayExtent.width),
                                                 static_cast<int32_t>(displayExtent.height), 1};

        commandBuffer.blitImage(sourceImage, vk::ImageLayout::eTransferSrcOptimal,
                                displayTargetA.image, vk::ImageLayout::eTransferDstOptimal,
                                blitRegion, vk::Filter::eLinear);

        core::ImageUtilities::transitionImageLayout(commandBuffer, displayTargetA.image,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
        displayTargetA.currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        std::vector<PostProcessEffect*> activeEffects;
        for (auto& e : effects)
        {
            if (!e->isEnabled() || !e->isInitialized()) continue;
            if (e->isPreUpscale()) continue;
            activeEffects.push_back(e.get());
        }

        if (activeEffects.empty())
        {
            // No post-upscale effects: copy displayTargetA to output
            core::ImageUtilities::transitionImageLayout(commandBuffer, displayTargetA.image,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageAspectFlagBits::eColor);
            core::ImageUtilities::transitionImageLayout(commandBuffer, outputImage,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);

            vk::ImageCopy region{};
            region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.srcSubresource.layerCount = 1;
            region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.dstSubresource.layerCount = 1;
            region.extent.width = displayExtent.width;
            region.extent.height = displayExtent.height;
            region.extent.depth = 1;

            commandBuffer.copyImage(displayTargetA.image, vk::ImageLayout::eTransferSrcOptimal,
                                    outputImage, vk::ImageLayout::eTransferDstOptimal, region);

            // Transition both back to shader read for next frame
            core::ImageUtilities::transitionImageLayout(commandBuffer, displayTargetA.image,
                vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
            displayTargetA.currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            core::ImageUtilities::transitionImageLayout(commandBuffer, outputImage,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
            return;
        }

        // Run post-upscale effects using display-res ping-pong targets
        // Input starts from displayDescriptorSetA (displayTargetA already has the blitted data)
        vk::DescriptorSet currentInputDescSet = displayDescriptorSetA;
        PingPongTarget* currentOutput = &displayTargetB;
        bool outputIsA = false;

        for (size_t i = 0; i < activeEffects.size(); ++i)
        {
            activeEffects[i]->preRecord(commandBuffer, currentInputDescSet);

            if (activeEffects[i]->getType() == ::postprocess::EffectType::ToneMapping && autoExposureOverride.has_value())
                static_cast<ToneMappingEffect*>(activeEffects[i])->setExposureOverride(autoExposureOverride.value());

            core::ImageUtilities::transitionImageLayout(commandBuffer, currentOutput->image,
                currentOutput->currentLayout, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);
            currentOutput->currentLayout = vk::ImageLayout::eColorAttachmentOptimal;

            core::DynamicRenderingInfo dynInfo{};
            dynInfo.extent = displayExtent;
            dynInfo.colorAttachments = {core::colorDontCare(currentOutput->imageView)};
            core::beginDynamicRendering(commandBuffer, dynInfo);

            activeEffects[i]->record(commandBuffer, currentInputDescSet);

            core::endDynamicRendering(commandBuffer);

            core::ImageUtilities::transitionImageLayout(commandBuffer, currentOutput->image,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
            currentOutput->currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            if (outputIsA)
            {
                currentInputDescSet = displayDescriptorSetA;
                currentOutput = &displayTargetB;
                outputIsA = false;
            }
            else
            {
                currentInputDescSet = displayDescriptorSetB;
                currentOutput = &displayTargetA;
                outputIsA = true;
            }
        }

        // Copy final result to display color image
        PingPongTarget* lastWritten = outputIsA ? &displayTargetB : &displayTargetA;

        core::ImageUtilities::transitionImageLayout(commandBuffer, lastWritten->image,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageAspectFlagBits::eColor);
        lastWritten->currentLayout = vk::ImageLayout::eTransferSrcOptimal;
        core::ImageUtilities::transitionImageLayout(commandBuffer, outputImage,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::ImageCopy finalCopy{};
        finalCopy.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        finalCopy.srcSubresource.layerCount = 1;
        finalCopy.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        finalCopy.dstSubresource.layerCount = 1;
        finalCopy.extent.width = displayExtent.width;
        finalCopy.extent.height = displayExtent.height;
        finalCopy.extent.depth = 1;

        commandBuffer.copyImage(
            lastWritten->image, vk::ImageLayout::eTransferSrcOptimal,
            outputImage, vk::ImageLayout::eTransferDstOptimal,
            finalCopy);

        core::ImageUtilities::transitionImageLayout(commandBuffer, outputImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    void PostProcessPipeline::lazyInit()
    {
        sceneColorFormat = swapChain.getSceneColorFormat();

        createSampler();
        createDescriptorSetLayout();
        createPingPongTargets();
        createDescriptorPool();
        createDescriptorSets();

        auto renderExtent = swapChain.getSwapchainExtent();
        auto displayExtent = swapChain.getDisplayExtent();
        for (auto& effect : effects)
        {
            if (!effect->isInitialized())
            {
                auto ext = effect->isPreUpscale() ? renderExtent : displayExtent;
                effect->init(sceneColorFormat, ext);
            }
        }

        initialized = true;
    }

    void PostProcessPipeline::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        linearSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void PostProcessPipeline::createDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        inputDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void PostProcessPipeline::createPingPongTargets()
    {
        auto extent = swapChain.getSwapchainExtent();
        vk::Format format = swapChain.getSceneColorFormat();

        auto createTarget = [&](PingPongTarget& target, vk::Extent2D targetExtent)
        {
            core::ImageInfoRequest req(device.getLogicalDevice(), device.getPhysicalDevice());
            req.width = targetExtent.width;
            req.height = targetExtent.height;
            req.format = format;
            req.tiling = vk::ImageTiling::eOptimal;
            req.usage = vk::ImageUsageFlagBits::eColorAttachment
                      | vk::ImageUsageFlagBits::eSampled
                      | vk::ImageUsageFlagBits::eTransferSrc
                      | vk::ImageUsageFlagBits::eTransferDst;
            req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

            core::ImageUtilities::createImage(req, target.image, target.allocation, device.getMemoryManager());

            core::ImageViewInfoRequest viewReq(device.getLogicalDevice(), target.image);
            viewReq.format = format;
            core::ImageUtilities::createImageView(viewReq, target.imageView);
        };

        createTarget(targetA, extent);
        createTarget(targetB, extent);

        // Create display-res targets when upscaling is active
        auto* upscaleManager = device.getUpscaleManager();
        if (upscaleManager && upscaleManager->isActive())
        {
            auto displayExtent = swapChain.getDisplayExtent();
            createTarget(displayTargetA, displayExtent);
            createTarget(displayTargetB, displayExtent);
        }
    }

    void PostProcessPipeline::createDescriptorPool()
    {
        uint32_t sceneImageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        auto* upscaleManager = device.getUpscaleManager();
        bool upscaling = upscaleManager && upscaleManager->isActive();
        uint32_t totalSets = 2 + sceneImageCount + (upscaling ? 2 : 0);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = totalSets;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = totalSets;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void PostProcessPipeline::createDescriptorSets()
    {
        uint32_t sceneImageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        auto* upscaleManager = device.getUpscaleManager();
        bool upscaling = upscaleManager && upscaleManager->isActive();
        uint32_t totalSets = 2 + sceneImageCount + (upscaling ? 2 : 0);

        std::vector<vk::DescriptorSetLayout> layouts(totalSets, inputDescriptorSetLayout);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = totalSets;
        allocInfo.pSetLayouts = layouts.data();

        auto sets = device.getLogicalDevice().allocateDescriptorSets(allocInfo);

        descriptorSetA = sets[0];
        descriptorSetB = sets[1];

        sceneDescriptorSets.resize(sceneImageCount);
        for (uint32_t i = 0; i < sceneImageCount; ++i)
            sceneDescriptorSets[i] = sets[2 + i];

        updateDescriptorSet(descriptorSetA, targetA.imageView);
        updateDescriptorSet(descriptorSetB, targetB.imageView);

        for (uint32_t i = 0; i < sceneImageCount; ++i)
            updateDescriptorSet(sceneDescriptorSets[i], offscreenResources.colorImages[i].colorImageView);

        if (upscaling)
        {
            uint32_t base = 2 + sceneImageCount;
            displayDescriptorSetA = sets[base];
            displayDescriptorSetB = sets[base + 1];

            updateDescriptorSet(displayDescriptorSetA, displayTargetA.imageView);
            updateDescriptorSet(displayDescriptorSetB, displayTargetB.imageView);
        }
    }

    void PostProcessPipeline::updateDescriptorSet(vk::DescriptorSet set, vk::ImageView imageView)
    {
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = imageView;
        imageInfo.sampler = linearSampler;

        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        device.getLogicalDevice().updateDescriptorSets(write, nullptr);
    }

    void PostProcessPipeline::addEffect(std::unique_ptr<PostProcessEffect> effect)
    {
        auto* ptr = effect.get();
        effects.push_back(std::move(effect));
        sortEffects();

        if (initialized && !ptr->isInitialized())
        {
            auto ext = ptr->isPreUpscale() ? swapChain.getSwapchainExtent() : swapChain.getDisplayExtent();
            ptr->init(sceneColorFormat, ext);
        }
    }

    void PostProcessPipeline::setDeletionQueue(core::DeferredDeletionQueue* queue) { deletionQueue = queue; }

    void PostProcessPipeline::removeEffect(::postprocess::EffectType type)
    {
        auto* dq = deletionQueue ? deletionQueue : core::RenderManager::getGlobalDeletionQueue();

        for (auto it = effects.begin(); it != effects.end();)
        {
            if ((*it)->getType() == type)
            {
                if ((*it)->isInitialized() && dq)
                {
                    auto shared = std::shared_ptr<PostProcessEffect>(std::move(*it));
                    // vk::Device arg intentionally unused — effect holds its own device reference
                    dq->queueCustom([shared](vk::Device) {
                        shared->cleanup();
                    });
                }
                else if ((*it)->isInitialized())
                {
                    // No deferred deletion queue — must wait for GPU to finish
                    // before destroying resources still referenced by in-flight command buffers
                    device.getLogicalDevice().waitIdle();
                    (*it)->cleanup();
                }
                it = effects.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void PostProcessPipeline::updateSettings(const ::postprocess::PostProcessSettings& settings)
    {
        // VK-1419: cache the tone-mapping sub-settings so render textures can mirror the
        // main viewport's look via their own ToneMappingEffect.
        lastToneMapping = settings.toneMapping;
        for (auto& effect : effects)
            effect->updateParameters(settings);
    }

    // --- Plugin effects (VK-1409) -----------------------------------------------
    // The public methods run on the caller's thread (typically main, via the
    // event dispatcher) and only enqueue ops. drainPendingOps() applies them on
    // the render thread, where mutating `effects` is safe.

    plugin::PostProcessEffectHandle PostProcessPipeline::addPluginEffect(
        std::string fragmentGlsl, uint32_t priority, uint32_t paramsSize,
        bool startEnabled, std::string debugName)
    {
        uint64_t id = nextPluginEffectId.fetch_add(1, std::memory_order_relaxed);

        // Constructing the effect only stores data (no GPU work) — safe off-thread.
        auto effect = std::make_unique<PluginPostProcessEffect>(
            device, std::move(fragmentGlsl), priority, paramsSize, startEnabled,
            std::move(debugName));

        PendingOp op;
        op.kind = PendingOp::Kind::Add;
        op.id = id;
        op.effect = std::move(effect);

        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            pendingOps.push_back(std::move(op));
        }
        return plugin::PostProcessEffectHandle{id};
    }

    void PostProcessPipeline::removePluginEffect(plugin::PostProcessEffectHandle handle)
    {
        if (!handle.isValid()) return;
        PendingOp op;
        op.kind = PendingOp::Kind::Remove;
        op.id = handle.id;
        std::lock_guard<std::mutex> lk(pendingMutex);
        pendingOps.push_back(std::move(op));
    }

    void PostProcessPipeline::setPluginEffectEnabled(plugin::PostProcessEffectHandle handle, bool enabled)
    {
        if (!handle.isValid()) return;
        PendingOp op;
        op.kind = PendingOp::Kind::SetEnabled;
        op.id = handle.id;
        op.enabled = enabled;
        std::lock_guard<std::mutex> lk(pendingMutex);
        pendingOps.push_back(std::move(op));
    }

    void PostProcessPipeline::setPluginEffectParams(plugin::PostProcessEffectHandle handle,
                                                    std::vector<std::byte> params)
    {
        if (!handle.isValid()) return;
        PendingOp op;
        op.kind = PendingOp::Kind::SetParams;
        op.id = handle.id;
        op.params = std::move(params);
        std::lock_guard<std::mutex> lk(pendingMutex);
        pendingOps.push_back(std::move(op));
    }

    void PostProcessPipeline::drainPendingOps()
    {
        std::vector<PendingOp> ops;
        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            if (pendingOps.empty()) return;
            ops.swap(pendingOps);
        }

        auto* dq = deletionQueue ? deletionQueue : core::RenderManager::getGlobalDeletionQueue();

        for (auto& op : ops)
        {
            switch (op.kind)
            {
            case PendingOp::Kind::Add:
            {
                auto* ptr = op.effect.get();
                pluginEffects[op.id] = ptr;
                effects.push_back(std::move(op.effect));
                sortEffects();
                if (initialized && !ptr->isInitialized())
                {
                    auto ext = ptr->isPreUpscale() ? swapChain.getSwapchainExtent()
                                                   : swapChain.getDisplayExtent();
                    ptr->init(sceneColorFormat, ext);
                }
                break;
            }
            case PendingOp::Kind::Remove:
            {
                auto mapIt = pluginEffects.find(op.id);
                if (mapIt == pluginEffects.end()) break;
                PostProcessEffect* target = mapIt->second;
                pluginEffects.erase(mapIt);

                for (auto it = effects.begin(); it != effects.end(); ++it)
                {
                    if (it->get() != target) continue;

                    if ((*it)->isInitialized() && dq)
                    {
                        // 3-frame deferred destroy — the effect's pipeline may be
                        // referenced by in-flight command buffers.
                        auto shared = std::shared_ptr<PostProcessEffect>(std::move(*it));
                        dq->queueCustom([shared](vk::Device) { shared->cleanup(); });
                    }
                    else if ((*it)->isInitialized())
                    {
                        device.getLogicalDevice().waitIdle();
                        (*it)->cleanup();
                    }
                    effects.erase(it);
                    break;
                }
                break;
            }
            case PendingOp::Kind::SetEnabled:
            {
                auto mapIt = pluginEffects.find(op.id);
                if (mapIt != pluginEffects.end())
                    mapIt->second->setEnabledExternal(op.enabled);
                break;
            }
            case PendingOp::Kind::SetParams:
            {
                auto mapIt = pluginEffects.find(op.id);
                if (mapIt != pluginEffects.end())
                    mapIt->second->setParams(op.params.data(), op.params.size());
                break;
            }
            }
        }
    }

    void PostProcessPipeline::applySettings(const ::postprocess::PostProcessSettings& settings)
    {
        auto hasEffect = [this](::postprocess::EffectType type) -> bool
        {
            return std::any_of(effects.begin(), effects.end(),
                [type](const auto& e) { return e->getType() == type; });
        };

        auto syncEffect = [&](::postprocess::EffectType type, bool enabled,
                              auto makeEffect)
        {
            bool shouldBeActive = settings.enabled && enabled;

            if (shouldBeActive && !hasEffect(type))
            {
                addEffect(makeEffect());
            }
            else if (!shouldBeActive && hasEffect(type))
            {
                removeEffect(type);
            }
        };

        syncEffect(::postprocess::EffectType::ToneMapping, settings.toneMapping.enabled,
            [this]() { return std::make_unique<ToneMappingEffect>(device); });

        syncEffect(::postprocess::EffectType::Bloom, settings.bloom.enabled,
            [this]() { return std::make_unique<BloomEffect>(device); });

        syncEffect(::postprocess::EffectType::Vignette, settings.vignette.enabled,
            [this]() { return std::make_unique<VignetteEffect>(device); });

        syncEffect(::postprocess::EffectType::ChromaticAberration, settings.chromaticAberration.enabled,
            [this]() { return std::make_unique<ChromaticAberrationEffect>(device); });

        syncEffect(::postprocess::EffectType::FilmGrain, settings.filmGrain.enabled,
            [this]() { return std::make_unique<FilmGrainEffect>(device); });

        syncEffect(::postprocess::EffectType::DepthOfField, settings.depthOfField.enabled,
            [this]() { return std::make_unique<DepthOfFieldEffect>(device, swapChain, offscreenResources, *this); });

        syncEffect(::postprocess::EffectType::SSAO, settings.ssao.enabled,
            [this]() { return std::make_unique<SSAOEffect>(device, swapChain, offscreenResources, *this); });

        syncEffect(::postprocess::EffectType::EdgeDetection, settings.edgeDetection.enabled,
            [this]() { return std::make_unique<EdgeDetectionEffect>(device, swapChain, offscreenResources, *this); });

        syncEffect(::postprocess::EffectType::AutoExposure, settings.autoExposure.enabled,
            [this]() { return std::make_unique<AutoExposureEffect>(device, swapChain, offscreenResources, *this); });

        syncEffect(::postprocess::EffectType::ColorGrading, settings.colorGrading.enabled,
            [this]() { return std::make_unique<ColorGradingEffect>(device); });

        syncEffect(::postprocess::EffectType::Underwater, settings.underwater.enabled,
            [this]() { return std::make_unique<UnderwaterEffect>(device, *this); });

        syncEffect(::postprocess::EffectType::RainDroplets, settings.rainDroplets.enabled,
            [this]() { return std::make_unique<RainDropletsEffect>(device, *this); });

        updateSettings(settings);
    }

    void PostProcessPipeline::sortEffects()
    {
        std::sort(effects.begin(), effects.end(),
            [](const auto& a, const auto& b) { return a->getPriority() < b->getPriority(); });
    }

    void PostProcessPipeline::recreate()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        cleanupPingPongTargets();

        dev.destroyDescriptorPool(descriptorPool);
        descriptorSetA = nullptr;
        descriptorSetB = nullptr;
        displayDescriptorSetA = nullptr;
        displayDescriptorSetB = nullptr;
        sceneDescriptorSets.clear();

        sceneColorFormat = swapChain.getSceneColorFormat();

        createPingPongTargets();
        createDescriptorPool();
        createDescriptorSets();

        auto renderExtent = swapChain.getSwapchainExtent();
        auto displayExtent = swapChain.getDisplayExtent();
        for (auto& effect : effects)
        {
            if (effect->isInitialized())
            {
                auto ext = effect->isPreUpscale() ? renderExtent : displayExtent;
                effect->recreate(sceneColorFormat, ext);
            }
        }
    }

    void PostProcessPipeline::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        for (auto& effect : effects)
        {
            if (effect->isInitialized())
                effect->cleanup();
        }

        cleanupPingPongTargets();

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (inputDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(inputDescriptorSetLayout);
            inputDescriptorSetLayout = nullptr;
        }

        if (linearSampler)
        {
            dev.destroySampler(linearSampler);
            linearSampler = nullptr;
        }

        descriptorSetA = nullptr;
        descriptorSetB = nullptr;
        displayDescriptorSetA = nullptr;
        displayDescriptorSetB = nullptr;
        sceneDescriptorSets.clear();

        initialized = false;
    }

    void PostProcessPipeline::cleanupPingPongTargets()
    {
        auto& dev = device.getLogicalDevice();

        auto destroyTarget = [&](PingPongTarget& target)
        {
            if (target.imageView)
            {
                dev.destroyImageView(target.imageView);
                target.imageView = nullptr;
            }
            if (target.image)
            {
                dev.destroyImage(target.image);
                target.image = nullptr;
            }
            if (target.allocation.isValid())
            {
                device.getMemoryManager().free(target.allocation);
                target.allocation = {};
            }
        };

        destroyTarget(targetA);
        destroyTarget(targetB);
        destroyTarget(displayTargetA);
        destroyTarget(displayTargetB);
    }
}
