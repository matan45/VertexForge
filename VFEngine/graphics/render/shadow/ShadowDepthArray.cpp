#include "ShadowDepthArray.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace render::shadow
{
    ShadowDepthArray::ShadowDepthArray(core::Device& device)
        : device(device)
    {
    }

    ShadowDepthArray::~ShadowDepthArray()
    {
        cleanup();
    }

    ShadowDepthArray::ShadowDepthArray(ShadowDepthArray&& other) noexcept
        : device(other.device)
        , image(other.image)
        , memory(other.memory)
        , arrayView(other.arrayView)
        , layerViews(std::move(other.layerViews))
        , width(other.width)
        , height(other.height)
        , layerCount(other.layerCount)
        , format(other.format)
        , initialized(other.initialized)
        , currentLayout(other.currentLayout)
    {
        other.image = nullptr;
        other.memory = nullptr;
        other.arrayView = nullptr;
        other.initialized = false;
    }

    ShadowDepthArray& ShadowDepthArray::operator=(ShadowDepthArray&& other) noexcept
    {
        if (this != &other)
        {
            cleanup();

            image = other.image;
            memory = other.memory;
            arrayView = other.arrayView;
            layerViews = std::move(other.layerViews);
            width = other.width;
            height = other.height;
            layerCount = other.layerCount;
            format = other.format;
            initialized = other.initialized;
            currentLayout = other.currentLayout;

            other.image = nullptr;
            other.memory = nullptr;
            other.arrayView = nullptr;
            other.initialized = false;
        }
        return *this;
    }

    void ShadowDepthArray::init(uint32_t w, uint32_t h, uint32_t layers, vk::Format fmt)
    {
        if (initialized)
        {
            spdlog::warn("ShadowDepthArray::init() called when already initialized");
            return;
        }

        if (layers == 0 || layers > 8)
        {
            throw std::invalid_argument("ShadowDepthArray: layer count must be between 1 and 8");
        }

        width = w;
        height = h;
        layerCount = layers;
        format = fmt;

        createImage();
        createImageViews();

        initialized = true;
        currentLayout = vk::ImageLayout::eUndefined;

        spdlog::debug("ShadowDepthArray initialized: {}x{} with {} layers", width, height, layerCount);
    }

    void ShadowDepthArray::cleanup()
    {
        if (!initialized)
            return;

        // NOTE: Caller must ensure this resource is not in use by the GPU.
        // ShadowResourcePool handles synchronization at the pool level.
        // Do NOT add waitIdle() here - it causes frame stalls.
        const auto& logicalDevice = device.getLogicalDevice();

        // Cleanup per-layer views
        for (auto& view : layerViews)
        {
            if (view)
            {
                logicalDevice.destroyImageView(view);
                view = nullptr;
            }
        }
        layerViews.clear();

        // Cleanup array view
        if (arrayView)
        {
            logicalDevice.destroyImageView(arrayView);
            arrayView = nullptr;
        }

        // Cleanup image and memory
        if (image)
        {
            logicalDevice.destroyImage(image);
            image = nullptr;
        }
        if (memory)
        {
            logicalDevice.freeMemory(memory);
            memory = nullptr;
        }

        initialized = false;
        currentLayout = vk::ImageLayout::eUndefined;

        spdlog::debug("ShadowDepthArray cleaned up");
    }

    void ShadowDepthArray::recreate(uint32_t w, uint32_t h, uint32_t layers)
    {
        vk::Format savedFormat = format;
        cleanup();
        init(w, h, layers, savedFormat);
    }

    void ShadowDepthArray::createImage()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        core::ImageInfoRequest imageInfo(
            logicalDevice,
            physicalDevice,
            width,
            height,
            layerCount,  // Multiple layers for CSM
            1,           // mipLevels
            format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::ImageUtilities::createImage(imageInfo, image, memory);
    }

    void ShadowDepthArray::createImageViews()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Create array view (2D_ARRAY type) for shader sampling
        vk::ImageViewCreateInfo arrayViewInfo{};
        arrayViewInfo.image = image;
        arrayViewInfo.viewType = vk::ImageViewType::e2DArray;
        arrayViewInfo.format = format;
        arrayViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        arrayViewInfo.subresourceRange.baseMipLevel = 0;
        arrayViewInfo.subresourceRange.levelCount = 1;
        arrayViewInfo.subresourceRange.baseArrayLayer = 0;
        arrayViewInfo.subresourceRange.layerCount = layerCount;

        arrayView = logicalDevice.createImageView(arrayViewInfo);

        // Create per-layer 2D views for framebuffer attachment
        layerViews.resize(layerCount);
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            vk::ImageViewCreateInfo layerViewInfo{};
            layerViewInfo.image = image;
            layerViewInfo.viewType = vk::ImageViewType::e2D;
            layerViewInfo.format = format;
            layerViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            layerViewInfo.subresourceRange.baseMipLevel = 0;
            layerViewInfo.subresourceRange.levelCount = 1;
            layerViewInfo.subresourceRange.baseArrayLayer = i;
            layerViewInfo.subresourceRange.layerCount = 1;

            layerViews[i] = logicalDevice.createImageView(layerViewInfo);
        }
    }

    vk::ImageView ShadowDepthArray::getLayerView(uint32_t layer) const
    {
        if (layer >= layerViews.size())
        {
            spdlog::error("ShadowDepthArray::getLayerView() - layer {} out of range (max {})",
                          layer, layerViews.size());
            return nullptr;
        }
        return layerViews[layer];
    }

    void ShadowDepthArray::transitionToDepthAttachment(vk::CommandBuffer cmd)
    {
        transitionLayers(cmd, 0, layerCount, currentLayout, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        currentLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }

    void ShadowDepthArray::transitionToShaderRead(vk::CommandBuffer cmd)
    {
        transitionLayers(cmd, 0, layerCount, currentLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
        currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    void ShadowDepthArray::transitionLayerToDepthAttachment(vk::CommandBuffer cmd, uint32_t layer,
                                                              vk::ImageLayout assumedCurrentLayout)
    {
        if (layer >= layerCount)
        {
            spdlog::error("ShadowDepthArray::transitionLayerToDepthAttachment() - layer {} out of range", layer);
            return;
        }

        // Mark global layout as undefined since we're doing per-layer transitions
        currentLayout = vk::ImageLayout::eUndefined;

        transitionLayers(cmd, layer, 1, assumedCurrentLayout, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }

    void ShadowDepthArray::transitionLayerToShaderRead(vk::CommandBuffer cmd, uint32_t layer,
                                                        vk::ImageLayout assumedCurrentLayout)
    {
        if (layer >= layerCount)
        {
            spdlog::error("ShadowDepthArray::transitionLayerToShaderRead() - layer {} out of range", layer);
            return;
        }

        // Mark global layout as undefined since we're doing per-layer transitions
        currentLayout = vk::ImageLayout::eUndefined;

        transitionLayers(cmd, layer, 1, assumedCurrentLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    void ShadowDepthArray::transitionLayers(vk::CommandBuffer cmd, uint32_t baseLayer, uint32_t count,
                                             vk::ImageLayout oldLayout, vk::ImageLayout newLayout)
    {
        if (oldLayout == newLayout)
            return;

        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = baseLayer;
        barrier.subresourceRange.layerCount = count;

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        using enum vk::AccessFlagBits;
        using enum vk::ImageLayout;

        if (oldLayout == eUndefined && newLayout == eDepthStencilAttachmentOptimal)
        {
            barrier.srcAccessMask = eNone;
            barrier.dstAccessMask = eDepthStencilAttachmentWrite;
            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        }
        else if (oldLayout == eDepthStencilAttachmentOptimal && newLayout == eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = eDepthStencilAttachmentWrite;
            barrier.dstAccessMask = eShaderRead;
            sourceStage = vk::PipelineStageFlagBits::eLateFragmentTests;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else if (oldLayout == eShaderReadOnlyOptimal && newLayout == eDepthStencilAttachmentOptimal)
        {
            barrier.srcAccessMask = eShaderRead;
            barrier.dstAccessMask = eDepthStencilAttachmentWrite;
            sourceStage = vk::PipelineStageFlagBits::eFragmentShader;
            destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        }
        else if (oldLayout == eUndefined && newLayout == eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = eNone;
            barrier.dstAccessMask = eShaderRead;
            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else
        {
            spdlog::error("ShadowDepthArray: Unsupported layout transition from {} to {}",
                          static_cast<int>(oldLayout), static_cast<int>(newLayout));
            return;
        }

        cmd.pipelineBarrier(
            sourceStage, destinationStage,
            vk::DependencyFlags{},
            nullptr, nullptr, barrier
        );
    }
}
