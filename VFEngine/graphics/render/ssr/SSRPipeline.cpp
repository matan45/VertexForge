#include "SSRPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>

namespace render::ssr
{
    SSRPipeline::SSRPipeline(core::Device& device, core::SwapChain& swapChain,
                             core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();
        depthAspectMask = vk::ImageAspectFlagBits::eDepth;
        if (depthFormat == vk::Format::eD16UnormS8Uint ||
            depthFormat == vk::Format::eD24UnormS8Uint ||
            depthFormat == vk::Format::eD32SfloatS8Uint)
        {
            depthAspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    }

    SSRPipeline::~SSRPipeline()
    {
        cleanup();
    }

    void SSRPipeline::init()
    {
        currentExtent = swapChain.getSwapchainExtent();
        traceExtent = ssrHalfResolution
            ? vk::Extent2D{std::max(currentExtent.width / 2, 1u), std::max(currentExtent.height / 2, 1u)}
            : currentExtent;

        createSampler();
        createDepthImageView();
        createIntermediateImages();
        createParamsBuffer();

        createDescriptorSetLayouts();
        createDescriptorPool();
        createDescriptorSets();
        updateDescriptorSets();

        loadShaders();

        createTracePipeline();
        createTemporalPipeline();
        createDenoisePipeline();
        createCompositePipeline();

        initialized = true;
    }

    void SSRPipeline::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupIntermediateImages();

        if (depthOnlyImageView)
        {
            dev.destroyImageView(depthOnlyImageView);
            depthOnlyImageView = nullptr;
        }

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        auto destroyLayout = [&](vk::DescriptorSetLayout& layout)
        {
            if (layout)
            {
                dev.destroyDescriptorSetLayout(layout);
                layout = nullptr;
            }
        };
        destroyLayout(traceSet0Layout);
        destroyLayout(traceSet1Layout);
        destroyLayout(temporalSet0Layout);
        destroyLayout(temporalSet1Layout);
        destroyLayout(denoiseSet0Layout);
        destroyLayout(compositeSet0Layout);

        if (paramsBuffer)
        {
            paramsBufferMapped = nullptr;
            core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferAllocation, device.getMemoryManager());
        }

        if (sampler)
        {
            dev.destroySampler(sampler);
            sampler = nullptr;
        }

        auto cleanupShader = [](std::shared_ptr<core::Shader>& s)
        {
            if (s)
            {
                s->cleanUp();
                s.reset();
            }
        };
        cleanupShader(traceShader);
        cleanupShader(temporalShader);
        cleanupShader(denoiseShader);
        cleanupShader(compositeShader);

