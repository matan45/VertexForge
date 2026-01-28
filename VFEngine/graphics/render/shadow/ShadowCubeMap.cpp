#include "ShadowCubeMap.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "print/Logger.hpp"

namespace render::shadow
{
    ShadowCubeMap::ShadowCubeMap(core::Device& device)
        : device(device)
    {
        faceViews.fill(nullptr);
        cachedFramebuffers.fill(nullptr);
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
        , cachedFramebuffers(other.cachedFramebuffers)
        , cachedRenderPass(other.cachedRenderPass)
        , size(other.size)
        , format(other.format)
        , initialized(other.initialized)
        , currentLayout(other.currentLayout)
    {
        other.image = nullptr;
        other.memory = nullptr;
        other.cubeView = nullptr;
        other.faceViews.fill(nullptr);
        other.cachedFramebuffers.fill(nullptr);
        other.cachedRenderPass = nullptr;
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
            cachedFramebuffers = other.cachedFramebuffers;
            cachedRenderPass = other.cachedRenderPass;
            size = other.size;
            format = other.format;
            initialized = other.initialized;
            currentLayout = other.currentLayout;

            other.image = nullptr;
            other.memory = nullptr;
            other.cubeView = nullptr;
            other.faceViews.fill(nullptr);
            other.cachedFramebuffers.fill(nullptr);
            other.cachedRenderPass = nullptr;
            other.initialized = false;
        }
        return *this;
    }

    void ShadowCubeMap::init(uint32_t sz, vk::Format fmt)
    {
        if (initialized)
        {
            loggerWarning("ShadowCubeMap::init() called when already initialized");
            return;
        }

        size = sz;
        format = fmt;

        createImage();
        createImageViews();

        initialized = true;
        currentLayout = vk::ImageLayout::eUndefined;
    }

    void ShadowCubeMap::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();

        for (auto& fb : cachedFramebuffers)
        {
            if (fb)
            {
                logicalDevice.destroyFramebuffer(fb);
                fb = nullptr;
            }
        }
        cachedRenderPass = nullptr;

        for (auto& view : faceViews)
        {
            if (view)
            {
                logicalDevice.destroyImageView(view);
                view = nullptr;
            }
        }

        if (cubeView)
        {
            logicalDevice.destroyImageView(cubeView);
            cubeView = nullptr;
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
        currentLayout = vk::ImageLayout::eUndefined;
    }

    void ShadowCubeMap::createImage()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        core::ImageInfoRequest imageInfo(
            logicalDevice,
            physicalDevice,
            size,
            size,
            ShadowConstants::CUBE_FACE_COUNT,
            1,
            format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            vk::ImageCreateFlagBits::eCubeCompatible
        );

        core::ImageUtilities::createImage(imageInfo, image, memory);
    }

    void ShadowCubeMap::createImageViews()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        vk::ImageViewCreateInfo cubeViewInfo{};
        cubeViewInfo.image = image;
        cubeViewInfo.viewType = vk::ImageViewType::eCube;
        cubeViewInfo.format = format;
        cubeViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        cubeViewInfo.subresourceRange.baseMipLevel = 0;
        cubeViewInfo.subresourceRange.levelCount = 1;
        cubeViewInfo.subresourceRange.baseArrayLayer = 0;
        cubeViewInfo.subresourceRange.layerCount = ShadowConstants::CUBE_FACE_COUNT;

        cubeView = logicalDevice.createImageView(cubeViewInfo);

        for (uint32_t face = 0; face < ShadowConstants::CUBE_FACE_COUNT; ++face)
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

    ShadowCubeMap::ExtractedResources ShadowCubeMap::extractResources()
    {
        ExtractedResources extracted;
        extracted.image = image;
        extracted.memory = memory;
        extracted.cubeView = cubeView;
        extracted.faceViews = faceViews;
        extracted.framebuffers = cachedFramebuffers;

        image = nullptr;
        memory = nullptr;
        cubeView = nullptr;
        faceViews.fill(nullptr);
        cachedFramebuffers.fill(nullptr);
        cachedRenderPass = nullptr;
        initialized = false;
        currentLayout = vk::ImageLayout::eUndefined;

        return extracted;
    }

    vk::Framebuffer ShadowCubeMap::getOrCreateFramebuffer(uint32_t face, vk::RenderPass renderPass,
                                                           const vk::Device& logicalDevice)
    {
        if (face >= ShadowConstants::CUBE_FACE_COUNT)
        {
            loggerError("ShadowCubeMap::getOrCreateFramebuffer() - face {} out of range", face);
            return nullptr;
        }

        if (cachedRenderPass != renderPass)
        {
            for (auto& fb : cachedFramebuffers)
            {
                if (fb)
                {
                    logicalDevice.destroyFramebuffer(fb);
                    fb = nullptr;
                }
            }
            cachedRenderPass = renderPass;
        }

        if (cachedFramebuffers[face])
            return cachedFramebuffers[face];

        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &faceViews[face];
        fbInfo.width = size;
        fbInfo.height = size;
        fbInfo.layers = 1;

        cachedFramebuffers[face] = logicalDevice.createFramebuffer(fbInfo);
        return cachedFramebuffers[face];
    }

    void ShadowCubeMap::transitionToDepthAttachment(vk::CommandBuffer cmd)
    {
        transitionFaces(cmd, 0, ShadowConstants::CUBE_FACE_COUNT, currentLayout, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        currentLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }

    void ShadowCubeMap::transitionToShaderRead(vk::CommandBuffer cmd)
    {
        transitionFaces(cmd, 0, ShadowConstants::CUBE_FACE_COUNT, currentLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
        currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
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
            loggerError("ShadowCubeMap: Unsupported layout transition from {} to {}",
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
