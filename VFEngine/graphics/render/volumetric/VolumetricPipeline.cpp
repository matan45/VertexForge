#include "VolumetricPipeline.hpp"
#include "../../core/Device.hpp"
#include "print/Logger.hpp"

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::volumetric
{
    VolumetricPipeline::VolumetricPipeline(core::Device& device)
        : device(device)
    {
    }

    VolumetricPipeline::~VolumetricPipeline()
    {
        cleanup();
    }

    void VolumetricPipeline::init(
        VolumetricQuality quality,
        vk::DescriptorSetLayout clusterGridLayout,
        vk::DescriptorSetLayout lightBufferLayout,
        vk::DescriptorSetLayout lightCullingLayout)
    {
        if (initialized)
        {
            loggerWarning("VolumetricPipeline: Already initialized");
            return;
        }

        // Create grid manager (3D images, descriptors, UBO)
        gridManager = std::make_unique<VolumetricGridManager>(device);
        gridManager->init(quality);

        const auto& dims = gridManager->getDimensions();
        auto gridDescLayout = gridManager->getDescriptorSetLayout();

        // Create compute passes
        lightInjection = std::make_unique<VolumetricLightInjection>(device);
        lightInjection->init(dims, gridDescLayout, clusterGridLayout, lightBufferLayout, lightCullingLayout);

        temporalFilter = std::make_unique<VolumetricTemporalFilter>(device);
        temporalFilter->init(dims, gridDescLayout);

        rayMarch = std::make_unique<VolumetricRayMarch>(device);
        rayMarch->init(dims, gridDescLayout);

        initialized = true;
        loggerInfo("VolumetricPipeline: Initialized with quality {} ({}x{}x{})",
                   static_cast<int>(quality), dims.width, dims.height, dims.depth);
    }

    void VolumetricPipeline::cleanup()
    {
        if (!initialized)
            return;

        if (rayMarch)
        {
            rayMarch->cleanup();
            rayMarch.reset();
        }

        if (temporalFilter)
        {
            temporalFilter->cleanup();
            temporalFilter.reset();
        }

        if (lightInjection)
        {
            lightInjection->cleanup();
            lightInjection.reset();
        }

        if (gridManager)
        {
            gridManager->cleanup();
            gridManager.reset();
        }

        initialized = false;
        frameIndex = 0;
        prevViewProjection = glm::mat4{1.0f};
        loggerInfo("VolumetricPipeline: Cleaned up");
    }

    void VolumetricPipeline::recreate(
        VolumetricQuality quality,
        vk::DescriptorSetLayout clusterGridLayout,
        vk::DescriptorSetLayout lightBufferLayout,
        vk::DescriptorSetLayout lightCullingLayout)
    {
        cleanup();
        init(quality, clusterGridLayout, lightBufferLayout, lightCullingLayout);
    }

    void VolumetricPipeline::update(
        const glm::mat4& viewProj, const glm::mat4& invViewProj,
        const glm::vec3& cameraPos, float nearPlane, float farPlane,
        const ::postprocess::VolumetricFogSettings& settings)
    {
        if (!initialized || !gridManager)
            return;

        const auto& dims = gridManager->getDimensions();

        GPUVolumetricParams params{};
        params.gridDimensions = glm::uvec4(dims.width, dims.height, dims.depth, 0);
        params.depthParams = glm::vec4(nearPlane, farPlane, glm::log(farPlane / nearPlane), 1.0f / glm::log(farPlane / nearPlane));
        params.invViewProjection = invViewProj;
        params.prevViewProjection = prevViewProjection;
        params.fogParams = glm::vec4(
            settings.uniformDensity,
            settings.heightFogDensity,
            settings.heightFogFalloff,
            settings.heightFogOffset);
        params.scatterParams = glm::vec4(
            settings.scatteringCoefficient,
            settings.absorptionCoefficient,
            settings.anisotropy,
            settings.maxDistance);
        params.fogColor = glm::vec4(
            settings.fogColor[0],
            settings.fogColor[1],
            settings.fogColor[2],
            settings.intensity);
        params.ambientParams = glm::vec4(
            settings.ambientIntensity,
            settings.temporalBlendFactor,
            static_cast<float>(frameIndex),
            0.0f);
        params.cameraPosition = glm::vec4(cameraPos, 0.0f);

        gridManager->updateParams(params);
        gridManager->swapHistory();

        // Save current view-projection for next frame's temporal reprojection
        prevViewProjection = viewProj;
        frameIndex++;
    }

    void VolumetricPipeline::dispatch(
        vk::CommandBuffer cmd,
        vk::DescriptorSet clusterGridDescSet,
        vk::DescriptorSet lightBufferDescSet,
        vk::DescriptorSet lightCullingDescSet)
    {
        if (!initialized || !enabled)
            return;

        auto gridDescSet = gridManager->getDescriptorSet();

        // Transition all volumetric images to eGeneral for compute access
        {
            std::array<vk::ImageMemoryBarrier, 4> imageBarriers{};

            // Scattering image
            imageBarriers[0].srcAccessMask = {};
            imageBarriers[0].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            imageBarriers[0].oldLayout = vk::ImageLayout::eUndefined;
            imageBarriers[0].newLayout = vk::ImageLayout::eGeneral;
            imageBarriers[0].image = gridManager->getScatteringImage();
            imageBarriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            // Current history (write target)
            imageBarriers[1].srcAccessMask = {};
            imageBarriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            imageBarriers[1].oldLayout = vk::ImageLayout::eUndefined;
            imageBarriers[1].newLayout = vk::ImageLayout::eGeneral;
            imageBarriers[1].image = gridManager->getCurrentHistoryImage();
            imageBarriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            // Previous history (read source)
            imageBarriers[2].srcAccessMask = {};
            imageBarriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;
            imageBarriers[2].oldLayout = vk::ImageLayout::eUndefined;
            imageBarriers[2].newLayout = vk::ImageLayout::eGeneral;
            imageBarriers[2].image = gridManager->getPreviousHistoryImage();
            imageBarriers[2].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            // Integrated output
            imageBarriers[3].srcAccessMask = {};
            imageBarriers[3].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            imageBarriers[3].oldLayout = vk::ImageLayout::eUndefined;
            imageBarriers[3].newLayout = vk::ImageLayout::eGeneral;
            imageBarriers[3].image = gridManager->getIntegratedImage();
            imageBarriers[3].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                {}, {},
                imageBarriers);
        }

        // Pass 1: Light Injection
        lightInjection->dispatch(cmd, gridDescSet, clusterGridDescSet,
                                 lightBufferDescSet, lightCullingDescSet, frameIndex);

        // Barrier: injection write -> temporal read
        {
            vk::MemoryBarrier memBarrier{
                vk::AccessFlagBits::eShaderWrite,
                vk::AccessFlagBits::eShaderRead
            };

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                1, &memBarrier,
                0, nullptr,
                0, nullptr);
        }

        // Pass 2: Temporal Reprojection
        temporalFilter->dispatch(cmd, gridDescSet);

        // Barrier: temporal write -> ray march read
        {
            vk::MemoryBarrier memBarrier{
                vk::AccessFlagBits::eShaderWrite,
                vk::AccessFlagBits::eShaderRead
            };

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                1, &memBarrier,
                0, nullptr,
                0, nullptr);
        }

        // Pass 3: Ray March / Accumulation
        rayMarch->dispatch(cmd, gridDescSet);

        // Final barrier: compute write -> fragment read (for post-process composite)
        {
            vk::MemoryBarrier memBarrier{
                vk::AccessFlagBits::eShaderWrite,
                vk::AccessFlagBits::eShaderRead
            };

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::DependencyFlags{},
                1, &memBarrier,
                0, nullptr,
                0, nullptr);
        }
    }

    vk::ImageView VolumetricPipeline::getIntegratedVolumeImageView() const
    {
        return gridManager ? gridManager->getIntegratedView() : vk::ImageView{};
    }

    vk::Image VolumetricPipeline::getIntegratedVolumeImage() const
    {
        return gridManager ? gridManager->getIntegratedImage() : vk::Image{};
    }

    vk::Sampler VolumetricPipeline::getVolumeSampler() const
    {
        return gridManager ? gridManager->getSampler() : vk::Sampler{};
    }

    const VolumetricGridDimensions& VolumetricPipeline::getDimensions() const
    {
        static VolumetricGridDimensions empty{};
        return gridManager ? gridManager->getDimensions() : empty;
    }
}