        historyValid = false;
        initialized = false;
    }

    void SSRPipeline::recreate()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();
        currentExtent = swapChain.getSwapchainExtent();
        traceExtent = ssrHalfResolution
            ? vk::Extent2D{std::max(currentExtent.width / 2, 1u), std::max(currentExtent.height / 2, 1u)}
            : currentExtent;

        cleanupPipelines();
        cleanupIntermediateImages();

        if (depthOnlyImageView)
        {
            dev.destroyImageView(depthOnlyImageView);
            depthOnlyImageView = nullptr;
        }

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        // Descriptor set layouts are resolution-independent — no need to recreate them.
        createDepthImageView();
        createIntermediateImages();

        createDescriptorPool();
        createDescriptorSets();
        updateDescriptorSets();

        createTracePipeline();
        createTemporalPipeline();
        createDenoisePipeline();
        createCompositePipeline();

        historyValid = false;
    }

    void SSRPipeline::setCameraData(const glm::mat4& view, const glm::mat4& projection,
                                     const glm::vec3& cameraPosition,
                                     float nearPlane, float farPlane, uint32_t frameIndex)
    {
        cachedPrevViewProjection = cachedProjection * cachedView;

        cachedView = view;
        cachedProjection = projection;
        cachedInverseView = glm::inverse(view);
        cachedInverseProjection = glm::inverse(projection);
        cachedCameraPosition = cameraPosition;
        cachedNear = nearPlane;
        cachedFar = farPlane;
        cachedFrameIndex = frameIndex;
    }

    void SSRPipeline::updateSettings(const SSRSettings& settings)
    {
        ssrMaxDistance = settings.maxDistance;
        ssrIntensity = settings.intensity;
        ssrRoughnessThreshold = settings.roughnessThreshold;
        ssrEdgeFadeStart = settings.edgeFadeStart;
        ssrTemporalBlend = settings.temporalBlend;
        ssrMaxSteps = settings.maxSteps;
        ssrHalfResolution = settings.halfResolution;
    }

    void SSRPipeline::setHiZResources(vk::ImageView hiZView, vk::Sampler hiZSmplr)
    {
        hiZImageView = hiZView;
        hiZSampler = hiZSmplr;
    }

    void SSRPipeline::setNormalRoughnessResources(vk::ImageView normalRoughnessView, vk::Image normalRoughnessImg)
    {
        normalRoughnessImageView = normalRoughnessView;
        normalRoughnessImage = normalRoughnessImg;
    }

    void SSRPipeline::createSampler()
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
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void SSRPipeline::createDepthImageView()
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = offscreenResources.depthImage.depthImage;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = swapChain.getSwapchainDepthStencilFormat();
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        depthOnlyImageView = device.getLogicalDevice().createImageView(viewInfo);
    }

    void SSRPipeline::createImageAndView(vk::Image& image, core::VulkanAllocation& alloc,
                                          vk::ImageView& view, vk::Extent2D extent,
                                          vk::Format format)
    {
        core::ImageInfoRequest req(device.getLogicalDevice(), device.getPhysicalDevice());
        req.width = extent.width;
        req.height = extent.height;
        req.format = format;
        req.tiling = vk::ImageTiling::eOptimal;
        req.usage = vk::ImageUsageFlagBits::eColorAttachment
                  | vk::ImageUsageFlagBits::eSampled
                  | vk::ImageUsageFlagBits::eTransferSrc
                  | vk::ImageUsageFlagBits::eTransferDst;
        req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageUtilities::createImage(req, image, alloc, device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(device.getLogicalDevice(), image);
        viewReq.format = format;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    void SSRPipeline::destroyImageAndView(vk::Image& image, core::VulkanAllocation& alloc,
                                           vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        if (view)  { dev.destroyImageView(view); view = nullptr; }
        if (image) { dev.destroyImage(image); image = nullptr; }
        if (alloc) { device.getMemoryManager().free(alloc); alloc = {}; }
    }

    void SSRPipeline::createIntermediateImages()
    {
        createImageAndView(ssrRawImage, ssrRawAllocation, ssrRawImageView,
                           traceExtent, SSR_FORMAT);

        createImageAndView(ssrDenoiseHorizImage, ssrDenoiseHorizAllocation, ssrDenoiseHorizImageView,
                           traceExtent, SSR_FORMAT);

        createImageAndView(ssrDenoisedImage, ssrDenoisedAllocation, ssrDenoisedImageView,
                           traceExtent, SSR_FORMAT);

        for (uint32_t i = 0; i < 2; ++i)
        {
            createImageAndView(ssrHistoryImages[i], ssrHistoryAllocations[i],
                               ssrHistoryImageViews[i], traceExtent, SSR_FORMAT);
        }
    }

    void SSRPipeline::createParamsBuffer()
    {
        auto& dev = device.getLogicalDevice();

        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(SSRParamsUBO);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                          | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferAllocation, device.getMemoryManager());
        paramsBufferMapped = paramsBufferAllocation.mappedPtr;
    }

    void SSRPipeline::updateParamsBuffer()
    {
        if (!paramsBufferMapped)
            return;

        SSRParamsUBO params{};
        params.projection = cachedProjection;
        params.inverseProjection = cachedInverseProjection;
        params.view = cachedView;
        params.inverseView = cachedInverseView;
        params.prevViewProjection = cachedPrevViewProjection;
        params.params = glm::vec4(ssrMaxDistance, ssrIntensity, ssrRoughnessThreshold, ssrEdgeFadeStart);
        params.resolution = glm::vec2(static_cast<float>(traceExtent.width),
                                       static_cast<float>(traceExtent.height));
        params.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                      1.0f / static_cast<float>(traceExtent.height));
        params.nearPlane = cachedNear;
        params.farPlane = cachedFar;
        params.maxSteps = ssrMaxSteps;
        params.frameIndex = cachedFrameIndex;
        params.historyValid = historyValid ? 1u : 0u;
        params.halfResolution = ssrHalfResolution ? 1u : 0u;
        params.temporalBlend = ssrTemporalBlend;

        std::memcpy(paramsBufferMapped, &params, sizeof(SSRParamsUBO));
    }

    void SSRPipeline::cleanupIntermediateImages()
    {
        destroyImageAndView(ssrRawImage, ssrRawAllocation, ssrRawImageView);
        destroyImageAndView(ssrDenoiseHorizImage, ssrDenoiseHorizAllocation, ssrDenoiseHorizImageView);
        destroyImageAndView(ssrDenoisedImage, ssrDenoisedAllocation, ssrDenoisedImageView);

        for (uint32_t i = 0; i < 2; ++i)
        {
            destroyImageAndView(ssrHistoryImages[i], ssrHistoryAllocations[i], ssrHistoryImageViews[i]);
        }
    }

    void SSRPipeline::cleanupPipelines()
    {
        auto& dev = device.getLogicalDevice();

        auto destroyPipe = [&](vk::Pipeline& p, vk::PipelineLayout& pl)
        {
            if (p)  { dev.destroyPipeline(p); p = nullptr; }
            if (pl) { dev.destroyPipelineLayout(pl); pl = nullptr; }
        };

        destroyPipe(tracePipeline, tracePipelineLayout);
        destroyPipe(temporalPipeline, temporalPipelineLayout);
        destroyPipe(denoisePipeline, denoisePipelineLayout);
        destroyPipe(compositePipeline, compositePipelineLayout);
    }

}
