#include "ShadowCubeMap.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include <spdlog/spdlog.h>

namespace render::shadow
{
    ShadowCubeMap::ShadowCubeMap(core::Device& device)
        : device(device)
    {
        faceViews.fill(nullptr);
    }

    ShadowCubeMap::~ShadowCubeMap()
    {
        cleanup();
    }

    ShadowCubeMap::ShadowCubeMap(ShadowCubeMap&& other) noexcept
        : device(other.device)
        , image(other.image)
        , memory(other.memory)
        , cubeView(other.cubeView)
        , faceViews(other.faceViews)
        , size(other.size)
        , format(other.format)
        , initialized(other.initialized)
        , currentLayout(other.currentLayout)
    {
        other.image = nullptr;
        other.memory = nullptr;
        other.cubeView = nullptr;
        other.faceViews.fill(nullptr);
        other.initialized = false;
    }

    ShadowCubeMap& ShadowCubeMap::operator=(ShadowCubeMap&& other) noexcept
    {
        if (this != &other)
        {
            cleanup();

            image = other.image;
            memory = other.memory;
            cubeView = other.cubeView;
            faceViews = other.faceViews;
            size = other.size;
            format = other.format;
            initialized = other.initialized;
            currentLayout = other.currentLayout;

            other.image = nullptr;
            other.memory = nullptr;
            other.cubeView = nullptr;
            other.faceViews.fill(nullptr);
            other.initialized = false;
        }
        return *this;
    }

    void ShadowCubeMap::init(uint32_t sz, vk::Format fmt)
    {
        if (initialized)
        {
            spdlog::warn("ShadowCubeMap::init() called when already initialized");
            return;
        }

        size = sz;
        format = fmt;

        createImage();
        createImageViews();

        initialized = true;
        currentLayout = vk::ImageLayout::eUndefined;

        spdlog::debug("ShadowCubeMap initialized: {}x{} (6 faces)", size, size);
    }

    void ShadowCubeMap::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        // Cleanup per-face views
        for (auto& view : faceViews)
        {
            if (view)
            {
                logicalDevice.destroyImageView(view);
                view = nullptr;
            }
        }

        // Cleanup cube view
        if (cubeView)
        {
            logicalDevice.destroyImageView(cubeView);
            cubeView = nullptr;
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

        spdlog::debug("ShadowCubeMap cleaned up");
    }

    void ShadowCubeMap::recreate(uint32_t sz)
    {
        vk::Format savedFormat = format;
        cleanup();
        init(sz, savedFormat);
    }

    void ShadowCubeMap::createImage()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        // Create cube-compatible image with 6 layers
        core::ImageInfoRequest imageInfo(
            logicalDevice,
            physicalDevice,
            size,
            size,
            FACE_COUNT,  // 6 layers for cube faces
            1,           // mipLevels
            format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::ImageCreateFlagBits::eCubeCompatible  // Required for cube views
        );

        core::ImageUtilities::createImage(imageInfo, image, memory);
    }

    void ShadowCubeMap::createImageViews()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Create cube view for shader sampling
        vk::ImageViewCreateInfo cubeViewInfo{};
        cubeViewInfo.image = image;
        cubeViewInfo.viewType = vk::ImageViewType::eCube;
        cubeViewInfo.format = format;
        cubeViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        cubeViewInfo.subresourceRange.baseMipLevel = 0;
        cubeViewInfo.subresourceRange.levelCount = 1;
        cubeViewInfo.subresourceRange.baseArrayLayer = 0;
        cubeViewInfo.subresourceRange.layerCount = FACE_COUNT;

        cubeView = logicalDevice.createImageView(cubeViewInfo);

        // Create per-face 2D views for framebuffer attachment
        for (uint32_t face = 0; face < FACE_COUNT; ++face)
        {
            vk::ImageViewCreateInfo faceViewInfo{};
            faceViewInfo.image = image;
            faceViewInfo.viewType = vk::ImageViewType::e2D;
            faceViewInfo.format = format;
            faceViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            faceViewInfo.subresourceRange.baseMipLevel = 0;
            faceViewInfo.subresourceRange.levelCount = 1;
            faceViewInfo.subresourceRange.baseArrayLayer = face;
            faceViewInfo.subresourceRange.layerCount = 1;

            faceViews[face] = logicalDevice.createImageView(faceViewInfo);
        }
    }

    vk::ImageView ShadowCubeMap::getFaceView(uint32_t face) const
    {
        if (face >= FACE_COUNT)
        {
            spdlog::error("ShadowCubeMap::getFaceView() - face {} out of range (max 5)", face);
            return nullptr;
        }
        return faceViews[face];
    }

    void ShadowCubeMap::transitionToDepthAttachment(vk::CommandBuffer cmd)
    {
        transitionFaces(cmd, 0, FACE_COUNT, currentLayout, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        currentLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }

    void ShadowCubeMap::transitionToShaderRead(vk::CommandBuffer cmd)
    {
        transitionFaces(cmd, 0, FACE_COUNT, currentLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
        currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    void ShadowCubeMap::transitionFaceToDepthAttachment(vk::CommandBuffer cmd, uint32_t face)
    {
        if (face >= FACE_COUNT)
        {
            spdlog::error("ShadowCubeMap::transitionFaceToDepthAttachment() - face {} out of range", face);
            return;
        }
        transitionFaces(cmd, face, 1, currentLayout, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }

    void ShadowCubeMap::transitionFaceToShaderRead(vk::CommandBuffer cmd, uint32_t face)
    {
        if (face >= FACE_COUNT)
        {
            spdlog::error("ShadowCubeMap::transitionFaceToShaderRead() - face {} out of range", face);
            return;
        }
        transitionFaces(cmd, face, 1, currentLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    void ShadowCubeMap::transitionFaces(vk::CommandBuffer cmd, uint32_t baseFace, uint32_t count,
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
        barrier.subresourceRange.baseArrayLayer = baseFace;
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
            spdlog::error("ShadowCubeMap: Unsupported layout transition from {} to {}",
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
