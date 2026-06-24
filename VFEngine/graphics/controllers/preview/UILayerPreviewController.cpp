#include "UILayerPreviewController.hpp"
#include "../../core/VulkanContext.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/CommandPool.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../render/ClearColor.hpp"
#include "../../render/ui/UIRenderPipeline.hpp"
#include "../../render/ui/UITextPipeline.hpp"
#include "../../render/text/TextFontCache.hpp"
#include "../offscreen/UIFrameBuilder.hpp"
#include "../offscreen/UICommon.hpp" // ui_common::isEffectivelyActiveWithin + re-exported UIRectMath
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <array>
#include <functional>
#include <vector>

namespace controllers
{
    namespace
    {
        // Orders the inline clear's color writes before the first UI record's colorLoad. Two
        // separate dynamic-rendering instances writing the same attachment have no implicit
        // dependency; the image stays COLOR_ATTACHMENT_OPTIMAL (memory/execution barrier only).
        void barrierColorWriteToLoad(const vk::CommandBuffer& commandBuffer, vk::Image colorImage)
        {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite |
                                    vk::AccessFlagBits2::eColorAttachmentRead;
            barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            barrier.image = colorImage;
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            vk::DependencyInfo depInfo{};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;
            commandBuffer.pipelineBarrier2KHR(depInfo);
        }
    } // anonymous namespace

    UILayerPreviewController::UILayerPreviewController()
        : device{*core::VulkanContext::getDevice()}
          , swapChain{*core::VulkanContext::getSwapChain()}
          , commandPool{std::make_unique<core::CommandPool>(device, swapChain)}
    {
    }

    UILayerPreviewController::~UILayerPreviewController()
    {
        device.getLogicalDevice().waitIdle();
        cleanUp();
    }

    void UILayerPreviewController::init()
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

