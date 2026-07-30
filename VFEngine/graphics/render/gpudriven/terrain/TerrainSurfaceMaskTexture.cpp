#include "TerrainSurfaceMaskTexture.hpp"

#include "../../../core/BufferUtilities.hpp"
#include "../../../core/Device.hpp"
#include "print/Log.hpp"

#include <cstring>

namespace render::gpudriven
{
    namespace
    {
        constexpr vk::Format MASK_FORMAT = vk::Format::eR8G8B8A8Unorm;
        constexpr uint32_t MASK_BYTES_PER_TEXEL = 4;

        vk::Sampler createMaskSampler(vk::Device vkDevice)
        {
            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.minFilter = vk::Filter::eLinear;
            // Clamp-to-edge matches TerrainSurfaceMaskData::sample on the CPU side, so a brush
            // read-back and the shader agree at the border.
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            return vkDevice.createSampler(samplerInfo);
        }
    }

    TerrainSurfaceMaskTexture::TerrainSurfaceMaskTexture(core::Device& device)
        : device(device)
    {
    }

    TerrainSurfaceMaskTexture::~TerrainSurfaceMaskTexture()
    {
        cleanup();
    }

    void TerrainSurfaceMaskTexture::ensureResources()
    {
        if (resourcesCreated) return;

        vk::Device vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest paramsRequest(vkDevice, device.getPhysicalDevice());
        paramsRequest.size = sizeof(TerrainWeatherMaskUBOData);
        paramsRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        paramsRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(paramsRequest, paramsBuffer, paramsAllocation,
                                            device.getMemoryManager());

        // 1x1 BLACK dummy: with no mask assigned every texel reads 0, which terrainWeatherOr turns
        // into a bitwise no-op on the global weather scalar. (The plugin world mask's dummy is white
        // because its neutral value is 1 — opposite convention, same purpose.)
        core::ImageInfoRequest dummyRequest(
            vkDevice, device.getPhysicalDevice(),
            1, 1, 1, 1,
            MASK_FORMAT,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(dummyRequest, dummyImage, dummyAllocation,
                                          device.getMemoryManager());

        core::ImageViewInfoRequest dummyViewRequest(
            vkDevice, dummyImage,
            MASK_FORMAT, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(dummyViewRequest, dummyView);

        sampler = createMaskSampler(vkDevice);

        // Upload once so the dummy is already in ShaderReadOnlyOptimal — the descriptor is written
        // eagerly at set creation and may be sampled before any mask exists.
        const uint8_t black[MASK_BYTES_PER_TEXEL] = {0, 0, 0, 255};
        core::ImageUtilities::uploadStagedPixelData(device, dummyImage, black,
                                                    MASK_BYTES_PER_TEXEL, 1, 1);

        params = {};
        writeParams();

        resourcesCreated = true;
        descriptorDirty = true;
    }

    bool TerrainSurfaceMaskTexture::createMask(uint32_t newWidth, uint32_t newHeight)
    {
        if (newWidth == 0 || newHeight == 0)
        {
            vfLogError("TerrainSurfaceMaskTexture: refusing a {}x{} mask", newWidth, newHeight);
            return false;
        }

        ensureResources();

        // Re-assigning the same-size mask is the common case (scene reload, undo); keep the image so
        // it does not churn device memory and so no descriptor rewrite is needed.
        if (maskImage && newWidth == width && newHeight == height)
            return true;

        releaseMask();

        vk::Device vkDevice = device.getLogicalDevice();
        const vk::DeviceSize byteSize =
            static_cast<vk::DeviceSize>(newWidth) * newHeight * MASK_BYTES_PER_TEXEL;

        core::ImageInfoRequest imageRequest(
            vkDevice, device.getPhysicalDevice(),
            newWidth, newHeight, 1, 1,
            MASK_FORMAT,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageRequest, maskImage, maskAllocation,
                                          device.getMemoryManager());

        core::ImageViewInfoRequest viewRequest(
            vkDevice, maskImage,
            MASK_FORMAT, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(viewRequest, maskView);

        // MAX_FRAMES_IN_FLIGHT staging slots, rotated in flushUploads. This is what makes a paint
        // stroke safe: without it, writing next frame's bytes could overwrite a region the previous
        // frame's copy has not consumed yet.
        staging.createStaging(vkDevice, device.getPhysicalDevice(), byteSize,
                              device.getMemoryManager());

        // Zero-init so the image is in ShaderReadOnlyOptimal from creation, exactly like the dummy.
        const std::vector<uint8_t> zeros(static_cast<size_t>(byteSize), 0);
        core::ImageUtilities::uploadStagedPixelData(device, maskImage, zeros.data(), byteSize,
                                                    newWidth, newHeight);

        width = newWidth;
        height = newHeight;
        descriptorDirty = true;

        vfLogInfo("TerrainSurfaceMaskTexture: created {}x{} surface mask ({} KB)",
                  width, height, byteSize / 1024);
        return true;
    }

    void TerrainSurfaceMaskTexture::queueUpload(const std::vector<uint8_t>& rgba)
    {
        if (!maskImage) return;

        const size_t expected = static_cast<size_t>(width) * height * MASK_BYTES_PER_TEXEL;
        if (rgba.size() != expected)
        {
            vfLogError("TerrainSurfaceMaskTexture: upload size mismatch — got {}, expected {}",
                       rgba.size(), expected);
            return;
        }

        pendingData = rgba;
        uploadPending = true;
    }

    void TerrainSurfaceMaskTexture::setParams(float minX, float minZ, float maxX, float maxZ,
                                              float wetnessScale, float snowScale, bool enabled)
    {
        ensureResources();

        params.worldMinX = minX;
        params.worldMinZ = minZ;
        params.worldMaxX = maxX;
        params.worldMaxZ = maxZ;
        params.wetnessScale = wetnessScale;
        params.snowScale = snowScale;
        // A degenerate rect would divide by ~0 in the shader; the shader guards with max(), but
        // disabling here means such a mask contributes nothing rather than a smear.
        const bool rectValid = (maxX - minX) > 1e-4f && (maxZ - minZ) > 1e-4f;
        params.flags = (enabled && rectValid && maskImage) ? TERRAIN_WEATHER_MASK_FLAG_ENABLED : 0u;
        writeParams();
    }

    void TerrainSurfaceMaskTexture::flushUploads(const vk::CommandBuffer& commandBuffer)
    {
        if (!uploadPending || !maskImage) return;

        staging.advance();
        staging.write(pendingData.data(), static_cast<vk::DeviceSize>(pendingData.size()));

        core::ImageUtilities::transitionImageLayout(commandBuffer, maskImage,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::BufferImageCopy region{};
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D{width, height, 1};
        commandBuffer.copyBufferToImage(staging.getBuffer(), maskImage,
                                        vk::ImageLayout::eTransferDstOptimal, region);

        core::ImageUtilities::transitionImageLayout(commandBuffer, maskImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        uploadPending = false;
    }

    bool TerrainSurfaceMaskTexture::consumeDescriptorDirty()
    {
        if (!descriptorDirty) return false;
        descriptorDirty = false;
        return true;
    }

    void TerrainSurfaceMaskTexture::writeParams()
    {
        if (paramsAllocation.mappedPtr)
            std::memcpy(paramsAllocation.mappedPtr, &params, sizeof(params));
    }

    void TerrainSurfaceMaskTexture::releaseMask()
    {
        if (!maskImage && !maskView) return;

        vk::Device vkDevice = device.getLogicalDevice();
        // Rare (mask assign / resize / scene unload) — wait so no in-flight frame samples the image.
        // Same trade PluginTextureManager::destroyTexture2D makes for the same reason.
        vkDevice.waitIdle();

        if (maskView)
        {
            vkDevice.destroyImageView(maskView);
            maskView = nullptr;
        }
        if (maskImage)
        {
            vkDevice.destroyImage(maskImage);
            maskImage = nullptr;
        }
        if (maskAllocation)
        {
            device.getMemoryManager().free(maskAllocation);
            maskAllocation = {};
        }
        staging.destroy(vkDevice, device.getMemoryManager());

        width = 0;
        height = 0;
        pendingData.clear();
        uploadPending = false;
        // The descriptor now has to fall back to the dummy, so it must be rewritten.
        descriptorDirty = true;
        params.flags = 0;
        writeParams();
    }

    void TerrainSurfaceMaskTexture::cleanup()
    {
        releaseMask();

        vk::Device vkDevice = device.getLogicalDevice();

        if (sampler)
        {
            vkDevice.destroySampler(sampler);
            sampler = nullptr;
        }
        if (dummyView)
        {
            vkDevice.destroyImageView(dummyView);
            dummyView = nullptr;
        }
        if (dummyImage)
        {
            vkDevice.destroyImage(dummyImage);
            dummyImage = nullptr;
        }
        if (dummyAllocation)
        {
            device.getMemoryManager().free(dummyAllocation);
            dummyAllocation = {};
        }
        if (paramsBuffer)
        {
            vkDevice.destroyBuffer(paramsBuffer);
            paramsBuffer = nullptr;
        }
        if (paramsAllocation)
        {
            device.getMemoryManager().free(paramsAllocation);
            paramsAllocation = {};
        }

        resourcesCreated = false;
        descriptorDirty = false;
    }
}
