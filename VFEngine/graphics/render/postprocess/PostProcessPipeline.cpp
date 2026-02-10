#include "PostProcessPipeline.hpp"
#include "effects/ToneMappingEffect.hpp"
#include "effects/FXAAEffect.hpp"
#include "effects/BloomEffect.hpp"
#include "effects/VignetteEffect.hpp"
#include "effects/ChromaticAberrationEffect.hpp"
#include "effects/FilmGrainEffect.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
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

    bool PostProcessPipeline::hasEnabledEffects() const
    {
        return std::any_of(effects.begin(), effects.end(),
            [](const auto& e) { return e->isEnabled(); });
    }

    void PostProcessPipeline::execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!hasEnabledEffects())
            return;

        if (!initialized)
            lazyInit();

        // Gather enabled and initialized effects
        std::vector<PostProcessEffect*> activeEffects;
        for (auto& e : effects)
        {
            if (e->isEnabled() && e->isInitialized())
                activeEffects.push_back(e.get());
        }
        if (activeEffects.empty())
            return;

        auto extent = swapChain.getSwapchainExtent();

        // Ping-pong state
        vk::DescriptorSet currentInputDescSet = sceneDescriptorSets[imageIndex];
        PingPongTarget* currentOutput = &targetA;
        bool outputIsA = true;

        for (size_t i = 0; i < activeEffects.size(); ++i)
        {
            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = renderPass;
            rpBegin.framebuffer = currentOutput->framebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = extent;

            activeEffects[i]->preRecord(commandBuffer, currentInputDescSet);

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            activeEffects[i]->record(commandBuffer, currentInputDescSet);
            commandBuffer.endRenderPass();

            // Swap for next iteration
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

        // The last written target is the opposite of currentOutput
        PingPongTarget* lastWritten = outputIsA ? &targetB : &targetA;

        // Transition lastWritten: eShaderReadOnlyOptimal -> eTransferSrcOptimal
        core::ImageUtilities::transitionImageLayout(commandBuffer, lastWritten->image,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Transition scene color: eShaderReadOnlyOptimal -> eTransferDstOptimal
        vk::Image sceneImage = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneImage,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Copy result back to scene color image
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

        // Transition scene color back: eTransferDstOptimal -> eShaderReadOnlyOptimal
        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    void PostProcessPipeline::lazyInit()
    {
        createSampler();
        createRenderPass();
        createDescriptorSetLayout();
        createPingPongTargets();
        createFramebuffers();
        createDescriptorPool();
        createDescriptorSets();

        auto extent = swapChain.getSwapchainExtent();
        for (auto& effect : effects)
        {
            if (!effect->isInitialized())
                effect->init(renderPass, extent);
        }

        initialized = true;
    }

    void PostProcessPipeline::createRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        renderPass = device.getLogicalDevice().createRenderPass(rpInfo);
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
        vk::Format format = swapChain.getSwapchainImageFormat();

        auto createTarget = [&](PingPongTarget& target)
        {
            core::ImageInfoRequest req(device.getLogicalDevice(), device.getPhysicalDevice());
            req.width = extent.width;
            req.height = extent.height;
            req.format = format;
            req.tiling = vk::ImageTiling::eOptimal;
            req.usage = vk::ImageUsageFlagBits::eColorAttachment
                      | vk::ImageUsageFlagBits::eSampled
                      | vk::ImageUsageFlagBits::eTransferSrc;
            req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

            core::ImageUtilities::createImage(req, target.image, target.memory);

            core::ImageViewInfoRequest viewReq(device.getLogicalDevice(), target.image);
            viewReq.format = format;
            core::ImageUtilities::createImageView(viewReq, target.imageView);
        };

        createTarget(targetA);
        createTarget(targetB);
    }

    void PostProcessPipeline::createFramebuffers()
    {
        auto extent = swapChain.getSwapchainExtent();

        auto createFB = [&](PingPongTarget& target)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &target.imageView;
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;

            target.framebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
        };

        createFB(targetA);
        createFB(targetB);
    }

    void PostProcessPipeline::createDescriptorPool()
    {
        uint32_t sceneImageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        uint32_t totalSets = 2 + sceneImageCount; // targetA + targetB + scene images

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
        uint32_t totalSets = 2 + sceneImageCount;

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

        // Write descriptors
        updateDescriptorSet(descriptorSetA, targetA.imageView);
        updateDescriptorSet(descriptorSetB, targetB.imageView);

        for (uint32_t i = 0; i < sceneImageCount; ++i)
            updateDescriptorSet(sceneDescriptorSets[i], offscreenResources.colorImages[i].colorImageView);
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
        effects.push_back(std::move(effect));
        sortEffects();

        if (initialized)
        {
            auto& added = effects.back();
            if (!added->isInitialized())
                added->init(renderPass, swapChain.getSwapchainExtent());
        }
    }

    void PostProcessPipeline::removeEffect(::postprocess::EffectType type)
    {
        auto it = std::remove_if(effects.begin(), effects.end(),
            [type](const auto& e) { return e->getType() == type; });

        for (auto removeIt = it; removeIt != effects.end(); ++removeIt)
        {
            if ((*removeIt)->isInitialized())
                (*removeIt)->cleanup();
        }

        effects.erase(it, effects.end());
    }

    void PostProcessPipeline::updateSettings(const ::postprocess::PostProcessSettings& settings)
    {
        for (auto& effect : effects)
            effect->updateParameters(settings);
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
            if (enabled && !hasEffect(type))
            {
                addEffect(makeEffect());
            }
            else if (!enabled && hasEffect(type))
            {
                removeEffect(type);
            }
        };

        syncEffect(::postprocess::EffectType::ToneMapping, settings.toneMapping.enabled,
            [this]() { return std::make_unique<ToneMappingEffect>(device); });

        syncEffect(::postprocess::EffectType::FXAA, settings.fxaa.enabled,
            [this]() { return std::make_unique<FXAAEffect>(device); });

        syncEffect(::postprocess::EffectType::Bloom, settings.bloom.enabled,
            [this]() { return std::make_unique<BloomEffect>(device); });

        syncEffect(::postprocess::EffectType::Vignette, settings.vignette.enabled,
            [this]() { return std::make_unique<VignetteEffect>(device); });

        syncEffect(::postprocess::EffectType::ChromaticAberration, settings.chromaticAberration.enabled,
            [this]() { return std::make_unique<ChromaticAberrationEffect>(device); });

        syncEffect(::postprocess::EffectType::FilmGrain, settings.filmGrain.enabled,
            [this]() { return std::make_unique<FilmGrainEffect>(device); });

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

        // Cleanup framebuffers and ping-pong targets
        dev.destroyFramebuffer(targetA.framebuffer);
        dev.destroyFramebuffer(targetB.framebuffer);
        cleanupPingPongTargets();

        // Cleanup descriptor pool (frees all descriptor sets)
        dev.destroyDescriptorPool(descriptorPool);
        descriptorSetA = nullptr;
        descriptorSetB = nullptr;
        sceneDescriptorSets.clear();

        // Cleanup render pass and pipeline
        dev.destroyRenderPass(renderPass);

        // Recreate
        createRenderPass();
        createPingPongTargets();
        createFramebuffers();
        createDescriptorPool();
        createDescriptorSets();

        auto extent = swapChain.getSwapchainExtent();
        for (auto& effect : effects)
        {
            if (effect->isInitialized())
                effect->recreate(renderPass, extent);
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

        dev.destroyFramebuffer(targetA.framebuffer);
        dev.destroyFramebuffer(targetB.framebuffer);

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

        if (renderPass)
        {
            dev.destroyRenderPass(renderPass);
            renderPass = nullptr;
        }

        if (linearSampler)
        {
            dev.destroySampler(linearSampler);
            linearSampler = nullptr;
        }

        descriptorSetA = nullptr;
        descriptorSetB = nullptr;
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
            if (target.memory)
            {
                dev.freeMemory(target.memory);
                target.memory = nullptr;
            }
        };

        destroyTarget(targetA);
        destroyTarget(targetB);
    }
}
