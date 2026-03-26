#include "AtmospherePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>

namespace render::atmosphere
{
    AtmospherePipeline::AtmospherePipeline(core::Device& device, core::SwapChain& swapChain,
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

    AtmospherePipeline::~AtmospherePipeline()
    {
        cleanup();
    }

    void AtmospherePipeline::init()
    {
        currentExtent = swapChain.getSwapchainExtent();

        createSampler();
        createParamsBuffer();

        createTransmittanceLUT();
        createMultiScatterLUT();
        createSkyViewLUT();
        createAerialPerspectiveLUT();

        createSkyRenderer();
        createComposite();

        initialized = true;
        paramsDirty = true;
    }

    void AtmospherePipeline::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        cleanupGraphicsPipelines();
        cleanupComputePipelines();

        cleanupSkyFramebuffers();
        cleanupCompositeFramebuffers();

        compositeRenderPass = nullptr;
        if (skyRenderPass) { dev.destroyRenderPass(skyRenderPass); skyRenderPass = nullptr; }

        destroyImage(transmittanceImage, transmittanceMemory, transmittanceView);
        destroyImage(multiScatterImage, multiScatterMemory, multiScatterView);
        destroyImage(skyViewImage, skyViewMemory, skyViewView);
        destroyImage(aerialImage, aerialMemory, aerialView);

        if (depthOnlyImageView) { dev.destroyImageView(depthOnlyImageView); depthOnlyImageView = nullptr; }

        auto destroyDS = [&](vk::DescriptorPool& pool, vk::DescriptorSetLayout& layout) {
            if (pool) { dev.destroyDescriptorPool(pool); pool = nullptr; }
            if (layout) { dev.destroyDescriptorSetLayout(layout); layout = nullptr; }
        };
        destroyDS(transmittanceDSPool, transmittanceDSLayout);
        destroyDS(multiScatterDSPool, multiScatterDSLayout);
        destroyDS(skyViewDSPool, skyViewDSLayout);
        destroyDS(aerialDSPool, aerialDSLayout);
        destroyDS(skyRendererDSPool, skyRendererDSLayout);
        destroyDS(compositeDSPool, compositeDSLayout);

        if (paramsBufferMapped) { dev.unmapMemory(paramsBufferMemory); paramsBufferMapped = nullptr; }
        if (paramsBuffer) { core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferMemory); }

        if (compositeParamsMapped) { dev.unmapMemory(compositeParamsMemory); compositeParamsMapped = nullptr; }
        if (compositeParamsBuffer) { core::BufferUtilities::destroyBuffer(dev, compositeParamsBuffer, compositeParamsMemory); }

        if (lutSampler) { dev.destroySampler(lutSampler); lutSampler = nullptr; }

        auto cleanShader = [](std::shared_ptr<core::Shader>& s) { if (s) { s->cleanUp(); s.reset(); } };
        cleanShader(transmittanceShader);
        cleanShader(multiScatterShader);
        cleanShader(skyViewShader);
        cleanShader(aerialShader);
        cleanShader(skyRendererShader);
        cleanShader(compositeShader);

