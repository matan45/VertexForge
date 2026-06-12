#pragma once

#include "OceanFFTTypes.hpp"
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::water
{
    class OceanFFTResources;

    class OceanFFTPipelines
    {
    public:
        explicit OceanFFTPipelines(core::Device& device);
        ~OceanFFTPipelines();

        OceanFFTPipelines(const OceanFFTPipelines&) = delete;
        OceanFFTPipelines& operator=(const OceanFFTPipelines&) = delete;

        void init(const OceanFFTResources& resources);
        void cleanup();

        void dispatchSpectrum(vk::CommandBuffer cmd, const OceanFFTConfig& config, const OceanFFTResources& resources);
        void dispatchTimeEvolve(vk::CommandBuffer cmd, const OceanFFTConfig& config, float time, const OceanFFTResources& resources);
        void dispatchFFT(vk::CommandBuffer cmd, const OceanFFTConfig& config, const OceanFFTResources& resources);
        void dispatchMerge(vk::CommandBuffer cmd, const OceanFFTConfig& config, const OceanFFTResources& resources,
                           float deltaTime, uint32_t foamParity);

        static void insertComputeBarrier(vk::CommandBuffer cmd);

    private:
        core::Device& device;

        std::unique_ptr<core::Shader> spectrumShader;
        std::unique_ptr<core::Shader> timeEvolveShader;
        std::unique_ptr<core::Shader> fftShader;
        std::unique_ptr<core::Shader> mergeShader;

        vk::Pipeline spectrumPipeline;
        vk::Pipeline timeEvolvePipeline;
        vk::Pipeline fftPipeline;
        vk::Pipeline mergePipeline;

        vk::PipelineLayout spectrumPipelineLayout;
        vk::PipelineLayout timeEvolvePipelineLayout;
        vk::PipelineLayout fftPipelineLayout;
        vk::PipelineLayout mergePipelineLayout;
    };
}
