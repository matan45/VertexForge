#include "VSMPhysicalTilePool.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Log.hpp"

namespace render::shadow
{
    VSMPhysicalTilePool::VSMPhysicalTilePool(core::Device& device)
        : device(device)
    {
    }

    VSMPhysicalTilePool::~VSMPhysicalTilePool()
    {
        cleanup();
    }

    void VSMPhysicalTilePool::init()
    {
        if (initialized)
            return;

        createPoolImage();
        createSamplers();
        createRenderPasses();
        createFramebuffers();

        // Initialize free list with all tiles
        freeTiles.resize(vsm::MAX_PHYSICAL_TILES);
        for (uint32_t i = 0; i < vsm::MAX_PHYSICAL_TILES; ++i)
            freeTiles[i] = vsm::MAX_PHYSICAL_TILES - 1 - i; // reverse so pop_back gives lowest index first

        // Transition pool image to shader-read-optimal for initial binding
        {
            const auto& logicalDevice = device.getLogicalDevice();
            auto cmd = core::Utilities::beginSingleTimeCommands(logicalDevice, device.getStagingCommandPool());

            vk::ImageMemoryBarrier barrier{};
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = poolImage;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            cmd->pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eFragmentShader,
                {},
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd, nullptr);
        }

        initialized = true;
        vfLogInfo("VSMPhysicalTilePool: Initialized {}x{} pool with {} tiles ({}x{} each)",
                  vsm::PHYSICAL_POOL_DIM, vsm::PHYSICAL_POOL_DIM,
                  vsm::MAX_PHYSICAL_TILES, vsm::PAGE_SIZE, vsm::PAGE_SIZE);
    }

    void VSMPhysicalTilePool::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        if (framebuffer)
        {
            logicalDevice.destroyFramebuffer(framebuffer);
            framebuffer = nullptr;
        }
        if (framebufferLoad)
        {
            logicalDevice.destroyFramebuffer(framebufferLoad);
            framebufferLoad = nullptr;
        }

        if (renderPass)
        {
            logicalDevice.destroyRenderPass(renderPass);
            renderPass = nullptr;
        }
        if (renderPassLoad)
        {
            logicalDevice.destroyRenderPass(renderPassLoad);
            renderPassLoad = nullptr;
        }

        if (comparisonSampler)
        {
            logicalDevice.destroySampler(comparisonSampler);
            comparisonSampler = nullptr;
        }
        if (depthSampler)
        {
            logicalDevice.destroySampler(depthSampler);
            depthSampler = nullptr;
        }

        if (poolImageView)
        {
            logicalDevice.destroyImageView(poolImageView);
            poolImageView = nullptr;
        }
        if (poolImage)
        {
            logicalDevice.destroyImage(poolImage);
            poolImage = nullptr;
        }
        if (poolMemory)
        {
            logicalDevice.freeMemory(poolMemory);
            poolMemory = nullptr;
        }

        freeTiles.clear();
        initialized = false;
    }

    void VSMPhysicalTilePool::createPoolImage()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        core::ImageInfoRequest imageInfo(
            logicalDevice,
            physicalDevice,
            vsm::PHYSICAL_POOL_DIM,
            vsm::PHYSICAL_POOL_DIM,
            1,
            1,
            depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::ImageUtilities::createImage(imageInfo, poolImage, poolMemory);

        core::ImageViewInfoRequest viewInfo(
            logicalDevice,
            poolImage,
            depthFormat,
            vk::ImageAspectFlagBits::eDepth,
            vk::ImageViewType::e2D,
            1,
            1
        );

        core::ImageUtilities::createImageView(viewInfo, poolImageView);
    }

    void VSMPhysicalTilePool::createSamplers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = vk::CompareOp::eLessOrEqual;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        comparisonSampler = logicalDevice.createSampler(samplerInfo);

        // Depth sampler for PCSS blocker search (nearest, no comparison)
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eNever;

        depthSampler = logicalDevice.createSampler(samplerInfo);
    }

    vk::RenderPass VSMPhysicalTilePool::createSingleRenderPass(vk::AttachmentLoadOp loadOp)
    {
        const auto& logicalDevice = device.getLogicalDevice();

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = loadOp;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 0;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<vk::SubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[0].dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                         vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
        dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &depthAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        return logicalDevice.createRenderPass(renderPassInfo);
    }

    void VSMPhysicalTilePool::createRenderPasses()
    {
        renderPass = createSingleRenderPass(vk::AttachmentLoadOp::eClear);
        renderPassLoad = createSingleRenderPass(vk::AttachmentLoadOp::eLoad);
    }

    void VSMPhysicalTilePool::createFramebuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &poolImageView;
        fbInfo.width = vsm::PHYSICAL_POOL_DIM;
        fbInfo.height = vsm::PHYSICAL_POOL_DIM;
        fbInfo.layers = 1;

        framebuffer = logicalDevice.createFramebuffer(fbInfo);

        fbInfo.renderPass = renderPassLoad;
        framebufferLoad = logicalDevice.createFramebuffer(fbInfo);
    }

    uint32_t VSMPhysicalTilePool::allocateTile()
    {
        if (freeTiles.empty())
        {
            vfLogWarning("VSMPhysicalTilePool: No free tiles available");
            return vsm::INVALID_TILE;
        }

        uint32_t tile = freeTiles.back();
        freeTiles.pop_back();
        return tile;
    }

    void VSMPhysicalTilePool::freeTile(uint32_t tileIndex)
    {
        if (tileIndex >= vsm::MAX_PHYSICAL_TILES)
            return;

        freeTiles.push_back(tileIndex);
    }

    void VSMPhysicalTilePool::freeAllTiles()
    {
        freeTiles.resize(vsm::MAX_PHYSICAL_TILES);
        for (uint32_t i = 0; i < vsm::MAX_PHYSICAL_TILES; ++i)
            freeTiles[i] = vsm::MAX_PHYSICAL_TILES - 1 - i;
    }

    vk::Viewport VSMPhysicalTilePool::getTileViewport(uint32_t tileIndex) const
    {
        uint32_t tileX = tileIndex % vsm::TILES_PER_SIDE;
        uint32_t tileY = tileIndex / vsm::TILES_PER_SIDE;

        return vk::Viewport{
            static_cast<float>(tileX * vsm::PAGE_SIZE),
            static_cast<float>(tileY * vsm::PAGE_SIZE),
            static_cast<float>(vsm::PAGE_SIZE),
            static_cast<float>(vsm::PAGE_SIZE),
            0.0f,
            1.0f
        };
    }

    vk::Rect2D VSMPhysicalTilePool::getTileScissor(uint32_t tileIndex) const
    {
        uint32_t tileX = tileIndex % vsm::TILES_PER_SIDE;
        uint32_t tileY = tileIndex / vsm::TILES_PER_SIDE;

        return vk::Rect2D{
            {static_cast<int32_t>(tileX * vsm::PAGE_SIZE), static_cast<int32_t>(tileY * vsm::PAGE_SIZE)},
            {vsm::PAGE_SIZE, vsm::PAGE_SIZE}
        };
    }

    float VSMPhysicalTilePool::getUtilization() const
    {
        return 1.0f - static_cast<float>(freeTiles.size()) / static_cast<float>(vsm::MAX_PHYSICAL_TILES);
    }

    vk::ImageCopy VSMPhysicalTilePool::getTileCopyRegion(uint32_t srcTileIndex, uint32_t dstTileIndex) const
    {
        uint32_t srcX = (srcTileIndex % vsm::TILES_PER_SIDE) * vsm::PAGE_SIZE;
        uint32_t srcY = (srcTileIndex / vsm::TILES_PER_SIDE) * vsm::PAGE_SIZE;
        uint32_t dstX = (dstTileIndex % vsm::TILES_PER_SIDE) * vsm::PAGE_SIZE;
        uint32_t dstY = (dstTileIndex / vsm::TILES_PER_SIDE) * vsm::PAGE_SIZE;

        vk::ImageCopy region{};
        region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcOffset = vk::Offset3D{static_cast<int32_t>(srcX), static_cast<int32_t>(srcY), 0};
        region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstOffset = vk::Offset3D{static_cast<int32_t>(dstX), static_cast<int32_t>(dstY), 0};
        region.extent = vk::Extent3D{vsm::PAGE_SIZE, vsm::PAGE_SIZE, 1};
        return region;
    }

    void VSMPhysicalTilePool::transitionPoolToTransfer(vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool)
    {
        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
        barrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tilePool->poolImage;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::PipelineStageFlagBits::eTransfer,
            {},
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
    }

    void VSMPhysicalTilePool::transitionPoolFromTransfer(vk::CommandBuffer cmd, VSMPhysicalTilePool* tilePool)
    {
        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tilePool->poolImage;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {},
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
    }
}