        initialized = false;
    }

    void AtmospherePipeline::updateSettings(const AtmosphereSettings& newSettings)
    {
        bool paramChanged = (settings.planetRadius != newSettings.planetRadius ||
                             settings.atmosphereRadius != newSettings.atmosphereRadius ||
                             settings.rayleighScattering != newSettings.rayleighScattering ||
                             settings.rayleighDensityExpScale != newSettings.rayleighDensityExpScale ||
                             settings.mieScattering != newSettings.mieScattering ||
                             settings.mieAbsorption != newSettings.mieAbsorption ||
                             settings.mieAnisotropy != newSettings.mieAnisotropy ||
                             settings.mieDensityExpScale != newSettings.mieDensityExpScale ||
                             settings.ozoneAbsorption != newSettings.ozoneAbsorption ||
                             settings.ozoneCenterAlt != newSettings.ozoneCenterAlt ||
                             settings.ozoneWidth != newSettings.ozoneWidth ||
                             settings.groundAlbedo != newSettings.groundAlbedo);

        settings = newSettings;
        enabled = newSettings.enabled;
        if (paramChanged)
            paramsDirty = true;
    }

    void AtmospherePipeline::setCameraData(const glm::mat4& view, const glm::mat4& projection,
                                            const glm::vec3& cameraPos, float nearPlane, float farPlane,
                                            float time)
    {
        cachedView = view;
        cachedProjection = projection;
        cachedCameraPos = cameraPos;
        cachedNear = nearPlane;
        cachedFar = farPlane;
        cachedTime = time;
    }

    void AtmospherePipeline::updateDayNightCycle(float deltaTime)
    {
        if (deltaTime > 0.0f && deltaTime < 1.0f)
            dayNightController.tick(deltaTime, settings);
    }

    void AtmospherePipeline::createSampler()
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        info.anisotropyEnable = VK_FALSE;
        lutSampler = device.getLogicalDevice().createSampler(info);
    }

    void AtmospherePipeline::createParamsBuffer()
    {
        auto& dev = device.getLogicalDevice();
        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(AtmosphereGPUParams);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferMemory);
        paramsBufferMapped = dev.mapMemory(paramsBufferMemory, 0, sizeof(AtmosphereGPUParams));

        core::BufferInfoRequest compositeReq(dev, device.getPhysicalDevice());
        compositeReq.size = sizeof(AtmosphereCompositeParams);
        compositeReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        compositeReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(compositeReq, compositeParamsBuffer, compositeParamsMemory);
        compositeParamsMapped = dev.mapMemory(compositeParamsMemory, 0, sizeof(AtmosphereCompositeParams));
    }

    void AtmospherePipeline::updateParamsBuffer()
    {
        if (!paramsBufferMapped) return;

        glm::vec3 sunDir = hasSunOverride
            ? glm::normalize(-sunDirectionOverride)
            : sunDirectionFromAngles(settings.sunAzimuth, settings.sunElevation);
        glm::mat4 vp = cachedProjection * cachedView;

        AtmosphereGPUParams gpu{};
        gpu.planetParams = glm::vec4(settings.planetRadius, settings.atmosphereRadius, 0.0f, 0.0f);
        gpu.rayleighScattering = glm::vec4(settings.rayleighScattering, settings.rayleighDensityExpScale);
        gpu.mieParams = glm::vec4(settings.mieScattering, settings.mieAbsorption, settings.mieAnisotropy, settings.mieDensityExpScale);
        gpu.ozoneAbsorption = glm::vec4(settings.ozoneAbsorption, settings.ozoneCenterAlt);
        gpu.ozoneParams = glm::vec4(settings.ozoneWidth, 0.0f, 0.0f, 0.0f);
        gpu.sunIrradiance = glm::vec4(settings.sunIrradiance, settings.sunAngularRadius);
        gpu.sunDirection = glm::vec4(sunDir, 0.0f);
        gpu.groundAlbedo = glm::vec4(settings.groundAlbedo, 0.0f);

        // Camera altitude above planet surface (minimum 1m to avoid degenerate LUT sampling)
        float altitude = std::max(cachedCameraPos.y, 1.0f);
        gpu.cameraPosition = glm::vec4(cachedCameraPos, altitude);

        gpu.invViewProjection = glm::inverse(vp);
        gpu.viewProjection = vp;
        gpu.screenParams = glm::vec4(cachedNear, cachedFar, settings.aerialMaxDist, settings.aerialIntensity);
        gpu.screenSize = glm::uvec4(currentExtent.width, currentExtent.height, 0, 0);

        // Moon
        glm::vec3 moonDir = sunDirectionFromAngles(settings.moonAzimuth, settings.moonElevation);
        gpu.moonDirection = glm::vec4(moonDir, settings.moonAngularRadius);
        float moonPhase = 0.5f * (1.0f - glm::dot(sunDir, moonDir));
        gpu.moonParams = glm::vec4(settings.moonBrightness, moonPhase, settings.nightSkyBrightness, 0.0f);

        // Stars (wrap time to avoid float precision loss after long sessions)
        gpu.starParams = glm::vec4(settings.starDensity, settings.starBrightness, settings.starTwinkleSpeed, std::fmod(cachedTime, 10000.0f));

        std::memcpy(paramsBufferMapped, &gpu, sizeof(gpu));
    }

    void AtmospherePipeline::destroyImage(vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        if (view) { dev.destroyImageView(view); view = nullptr; }
        if (image) { dev.destroyImage(image); image = nullptr; }
        if (memory) { dev.freeMemory(memory); memory = nullptr; }
    }

    void AtmospherePipeline::cleanupSkyFramebuffers()
    {
        auto& dev = device.getLogicalDevice();
        for (auto& fb : skyFramebuffers)
        {
            if (fb) { dev.destroyFramebuffer(fb); fb = nullptr; }
        }
        skyFramebuffers.clear();
    }

    void AtmospherePipeline::cleanupCompositeFramebuffers()
    {
        auto& dev = device.getLogicalDevice();
        for (auto& fb : compositeFramebuffers)
        {
            if (fb) { dev.destroyFramebuffer(fb); fb = nullptr; }
        }
        compositeFramebuffers.clear();
    }

    void AtmospherePipeline::cleanupComputePipelines()
    {
        auto& dev = device.getLogicalDevice();
        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& l) {
            if (p) { dev.destroyPipeline(p); p = nullptr; }
            if (l) { dev.destroyPipelineLayout(l); l = nullptr; }
        };
        destroyPipeline(transmittancePipeline, transmittancePipelineLayout);
        destroyPipeline(multiScatterPipeline, multiScatterPipelineLayout);
        destroyPipeline(skyViewPipeline, skyViewPipelineLayout);
        destroyPipeline(aerialPipeline, aerialPipelineLayout);
    }

    void AtmospherePipeline::cleanupGraphicsPipelines()
    {
        auto& dev = device.getLogicalDevice();
        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& l) {
            if (p) { dev.destroyPipeline(p); p = nullptr; }
            if (l) { dev.destroyPipelineLayout(l); l = nullptr; }
        };
        destroyPipeline(skyRendererPipeline, skyRendererPipelineLayout);
        destroyPipeline(compositePipeline, compositePipelineLayout);
    }
}
