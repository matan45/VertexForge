#include "ShadowDepthArray.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "print/Log.hpp"
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
            return;
        }

        if (layers == 0 || layers > 8)
            throw std::invalid_argument("ShadowDepthArray: layer count must be between 1 and 8");

        width = w;
        height = h;
        layerCount = layers;
        format = fmt;

        createImage();
        createImageViews();

        initialized = true;
    }

    void ShadowDepthArray::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();

        for (auto& view : layerViews)
        {
            if (view)
            {
                logicalDevice.destroyImageView(view);
                view = nullptr;
            }
        }
        layerViews.clear();

        if (arrayView)
        {
            logicalDevice.destroyImageView(arrayView);
            arrayView = nullptr;
        }

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
            layerCount,
            1,
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

    ShadowDepthArray::ExtractedResources ShadowDepthArray::extractResources()
    {
        ExtractedResources extracted;
        extracted.image = image;
        extracted.memory = memory;
        extracted.arrayView = arrayView;
        extracted.layerViews = std::move(layerViews);

        image = nullptr;
        memory = nullptr;
        arrayView = nullptr;
        layerViews.clear();
        initialized = false;

        return extracted;
    }
}