            // ClearColor is constructed against our OffscreenResources but we only use it to hold
            // the (transparent) clear value — its recordCommandBuffer expects a depth image, which
            // a UI-only target intentionally lacks, so render() clears the color inline instead.
            clearColor = std::make_unique<render::ClearColor>(device, swapChain, *offscreenResources);
            clearColor->init();
            clearColor->setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)); // transparent backdrop

            // Own a self-contained font cache for the text pipeline (see header note).
            ownedFontCache = std::make_unique<render::text::TextFontCache>(device);
            ownedFontCache->init();
            fontCache = ownedFontCache.get();

            uiPipeline = std::make_unique<render::ui::UIRenderPipeline>(device, swapChain, *offscreenResources);
            uiPipeline->init();
            uiPipeline->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());

            textPipeline = std::make_unique<render::ui::UITextPipeline>(device, swapChain, *offscreenResources, *fontCache);
            textPipeline->init();
            textPipeline->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());

            frameBuilder = std::make_unique<controllers::offscreen::UIFrameBuilder>();

            initialized = true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to initialize UILayerPreviewController: {}", e.what());
            cleanUp();
        }
    }

    void UILayerPreviewController::cleanUp()
    {
        device.getLogicalDevice().waitIdle();

        built = false;
        rootEntity = entt::null;

        frameBuilder.reset();

        if (textPipeline)
        {
            textPipeline->cleanUp();
            textPipeline.reset();
        }
        if (uiPipeline)
        {
            uiPipeline->cleanUp();
            uiPipeline.reset();
        }

        if (ownedFontCache)
        {
            ownedFontCache->cleanUp();
            ownedFontCache.reset();
        }
        fontCache = nullptr;

        if (clearColor)
        {
            clearColor->cleanUp();
            clearColor.reset();
        }

        for (auto& fence : inFlightFences)
        {
            if (fence)
                device.getLogicalDevice().destroyFence(fence);
        }
        inFlightFences.clear();

        // Drop ImGui descriptor sets BEFORE destroying the images they reference.
        if (offscreenResources)
        {
            for (auto const& resources : offscreenResources->colorImages)
            {
                if (resources.descriptorSet)
                    ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }

        if (sampler)
        {
            device.getLogicalDevice().destroySampler(sampler);
            sampler = nullptr;
        }

        cleanupOffscreenResources();
        offscreenResources.reset(); // pipelines (holders of the reference) are already destroyed

        if (commandPool)
            commandPool->cleanUp();

        initialized = false;
    }

    void UILayerPreviewController::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void UILayerPreviewController::createOffscreenResources()
    {
        // Create the OffscreenResources object ONCE; the owned pipelines + ClearColor bind a
        // reference to it at construction, so a reference-resolution change must rebuild the images
        // IN PLACE (cleanupOffscreenResources keeps the object). Only final cleanUp resets the ptr.
        if (!offscreenResources)
            offscreenResources = std::make_unique<core::OffscreenResources>();

        const vk::Format colorFormat = swapChain.getSceneColorFormat();

        // --- UI stencil image (eS8Uint), sized to the reference extent. The UI pipelines clear/
        // load it via offscreenResources.uiStencilImage; a UI-only target shares no stencil with
        // the main pass (different extent, concurrent use). Mirrors OffScreenViewPort. ---
        {
            core::ImageInfoRequest stencilInfo(device.getLogicalDevice(), device.getPhysicalDevice());
            stencilInfo.width = refExtent.width;
            stencilInfo.height = refExtent.height;
            stencilInfo.format = vk::Format::eS8Uint;
            stencilInfo.tiling = vk::ImageTiling::eOptimal;
            stencilInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
            stencilInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

            core::StencilImage stencil;
            core::ImageUtilities::createImage(stencilInfo, stencil.stencilImage,
                                              stencil.stencilImageAllocation, device.getMemoryManager());

            core::ImageViewInfoRequest stencilViewRequest(device.getLogicalDevice(), stencil.stencilImage);
            stencilViewRequest.format = vk::Format::eS8Uint;
            stencilViewRequest.aspectFlags = vk::ImageAspectFlagBits::eStencil;
            core::ImageUtilities::createImageView(stencilViewRequest, stencil.stencilImageView);

            vk::UniqueCommandBuffer transitionStencilImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(transitionStencilImage.get(), stencil.stencilImage,
                                                        vk::ImageLayout::eUndefined,
                                                        vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                                        vk::ImageAspectFlagBits::eStencil);
            core::Utilities::endSingleTimeCommands(device, transitionStencilImage);

            offscreenResources->uiStencilImage = std::move(stencil);
        }

        // --- Color images (one per swapchain image), display-resolution unused (kept empty so the
        // UI records target colorImages). Each ends in SHADER_READ_ONLY_OPTIMAL ready to sample. ---
        core::ImageInfoRequest imageColorInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageColorInfo.width = refExtent.width;
        imageColorInfo.height = refExtent.height;
        imageColorInfo.format = colorFormat;
        imageColorInfo.tiling = vk::ImageTiling::eOptimal;
        imageColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        imageColorInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        offscreenResources->colorImages.reserve(swapChain.getImageCount());
        for (size_t i = 0; i < swapChain.getImageCount(); ++i)
        {
            core::ColorImage color;
            core::ImageUtilities::createImage(imageColorInfo, color.colorImage,
                                              color.colorImageAllocation, device.getMemoryManager());

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

    void UILayerPreviewController::cleanupOffscreenResources()
    {
        // Destroys the GPU images IN PLACE but keeps the OffscreenResources object alive (the
        // pipelines hold a reference to it). Final teardown resets the unique_ptr in cleanUp().
        if (!offscreenResources) return;

        for (auto const& resources : offscreenResources->colorImages)
        {
            device.getLogicalDevice().destroyImageView(resources.colorImageView);
            device.getLogicalDevice().destroyImage(resources.colorImage);
            device.getMemoryManager().free(resources.colorImageAllocation);
        }
        offscreenResources->colorImages.clear();

        if (offscreenResources->uiStencilImage.stencilImageView)
            device.getLogicalDevice().destroyImageView(offscreenResources->uiStencilImage.stencilImageView);
        if (offscreenResources->uiStencilImage.stencilImage)
            device.getLogicalDevice().destroyImage(offscreenResources->uiStencilImage.stencilImage);
        if (offscreenResources->uiStencilImage.stencilImageAllocation)
        {
            device.getMemoryManager().free(offscreenResources->uiStencilImage.stencilImageAllocation);
            offscreenResources->uiStencilImage.stencilImageAllocation = {};
        }
        offscreenResources->uiStencilImage.stencilImageView = nullptr;
        offscreenResources->uiStencilImage.stencilImage = nullptr;
    }

    void UILayerPreviewController::updateDescriptorSet(vk::DescriptorSet& descriptorSet,
                                                       const vk::ImageView& imageView) const
    {
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    bool UILayerPreviewController::buildFromCanvas(entt::entity canvasRoot, uint32_t refWidth, uint32_t refHeight)
    {
        const uint32_t w = std::max(1u, refWidth);
        const uint32_t h = std::max(1u, refHeight);

        // Seed refExtent before the first init so the initial offscreen is created at the requested
        // size (avoids an immediate create-then-recreate when the canvas res != the default).
        if (!initialized)
        {
            refExtent = vk::Extent2D{w, h};
            init();
            if (!initialized)
            {
                built = false;
                rootEntity = entt::null;
                return false; // init failed (logged in init)
            }
        }
        else if (w != refExtent.width || h != refExtent.height)
        {
            setReferenceResolution(w, h);
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(canvasRoot) ||
            !registry.all_of<components::UICanvasComponent>(canvasRoot))
        {
            vfLogWarning("UILayerPreviewController::buildFromCanvas: entity is not a valid UICanvasComponent root");
            built = false;
            rootEntity = entt::null;
            return false;
        }

        rootEntity = canvasRoot;
        built = true;
        return true;
    }

    void UILayerPreviewController::setReferenceResolution(uint32_t refWidth, uint32_t refHeight)
    {
        const uint32_t w = std::max(1u, refWidth);
        const uint32_t h = std::max(1u, refHeight);
        if (initialized && w == refExtent.width && h == refExtent.height)
            return;

        refExtent = vk::Extent2D{w, h};

        if (!initialized)
        {
            init();
            return;
        }

        // Recreate the offscreen color/stencil at the new extent. Wait idle so no in-flight submit
        // references the old images, then drop the ImGui descriptor sets before destroying images.
        device.getLogicalDevice().waitIdle();

        if (offscreenResources)
        {
            for (auto const& resources : offscreenResources->colorImages)
            {
                if (resources.descriptorSet)
                    ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }
        cleanupOffscreenResources();
        createOffscreenResources();
    }

    void* UILayerPreviewController::render()
    {
        if (!initialized || !built)
            return nullptr;

        uint32_t imageIndex = core::RenderManager::getImageIndex();
        if (imageIndex >= offscreenResources->colorImages.size())
            return nullptr;

        // Wait for this image's previous submit before touching its command buffer and the shared
        // UI instance/text buffers it referenced (single mapped buffers, not per-image), then write
        // the new draw lists. Matches the per-image fence discipline of the other preview controllers.
        vk::Result result = device.getLogicalDevice().waitForFences(
            1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);
        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        // Build the scoped screen-space draw lists for the bound subtree at the reference extent.
        controllers::offscreen::UICanvasDrawLists lists;
        frameBuilder->prepareUICanvasScoped(rootEntity, refExtent, lists);

        uiPipeline->setUIImageDrawList(lists.images);
        textPipeline->setUITextDrawList(lists.labels);

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();
        commandBuffer.begin(vk::CommandBufferBeginInfo{});

        auto& colorImage = offscreenResources->colorImages[imageIndex];

        // Color SHADER_READ_ONLY -> COLOR_ATTACHMENT for the clear + UI records.
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            colorImage.colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Inline clear to a transparent backdrop (the UI records use colorLoad). Color-only — the
        // UI target has no depth.
        {
            const glm::vec4 c = clearColor->getClearColor();
            auto colorAttach = core::colorClear(colorImage.colorImageView,
                vk::ClearColorValue(std::array<float, 4>{c.r, c.g, c.b, c.a}));
            core::DynamicRenderingInfo info{};
            info.extent = refExtent;
            info.colorAttachments = {colorAttach};
            core::beginDynamicRendering(commandBuffer, info);
            core::endDynamicRendering(commandBuffer);
        }

        // Order the clear's writes before the first UI record's load.
        barrierColorWriteToLoad(commandBuffer, colorImage.colorImage);

        // Main UI pass: images then text, then the overlay layer (tooltips, windows) on top.
        uiPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, refExtent, false);
        textPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, refExtent, false);
        uiPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, refExtent, true);
        textPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, refExtent, true);

        // COLOR_ATTACHMENT -> SHADER_READ_ONLY so ImGui can sample the result this frame.
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            colorImage.colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        commandBuffer.end();

        vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &commandBuffer, 0, nullptr);
        device.submitGraphics(submitInfo, inFlightFences[imageIndex]);

        return static_cast<void*>(colorImage.descriptorSet);
    }

    entt::entity UILayerPreviewController::pickElementAt(glm::vec2 refPx) const
    {
        if (!built || rootEntity == entt::null)
            return entt::null;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(rootEntity) ||
            !registry.all_of<components::UICanvasComponent>(rootEntity))
            return entt::null;

        const float vw = static_cast<float>(refExtent.width);
        const float vh = static_cast<float>(refExtent.height);
        const auto& canvas = registry.get<components::UICanvasComponent>(rootEntity);
        const float scale = utilities::ui::computeCanvasScale(&canvas, vw, vh);

        // Top-most (last-drawn) wins: depth-first traversal records parents before children and
        // earlier siblings before later, so iterate the collected rects in reverse.
        std::vector<entt::entity> hits;

        std::function<void(entt::entity)> collect = [&](entt::entity e)
        {
            if (!registry.valid(e))
                return;
            // Scope-active: treat the (intentionally inactive) sandbox root as active.
            if (e != rootEntity &&
                !controllers::offscreen::ui_common::isEffectivelyActiveWithin(registry, e, rootEntity))
                return;

            if (e != rootEntity && registry.all_of<components::UIRectComponent>(e))
            {
                const auto& rectComp = registry.get<components::UIRectComponent>(e);
                utilities::ui::PixelRect rect = utilities::ui::resolvePixelRect(rectComp, vw, vh, scale);
                if (refPx.x >= rect.x && refPx.x <= rect.x + rect.w &&
                    refPx.y >= rect.y && refPx.y <= rect.y + rect.h)
                    hits.push_back(e);
            }

            if (registry.all_of<components::ChildrenComponent>(e))
            {
                for (auto child : registry.get<components::ChildrenComponent>(e).children)
                    collect(child);
            }
        };
        collect(rootEntity);

        return hits.empty() ? entt::null : hits.back();
    }

    std::optional<glm::vec4> UILayerPreviewController::resolvedRect(entt::entity entity) const
    {
        if (!built || rootEntity == entt::null)
            return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(entity) ||
            !registry.all_of<components::UIRectComponent>(entity) ||
            !registry.all_of<components::UICanvasComponent>(rootEntity))
            return std::nullopt;

        const float vw = static_cast<float>(refExtent.width);
        const float vh = static_cast<float>(refExtent.height);
        const auto& canvas = registry.get<components::UICanvasComponent>(rootEntity);
        const float scale = utilities::ui::computeCanvasScale(&canvas, vw, vh);

        const auto& rectComp = registry.get<components::UIRectComponent>(entity);
        utilities::ui::PixelRect rect = utilities::ui::resolvePixelRect(rectComp, vw, vh, scale);
        return glm::vec4(rect.x, rect.y, rect.w, rect.h);
    }
}
