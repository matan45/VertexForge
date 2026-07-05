#include "VTPhysicalPool.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Log.hpp"

namespace render::vt
{
    VTPhysicalPool::VTPhysicalPool(core::Device& device)
        : device(device)
    {
    }

    VTPhysicalPool::~VTPhysicalPool()
    {
        cleanup();
    }

    void VTPhysicalPool::init(const VTPoolDesc& poolDesc)
    {
        if (initialized)
            return;

        desc = poolDesc;
        poolDim = (desc.poolDim / VT_PAGE_SIZE) * VT_PAGE_SIZE;
        if (poolDim < VT_PAGE_SIZE)
            poolDim = VT_PAGE_SIZE;
        if (desc.planeFormats.empty())
            desc.planeFormats.push_back(vk::Format::eR8G8B8A8Unorm);

        createPlanes();
        createSampler();

        allocator.init(vtMaxTiles(poolDim));

        // Bring every plane to shader-read-optimal for a defined initial layout.
        {
            const auto& logicalDevice = device.getLogicalDevice();
            auto cmd = core::Utilities::beginSingleTimeCommands(logicalDevice, device.getStagingCommandPool());
            for (auto& plane : planes)
            {
                vk::ImageMemoryBarrier barrier{};
                barrier.srcAccessMask = {};
                barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eUndefined;
                barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = plane.image;
                barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;

                cmd->pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eFragmentShader,
                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            core::Utilities::endSingleTimeCommands(device, cmd, nullptr);
        }

        initialized = true;
        vfLogInfo("VTPhysicalPool: {}x{} atlas, {} plane(s), {} tiles ({}x{} each, {} border)",
                  poolDim, poolDim, planes.size(), vtMaxTiles(poolDim),
                  VT_PAGE_SIZE, VT_PAGE_SIZE, VT_BORDER);
    }

    void VTPhysicalPool::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        if (sampler)
        {
            logicalDevice.destroySampler(sampler);
            sampler = nullptr;
        }
        for (auto& plane : planes)
        {
            if (plane.view)
            {
                logicalDevice.destroyImageView(plane.view);
                plane.view = nullptr;
            }
            if (plane.image)
            {
                logicalDevice.destroyImage(plane.image);
                plane.image = nullptr;
            }
            if (plane.allocation.isValid())
            {
                device.getMemoryManager().free(plane.allocation);
                plane.allocation = {};
            }
        }
        planes.clear();
        initialized = false;
    }

    void VTPhysicalPool::createPlanes()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();
        auto& memManager = device.getMemoryManager();

        planes.reserve(desc.planeFormats.size());
        for (vk::Format fmt : desc.planeFormats)
        {
            Plane plane;
            plane.format = fmt;

            core::ImageInfoRequest imageInfo(
                logicalDevice, physicalDevice,
                poolDim, poolDim, 1, 1,
                fmt, vk::ImageTiling::eOptimal,
                desc.usage, vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(imageInfo, plane.image, plane.allocation, memManager);

            core::ImageViewInfoRequest viewInfo(
                logicalDevice, plane.image, fmt,
                vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D, 1, 1);
            core::ImageUtilities::createImageView(viewInfo, plane.view);

            planes.push_back(plane);
        }
    }

    void VTPhysicalPool::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest; // trilinear done manually via 2 page lookups
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = desc.enableAnisotropy ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = desc.enableAnisotropy ? 8.0f : 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eNever;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    float VTPhysicalPool::utilization() const
    {
        const uint32_t cap = allocator.capacityCount();
        if (cap == 0)
            return 0.0f;
        return static_cast<float>(allocator.allocatedCount()) / static_cast<float>(cap);
    }

    vk::Viewport VTPhysicalPool::getTileViewport(uint32_t tile) const
    {
        const uint32_t per = vtTilesPerSide(poolDim);
        const uint32_t tx = tile % per;
        const uint32_t ty = tile / per;
        return vk::Viewport{
            static_cast<float>(tx * VT_PAGE_SIZE),
            static_cast<float>(ty * VT_PAGE_SIZE),
            static_cast<float>(VT_PAGE_SIZE),
            static_cast<float>(VT_PAGE_SIZE),
            0.0f, 1.0f};
    }

    vk::Rect2D VTPhysicalPool::getTileScissor(uint32_t tile) const
    {
        const uint32_t per = vtTilesPerSide(poolDim);
        const uint32_t tx = tile % per;
        const uint32_t ty = tile / per;
        return vk::Rect2D{
            {static_cast<int32_t>(tx * VT_PAGE_SIZE), static_cast<int32_t>(ty * VT_PAGE_SIZE)},
            {VT_PAGE_SIZE, VT_PAGE_SIZE}};
    }

    vk::BufferImageCopy VTPhysicalPool::getTileBufferCopy(uint32_t tile, uint32_t plane,
                                                         vk::DeviceSize bufferOffset) const
    {
        (void)plane; // all planes share the same tile geometry; caller picks the destination image
        const uint32_t per = vtTilesPerSide(poolDim);
        const uint32_t tx = (tile % per) * VT_PAGE_SIZE;
        const uint32_t ty = (tile / per) * VT_PAGE_SIZE;

        vk::BufferImageCopy region{};
        region.bufferOffset = bufferOffset;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{static_cast<int32_t>(tx), static_cast<int32_t>(ty), 0};
        region.imageExtent = vk::Extent3D{VT_PAGE_SIZE, VT_PAGE_SIZE, 1};
        return region;
    }

    void VTPhysicalPool::transition(vk::CommandBuffer cmd,
                                    vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                    vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage,
                                    vk::AccessFlags srcAccess, vk::AccessFlags dstAccess) const
    {
        std::vector<vk::ImageMemoryBarrier> barriers;
        barriers.reserve(planes.size());
        for (const auto& plane : planes)
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = plane.image;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barriers.push_back(barrier);
        }
        cmd.pipelineBarrier(srcStage, dstStage, {}, 0, nullptr, 0, nullptr,
                            static_cast<uint32_t>(barriers.size()), barriers.data());
    }
}
