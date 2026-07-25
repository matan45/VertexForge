#include "WaterShoreDepthResources.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../../utilities/water/ShoreDepthField.hpp"
#include <cstring>

namespace render::water
{
    namespace
    {
        constexpr uint32_t SHORE_RES = ::water::SHORE_FIELD_RESOLUTION;
        constexpr vk::DeviceSize SHORE_BYTES =
            static_cast<vk::DeviceSize>(SHORE_RES) * SHORE_RES * sizeof(float);
    }

    WaterShoreDepthResources::WaterShoreDepthResources(core::Device& device)
        : device(device)
    {
    }

    WaterShoreDepthResources::~WaterShoreDepthResources()
    {
        cleanup();
    }

    void WaterShoreDepthResources::init()
    {
        if (initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();

        core::ImageInfoRequest imgReq(vkDevice, device.getPhysicalDevice(),
            SHORE_RES, SHORE_RES, 1, 1,
            vk::Format::eR32Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::ImageUtilities::createImage(imgReq, image, allocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(vkDevice, image,
            vk::Format::eR32Sfloat,
            vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
        core::ImageUtilities::createImageView(viewReq, imageView);

        // Linear so the depth the vertex shader reads matches ShoreDepthField::sample's bilinear
        // filter, and clamp-to-edge so sampling outside the window is defined - the window fade is
        // what actually neutralises those border values on both sides.
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        sampler = vkDevice.createSampler(samplerInfo);

        core::BufferInfoRequest stagingReq(vkDevice, device.getPhysicalDevice());
        stagingReq.size = SHORE_BYTES;
        stagingReq.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingReq.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(stagingReq, stagingBuffer, stagingAllocation,
                                            device.getMemoryManager());
        stagingMapped = stagingAllocation.mappedPtr;

        // Seed the image with "no bottom anywhere" so the very first frames - before any bake has
        // finished - read a depth that makes every shoaling factor exactly 1. Without this the
        // undefined contents could briefly flatten the whole ocean.
        {
            std::vector<float> deep(static_cast<std::size_t>(SHORE_RES) * SHORE_RES,
                                    ::water::SHORE_FIELD_DEEP);
            core::ImageUtilities::uploadStagedPixelData(device, image, deep.data(), SHORE_BYTES,
                                                        SHORE_RES, SHORE_RES);
        }

        initialized = true;
    }

    void WaterShoreDepthResources::cleanup()
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();

        if (stagingBuffer)
        {
            stagingMapped = nullptr;
            core::BufferUtilities::destroyBuffer(vkDevice, stagingBuffer, stagingAllocation,
                                                 device.getMemoryManager());
            stagingBuffer = nullptr;
            stagingAllocation = {};
        }

        if (sampler) { vkDevice.destroySampler(sampler); sampler = nullptr; }
        if (imageView) { vkDevice.destroyImageView(imageView); imageView = nullptr; }
        if (image) { vkDevice.destroyImage(image); image = nullptr; }
        if (allocation) { device.getMemoryManager().free(allocation); allocation = {}; }

        pendingUpload = false;
        initialized = false;
    }

    void WaterShoreDepthResources::stage(const std::vector<float>& depths)
    {
        if (!initialized || !stagingMapped)
            return;
        if (depths.size() != static_cast<std::size_t>(SHORE_RES) * SHORE_RES)
            return;

        std::memcpy(stagingMapped, depths.data(), SHORE_BYTES);
        pendingUpload = true;
    }

    void WaterShoreDepthResources::recordUpload(vk::CommandBuffer cmd)
    {
        if (!initialized || !pendingUpload)
            return;

        // Hand-rolled barriers rather than ImageUtilities::transitionImageLayout: that helper's
        // shader-side stage mask is eFragmentShader only, and this texture is read by the water
        // VERTEX stage (shoaling and the breaking deformer displace geometry). A barrier that
        // names a LATER stage does not make an EARLIER one wait, so the vertex fetch of a
        // subsequent draw could race the copy.
        constexpr auto shaderStages = vk::PipelineStageFlagBits::eVertexShader |
                                      vk::PipelineStageFlagBits::eFragmentShader;

        vk::ImageMemoryBarrier barrier{};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        cmd.pipelineBarrier(shaderStages, vk::PipelineStageFlagBits::eTransfer,
                            {}, nullptr, nullptr, barrier);

        vk::BufferImageCopy region{};
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D{SHORE_RES, SHORE_RES, 1};
        cmd.copyBufferToImage(stagingBuffer, image, vk::ImageLayout::eTransferDstOptimal, region);

        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, shaderStages,
                            {}, nullptr, nullptr, barrier);

        pendingUpload = false;
    }
}
