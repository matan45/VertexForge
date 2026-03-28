#include "CloudPipeline.hpp"
#include "CloudNoise.hpp"
#include "CloudRayMarch.hpp"
#include "CloudTemporal.hpp"
#include "CloudComposite.hpp"
#include "../atmosphere/AtmospherePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::cloud
{
    CloudPipeline::CloudPipeline(core::Device& device, core::SwapChain& swapChain,
                                 core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
    }

    CloudPipeline::~CloudPipeline()
    {
        cleanup();
    }

    void CloudPipeline::createFallbackTexture()
    {
        auto& dev = device.getLogicalDevice();

        // 1x1 white RGBA16F image
        vk::ImageCreateInfo imgInfo{};
        imgInfo.imageType = vk::ImageType::e2D;
        imgInfo.extent = vk::Extent3D{1, 1, 1};
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.format = vk::Format::eR16G16B16A16Sfloat;
        imgInfo.tiling = vk::ImageTiling::eOptimal;
        imgInfo.initialLayout = vk::ImageLayout::eUndefined;
        imgInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imgInfo.samples = vk::SampleCountFlagBits::e1;
        fallbackImage = dev.createImage(imgInfo);

        auto memReqs = dev.getImageMemoryRequirements(fallbackImage);
        fallbackAllocation = device.getMemoryManager().allocate(memReqs, vk::MemoryPropertyFlagBits::eDeviceLocal);
        dev.bindImageMemory(fallbackImage, fallbackAllocation.memory, fallbackAllocation.offset);

        core::ImageViewInfoRequest viewReq(dev, fallbackImage);
        viewReq.format = vk::Format::eR16G16B16A16Sfloat;
        core::ImageUtilities::createImageView(viewReq, fallbackView);

        // Upload white pixel (1,1,1,1)
        uint16_t whitePixel[4] = {0x3C00, 0x3C00, 0x3C00, 0x3C00}; // half-float 1.0
        core::ImageUtilities::uploadStagedPixelData(device, fallbackImage,
            whitePixel, sizeof(whitePixel), 1, 1);

        // Sampler
        vk::SamplerCreateInfo sampInfo{};
        sampInfo.magFilter = vk::Filter::eLinear;
        sampInfo.minFilter = vk::Filter::eLinear;
        sampInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        sampInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        fallbackSampler = dev.createSampler(sampInfo);
    }

    void CloudPipeline::destroyFallbackTexture()
    {
        auto& dev = device.getLogicalDevice();
        if (fallbackSampler) { dev.destroySampler(fallbackSampler); fallbackSampler = nullptr; }
        if (fallbackView) { dev.destroyImageView(fallbackView); fallbackView = nullptr; }
        if (fallbackImage) { dev.destroyImage(fallbackImage); fallbackImage = nullptr; }
        if (fallbackAllocation.isValid()) { device.getMemoryManager().free(fallbackAllocation); fallbackAllocation = {}; }
    }

    void CloudPipeline::init()
    {
        // Create fallback texture for when atmosphere is not available
        createFallbackTexture();

        // Create noise textures
        cloudNoise = std::make_unique<CloudNoise>(device);
        cloudNoise->init();

        // Get transmittance LUT from atmosphere, or use fallback
        vk::ImageView transmittanceView = fallbackView;
        vk::Sampler lutSampler = fallbackSampler;
        if (atmospherePipeline && atmospherePipeline->isInitialized())
        {
            transmittanceView = atmospherePipeline->getTransmittanceView();
            lutSampler = atmospherePipeline->getLUTSampler();
        }

        // Create ray march compute pipeline
        cloudRayMarch = std::make_unique<CloudRayMarch>(device, swapChain);
        cloudRayMarch->init(
            cloudNoise->getShapeView(),
            cloudNoise->getDetailView(),
            cloudNoise->getWeatherView(),
            cloudNoise->getSampler(),
            transmittanceView,
            lutSampler,
            cloudNoise->getBlueNoiseView());

        // Create temporal reprojection
        cloudTemporal = std::make_unique<CloudTemporal>(device, swapChain);
        cloudTemporal->init(cloudRayMarch->getResultView(), cloudRayMarch->getResultImage());

        // Create composite pass
        cloudComposite = std::make_unique<CloudComposite>(device, swapChain, offscreenResources);
        cloudComposite->init(cloudTemporal->getHistoryView(), cloudNoise->getSampler());

        initialized = true;
    }

    void CloudPipeline::cleanup()
    {
        if (!initialized)
            return;

        // Wait for GPU to finish using cloud resources before destroying them
        device.getLogicalDevice().waitIdle();

        if (cloudComposite) { cloudComposite->cleanup(); cloudComposite.reset(); }
        if (cloudTemporal) { cloudTemporal->cleanup(); cloudTemporal.reset(); }
        if (cloudRayMarch) { cloudRayMarch->cleanup(); cloudRayMarch.reset(); }
        if (cloudNoise) { cloudNoise->cleanup(); cloudNoise.reset(); }

        destroyFallbackTexture();

        initialized = false;
    }

    void CloudPipeline::recreate()
    {
        if (!initialized) return;

        vk::ImageView transmittanceView = fallbackView;
        vk::Sampler lutSampler = fallbackSampler;
        if (atmospherePipeline && atmospherePipeline->isInitialized())
        {
            transmittanceView = atmospherePipeline->getTransmittanceView();
            lutSampler = atmospherePipeline->getLUTSampler();
        }

        // Noise textures don't change with swapchain resize
        cloudRayMarch->recreate(
            cloudNoise->getShapeView(),
            cloudNoise->getDetailView(),
            cloudNoise->getWeatherView(),
            cloudNoise->getSampler(),
            transmittanceView,
            lutSampler,
            cloudNoise->getBlueNoiseView());

        cloudTemporal->recreate(cloudRayMarch->getResultView(), cloudRayMarch->getResultImage());
        cloudComposite->recreate(cloudTemporal->getHistoryView(), cloudNoise->getSampler());
    }

    void CloudPipeline::updateSettings(const CloudSettings& newSettings)
    {
        bool wasEnabled = enabled;
        enabled = newSettings.enabled;
        settings = newSettings;

        if (enabled && !initialized)
        {
            init();
        }
        else if (!enabled && wasEnabled && initialized)
        {
            device.getLogicalDevice().waitIdle();
            cleanup();
        }
    }

    void CloudPipeline::setCameraData(const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos, float nearPlane, float farPlane,
                                       float time)
    {
        prevViewProjection = cachedProjection * cachedView;
        cachedView = view;
        cachedProjection = projection;
        cachedCameraPos = cameraPos;
        cachedNear = nearPlane;
        cachedFar = farPlane;
        cachedTime = time;
    }

    GPUCloudParams CloudPipeline::buildGPUParams() const
    {
        GPUCloudParams params{};

        float thickness = settings.cloudMaxAltitude - settings.cloudMinAltitude;
        float planetRadius = 6360000.0f; // default, will be overridden if atmosphere exists

        if (atmospherePipeline && atmospherePipeline->isInitialized())
        {
            auto atmosSettings = atmospherePipeline->getSettings();
            planetRadius = atmosSettings.planetRadius;
            params.atmosphereParams = glm::vec4(atmosSettings.planetRadius, atmosSettings.atmosphereRadius, 0.0f, 0.0f);
        }
        else
        {
            params.atmosphereParams = glm::vec4(0.0f);
        }

        params.cloudLayer = glm::vec4(settings.cloudMinAltitude, settings.cloudMaxAltitude, thickness, planetRadius);
        params.cloudDensity = glm::vec4(settings.globalDensity, settings.globalCoverage, 0.0f, settings.cloudType);
        params.cloudShaping = glm::vec4(settings.shapeScale, settings.detailScale, settings.erosionStrength, settings.curlStrength);

        // Wind direction from degrees
        float windRad = glm::radians(settings.windDirectionDeg);
        glm::vec3 windDir = glm::vec3(std::cos(windRad), 0.0f, std::sin(windRad));
        float timeOffset = cachedTime;
        params.windParams = glm::vec4(windDir * settings.windSpeed, timeOffset);

        params.lightParams = glm::vec4(settings.lightAbsorption, settings.phaseForward, settings.phaseBackward, settings.phaseBlend);
        params.lightColor = glm::vec4(sunIrradiance, settings.ambientIntensity);
        params.sunDirection = glm::vec4(glm::normalize(sunDirection), 0.0f);
        params.cameraPosition = glm::vec4(cachedCameraPos, 0.0f);
        params.invViewProjection = glm::inverse(cachedProjection * cachedView);
        params.prevViewProjection = prevViewProjection;

        auto extent = swapChain.getSwapchainExtent();
        params.screenParams = glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height),
                                         cachedNear, cachedFar);
        params.temporalParams = glm::vec4(settings.temporalBlendFactor, static_cast<float>(frameIndex % 16), 0.0f, 0.0f);
        params.marchParams = glm::vec4(static_cast<float>(settings.maxMarchSteps),
                                        static_cast<float>(settings.lightMarchSteps), 0.0f, 0.0f);
        params.cloudColorTint = glm::vec4(settings.cloudColorTint, 0.0f);
        params.lightParams2 = glm::vec4(settings.silverLiningIntensity, settings.silverLiningSpread,
                                         settings.multiScatterBoost, 0.0f);

        return params;
    }

    void CloudPipeline::dispatchCompute(const vk::CommandBuffer& cmd)
    {
        if (!initialized || !enabled)
            return;

        // Generate noise textures on first frame
        if (!cloudNoise->isGenerated())
        {
            cloudNoise->generate(cmd);
        }

        // Update GPU params
        GPUCloudParams params = buildGPUParams();
        cloudRayMarch->updateParams(params);

        // Dispatch ray march
        cloudRayMarch->dispatch(cmd);

        // Temporal reprojection
        CloudTemporalUBO temporalParams{};
        temporalParams.invViewProjection = params.invViewProjection;
        temporalParams.prevViewProjection = params.prevViewProjection;
        auto halfExtent = cloudRayMarch->getHalfExtent();
        temporalParams.screenParams = glm::vec4(
            static_cast<float>(halfExtent.width),
            static_cast<float>(halfExtent.height),
            cachedNear, cachedFar);
        temporalParams.temporalParams = params.temporalParams;
        cloudTemporal->updateParams(temporalParams);
        cloudTemporal->dispatch(cmd);

        frameIndex++;
    }

    void CloudPipeline::renderComposite(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        if (!initialized || !enabled)
            return;

        CloudCompositePushConstants pc{};
        pc.nearPlane = cachedNear;
        pc.farPlane = cachedFar;
        pc.cloudMinAlt = settings.cloudMinAltitude;
        pc.cloudMaxAlt = settings.cloudMaxAltitude;

        cloudComposite->render(cmd, imageIndex, pc);
    }
}
