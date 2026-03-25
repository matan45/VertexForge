#include "VolumetricPipeline.hpp"
#include "../../core/Device.hpp"
#include "print/Log.hpp"
#include <cmath>

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
        vk::DescriptorSetLayout lightCullingLayout,
        vk::DescriptorSetLayout shadowDataLayout,
        vk::DescriptorSetLayout shadowTextureLayout,
        vk::DescriptorSetLayout fogVolumeLayout,
        vk::DescriptorSetLayout giSamplingLayout)
    {
        if (initialized)
        {
            vfLogWarning("VolumetricPipeline: Already initialized");
            return;
        }

        gridManager = std::make_unique<VolumetricGridManager>(device);
        gridManager->init(quality);

        const auto& dims = gridManager->getDimensions();
        auto gridDescLayout = gridManager->getDescriptorSetLayout();

        lightInjection = std::make_unique<VolumetricLightInjection>(device);
        lightInjection->init(dims, gridDescLayout, clusterGridLayout, lightBufferLayout, lightCullingLayout,
                             shadowDataLayout, shadowTextureLayout, fogVolumeLayout, giSamplingLayout);

        temporalFilter = std::make_unique<VolumetricTemporalFilter>(device);
        temporalFilter->init(dims, gridDescLayout);

        rayMarch = std::make_unique<VolumetricRayMarch>(device);
        rayMarch->init(dims, gridDescLayout);

        initialized = true;
        vfLogInfo("VolumetricPipeline: Initialized with quality {} ({}x{}x{})",
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
    }

    void VolumetricPipeline::recreate(
        VolumetricQuality quality,
        vk::DescriptorSetLayout clusterGridLayout,
        vk::DescriptorSetLayout lightBufferLayout,
        vk::DescriptorSetLayout lightCullingLayout,
        vk::DescriptorSetLayout shadowDataLayout,
        vk::DescriptorSetLayout shadowTextureLayout,
        vk::DescriptorSetLayout fogVolumeLayout,
        vk::DescriptorSetLayout giSamplingLayout)
    {
        cleanup();
        init(quality, clusterGridLayout, lightBufferLayout, lightCullingLayout,
             shadowDataLayout, shadowTextureLayout, fogVolumeLayout, giSamplingLayout);
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
            settings.giInjectionIntensity);
        params.cameraPosition = glm::vec4(cameraPos, 0.0f);

        elapsedTime = std::fmod(elapsedTime + 1.0f / 60.0f, 1000.0f);
        params.noiseParams = glm::vec4(
            settings.noiseScale,
            settings.noiseEnabled ? settings.noiseIntensity : 0.0f,
            elapsedTime * settings.noiseSpeed,
            static_cast<float>(settings.noiseOctaves));

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
        vk::DescriptorSet lightCullingDescSet,
        vk::DescriptorSet shadowDataDescSet,
        vk::DescriptorSet shadowTextureDescSet,
        vk::DescriptorSet fogVolumeDescSet,
        vk::DescriptorSet giSamplingDescSet)
    {
        if (!initialized || !enabled)
            return;

        auto gridDescSet = gridManager->getDescriptorSet();

        // Ensure previous frame's fragment reads (composite) are complete before compute writes
        // Images are already in eGeneral from initialization
        {
            vk::MemoryBarrier memBarrier{
                vk::AccessFlagBits::eShaderRead,
                vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead
            };

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                1, &memBarrier,
                0, nullptr,
                0, nullptr);
        }

        // Pass 1: Light Injection (with shadow sampling + fog volumes + GI)
        lightInjection->dispatch(cmd, gridDescSet, clusterGridDescSet,
                                 lightBufferDescSet, lightCullingDescSet,
                                 shadowDataDescSet, shadowTextureDescSet,
                                 fogVolumeDescSet, giSamplingDescSet, frameIndex);

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
