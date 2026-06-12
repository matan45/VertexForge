#include "OceanFFT.hpp"
#include "OceanFFTResources.hpp"
#include "OceanFFTPipelines.hpp"
#include "OceanFFTReadback.hpp"
#include "../../core/Device.hpp"
#include "print/Log.hpp"

// Windows defines MemoryBarrier as a macro, which conflicts with vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::water
{
    OceanFFT::OceanFFT(core::Device& device)
        : device(device)
    {
    }

    OceanFFT::~OceanFFT()
    {
        cleanup();
    }

    void OceanFFT::init(const OceanFFTConfig& cfg)
    {
        if (initialized)
            return;

        config = cfg;

        resources = std::make_unique<OceanFFTResources>(device);
        resources->init(config.resolution);

        pipelines = std::make_unique<OceanFFTPipelines>(device);
        pipelines->init(*resources);

        readback = std::make_unique<OceanFFTReadback>(device);
        readback->init(config.resolution);

        spectrumDirty = true;
        firstDispatch = true;
        foamHistoryIndex = 0;
        lastDispatchTime = -1.0f;
        initialized = true;

        vfLogInfo("OceanFFT: Initialized ({}x{}, patch={}, wind={})",
                  config.resolution, config.resolution, config.patchSize, config.windSpeed);
    }

    void OceanFFT::cleanup()
    {
        if (!initialized)
            return;

        device.getLogicalDevice().waitIdle();

        pipelines->cleanup();
        pipelines.reset();

        readback->cleanup();
        readback.reset();

        resources->cleanup();
        resources.reset();

        initialized = false;
    }

    void OceanFFT::updateConfig(const OceanFFTConfig& newConfig)
    {
        bool resolutionChanged = newConfig.resolution != config.resolution;

        bool spectrumChanged =
            newConfig.patchSize != config.patchSize ||
            newConfig.windSpeed != config.windSpeed ||
            newConfig.windDirection != config.windDirection ||
            newConfig.amplitude != config.amplitude;

        config = newConfig;

        if (resolutionChanged && initialized)
        {
            device.getLogicalDevice().waitIdle();
            cleanup();
            init(config);
        }
        else if (spectrumChanged)
        {
            spectrumDirty = true;
        }
    }

    void OceanFFT::dispatch(vk::CommandBuffer cmd, float time)
    {
        if (!initialized) return;

        // Transition output images back to General for compute writes
        if (!firstDispatch)
        {
            std::array<vk::ImageMemoryBarrier, 3> barriers{};

            barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barriers[0].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barriers[0].newLayout = vk::ImageLayout::eGeneral;
            barriers[0].image = resources->getDisplacementImage();
            barriers[0].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barriers[1].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barriers[1].newLayout = vk::ImageLayout::eGeneral;
            barriers[1].image = resources->getNormalImage();
            barriers[1].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barriers[2].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barriers[2].newLayout = vk::ImageLayout::eGeneral;
            barriers[2].image = resources->getCausticImage();
            barriers[2].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barriers);
        }
        firstDispatch = false;

        if (spectrumDirty)
        {
            pipelines->dispatchSpectrum(cmd, config, *resources);
            spectrumDirty = false;

            OceanFFTPipelines::insertComputeBarrier(cmd);
        }

        pipelines->dispatchTimeEvolve(cmd, config, time, *resources);
        OceanFFTPipelines::insertComputeBarrier(cmd);

        pipelines->dispatchFFT(cmd, config, *resources);
        OceanFFTPipelines::insertComputeBarrier(cmd);

        // Foam history ping-pong: last frame's "curr" (written, General) becomes this
        // frame's "prev" (sampled, ShaderReadOnly) and vice versa. The initial layouts
        // set up in OceanFFTResources::transitionImagesInitial satisfy frame 0.
        {
            std::array<vk::ImageMemoryBarrier, 2> foamBarriers{};

            foamBarriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            foamBarriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
            foamBarriers[0].oldLayout = vk::ImageLayout::eGeneral;
            foamBarriers[0].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            foamBarriers[0].image = resources->getFoamHistoryImage(foamHistoryIndex);
            foamBarriers[0].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            foamBarriers[1].srcAccessMask = vk::AccessFlagBits::eShaderRead;
            foamBarriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            foamBarriers[1].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            foamBarriers[1].newLayout = vk::ImageLayout::eGeneral;
            foamBarriers[1].image = resources->getFoamHistoryImage(foamHistoryIndex + 1);
            foamBarriers[1].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, foamBarriers);
        }

        // dt for foam advection/decay; clamp so alt-tab pauses don't teleport the foam
        float deltaTime = lastDispatchTime >= 0.0f ? glm::clamp(time - lastDispatchTime, 0.0f, 0.1f) : 0.0f;
        lastDispatchTime = time;

        pipelines->dispatchMerge(cmd, config, *resources, deltaTime, foamHistoryIndex);
        foamHistoryIndex = 1 - foamHistoryIndex;

        readback->recordCopy(cmd, resources->getDisplacementImage(), config.resolution);
    }

    void OceanFFT::insertBarrier(vk::CommandBuffer cmd)
    {
        std::array<vk::ImageMemoryBarrier, 3> barriers{};

        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[0].oldLayout = vk::ImageLayout::eGeneral;
        barriers[0].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[0].image = resources->getDisplacementImage();
        barriers[0].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[1].oldLayout = vk::ImageLayout::eGeneral;
        barriers[1].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[1].image = resources->getNormalImage();
        barriers[1].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[2].oldLayout = vk::ImageLayout::eGeneral;
        barriers[2].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[2].image = resources->getCausticImage();
        barriers[2].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, barriers);
    }

    vk::DescriptorSetLayout OceanFFT::getOceanTextureLayout() const
    {
        return resources ? resources->getOceanTextureDescLayout() : vk::DescriptorSetLayout{};
    }

    vk::DescriptorSet OceanFFT::getOceanTextureDescSet() const
    {
        return resources ? resources->getOceanTextureDescSet() : vk::DescriptorSet{};
    }

    vk::ImageView OceanFFT::getCausticView() const
    {
        return resources ? resources->getCausticView() : vk::ImageView{};
    }

    void OceanFFT::readbackDisplacementData()
    {
        if (!initialized) return;
        readback->readback(config.resolution);
    }

    float OceanFFT::sampleHeightAt(const glm::vec2& worldXZ) const
    {
        if (!initialized) return 0.0f;
        return readback->sampleHeightAt(worldXZ, config.resolution, config.patchSize);
    }
}
