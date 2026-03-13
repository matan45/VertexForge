#include "OceanFFTPipelines.hpp"
#include "OceanFFTResources.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"

#include <cmath>

// Windows defines MemoryBarrier as a macro, which conflicts with vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::water
{
    OceanFFTPipelines::OceanFFTPipelines(core::Device& device)
        : device(device)
    {
    }

    OceanFFTPipelines::~OceanFFTPipelines()
    {
        cleanup();
    }

    void OceanFFTPipelines::init(const OceanFFTResources& resources)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto createComputePipeline = [&](const char* shaderPath,
                                          vk::DescriptorSetLayout layout,
                                          uint32_t pushConstantSize,
                                          vk::PipelineLayout& outPipelineLayout,
                                          vk::Pipeline& outPipeline) -> std::unique_ptr<core::Shader>
        {
            auto shader = std::make_unique<core::Shader>(device);
            shader->readShader(shaderPath);

            const auto& stages = shader->getShaderStages();
            if (stages.empty())
            {
                vfLogError("OceanFFT: Failed to load shader {}: {}",
                            shaderPath, shader->getLastCompilationError());
                return {};
            }

            vk::PushConstantRange pushRange{};
            pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
            pushRange.offset = 0;
            pushRange.size = pushConstantSize;

            vk::PipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &layout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;

            outPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

            vk::ComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.stage = stages[0];
            pipelineInfo.layout = outPipelineLayout;

            auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
            if (result.result != vk::Result::eSuccess)
            {
                vfLogError("OceanFFT: Failed to create compute pipeline for {}", shaderPath);
                return {};
            }

            outPipeline = result.value;
            return shader;
        };

        spectrumShader = createComputePipeline(
            "../../resources/shaders/water/ocean_spectrum.glsl",
            resources.getSpectrumDescLayout(), sizeof(SpectrumPushConstants),
            spectrumPipelineLayout, spectrumPipeline);

        timeEvolveShader = createComputePipeline(
            "../../resources/shaders/water/ocean_time_evolve.glsl",
            resources.getTimeEvolveDescLayout(), sizeof(TimeEvolvePushConstants),
            timeEvolvePipelineLayout, timeEvolvePipeline);

        fftShader = createComputePipeline(
            "../../resources/shaders/water/ocean_fft.glsl",
            resources.getFFTDescLayout(), sizeof(FFTPushConstants),
            fftPipelineLayout, fftPipeline);

        mergeShader = createComputePipeline(
            "../../resources/shaders/water/ocean_merge.glsl",
            resources.getMergeDescLayout(), sizeof(MergePushConstants),
            mergePipelineLayout, mergePipeline);
    }

    void OceanFFTPipelines::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& l)
        {
            if (p) { vkDevice.destroyPipeline(p); p = nullptr; }
            if (l) { vkDevice.destroyPipelineLayout(l); l = nullptr; }
        };

        destroyPipeline(spectrumPipeline, spectrumPipelineLayout);
        destroyPipeline(timeEvolvePipeline, timeEvolvePipelineLayout);
        destroyPipeline(fftPipeline, fftPipelineLayout);
        destroyPipeline(mergePipeline, mergePipelineLayout);

        spectrumShader.reset();
        timeEvolveShader.reset();
        fftShader.reset();
        mergeShader.reset();
    }

    void OceanFFTPipelines::dispatchSpectrum(vk::CommandBuffer cmd, const OceanFFTConfig& config, const OceanFFTResources& resources)
    {
        uint32_t N = config.resolution;
        float windRad = glm::radians(config.windDirection);

        SpectrumPushConstants pc{};
        pc.N = N;
        pc.patchSize = config.patchSize;
        pc.windSpeed = config.windSpeed;
        pc.windDirX = std::cos(windRad);
        pc.windDirZ = std::sin(windRad);
        pc.amplitude = config.amplitude;
        pc.gravity = config.gravity;
        pc.cutoffLow = config.patchSize / 2000.0f;
        pc.seed = 42;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, spectrumPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, spectrumPipelineLayout, 0, resources.getSpectrumDescSet(), nullptr);
        cmd.pushConstants(spectrumPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);

        uint32_t groups = (N + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        cmd.dispatch(groups, groups, 1);
    }

    void OceanFFTPipelines::dispatchTimeEvolve(vk::CommandBuffer cmd, const OceanFFTConfig& config, float time, const OceanFFTResources& resources)
    {
        uint32_t N = config.resolution;

        TimeEvolvePushConstants pc{};
        pc.N = N;
        pc.time = time;
        pc.choppiness = config.choppiness;
        pc.patchSize = config.patchSize;
        pc.gravity = config.gravity;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, timeEvolvePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, timeEvolvePipelineLayout, 0, resources.getTimeEvolveDescSet(), nullptr);
        cmd.pushConstants(timeEvolvePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);

        uint32_t groups = (N + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        cmd.dispatch(groups, groups, 1);
    }

    void OceanFFTPipelines::dispatchFFT(vk::CommandBuffer cmd, const OceanFFTConfig& config, const OceanFFTResources& resources)
    {
        uint32_t N = config.resolution;
        uint32_t logN = static_cast<uint32_t>(std::log2(N));
        uint32_t groups = (N + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

        const auto& fftDescSets = resources.getFFTDescSets();

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, fftPipeline);

        // Process each field (Dy=0, Dx=1, Dz=2)
        for (int field = 0; field < 3; ++field)
        {
            uint32_t pingPong = 0;

            // Horizontal passes
            for (uint32_t stage = 0; stage < logN; ++stage)
            {
                FFTPushConstants pc{};
                pc.N = N;
                pc.stage = stage;
                pc.direction = 0;

                uint32_t descIdx = field * 2 + pingPong;
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, fftPipelineLayout, 0, fftDescSets[descIdx], nullptr);
                cmd.pushConstants(fftPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
                cmd.dispatch(groups, groups, 1);

                vk::MemoryBarrier memBarrier{};
                memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
                memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, memBarrier, {}, {});

                pingPong = 1 - pingPong;
            }

            // Vertical passes
            for (uint32_t stage = 0; stage < logN; ++stage)
            {
                FFTPushConstants pc{};
                pc.N = N;
                pc.stage = stage;
                pc.direction = 1;

                uint32_t descIdx = field * 2 + pingPong;
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, fftPipelineLayout, 0, fftDescSets[descIdx], nullptr);
                cmd.pushConstants(fftPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
                cmd.dispatch(groups, groups, 1);

                vk::MemoryBarrier memBarrier{};
                memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
                memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eComputeShader,
                    {}, memBarrier, {}, {});

                pingPong = 1 - pingPong;
            }
        }
    }

    void OceanFFTPipelines::dispatchMerge(vk::CommandBuffer cmd, const OceanFFTConfig& config, const OceanFFTResources& resources)
    {
        uint32_t N = config.resolution;

        MergePushConstants pc{};
        pc.N = N;
        pc.choppiness = config.choppiness;
        pc.patchSize = config.patchSize;
        pc.foamThreshold = config.foamThreshold;
        pc.displacementScale = config.displacementScale;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mergePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mergePipelineLayout, 0, resources.getMergeDescSet(), nullptr);
        cmd.pushConstants(mergePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);

        uint32_t groups = (N + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        cmd.dispatch(groups, groups, 1);
    }

    void OceanFFTPipelines::insertComputeBarrier(vk::CommandBuffer cmd)
    {
        vk::MemoryBarrier memBarrier{};
        memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, memBarrier, {}, {});
    }
}
