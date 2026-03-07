#include "OceanFFT.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Log.hpp"

#include <cmath>
#include <cstring>

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

        createTextures();
        createSampler();
        createDescriptorLayouts();
        createPipelines();
        createDescriptorPool();
        allocateDescriptorSets();
        updateDescriptorSets();
        transitionImagesInitial();

        createReadbackBuffer();

        spectrumDirty = true;
        firstDispatch = true;
        readbackReady = false;
        initialized = true;

        vfLogInfo("OceanFFT: Initialized ({}x{}, patch={}, wind={})",
                  config.resolution, config.resolution, config.patchSize, config.windSpeed);
    }

    void OceanFFT::cleanup()
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();

        destroyPipelines();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (spectrumDescLayout)   { vkDevice.destroyDescriptorSetLayout(spectrumDescLayout);   spectrumDescLayout = nullptr; }
        if (timeEvolveDescLayout) { vkDevice.destroyDescriptorSetLayout(timeEvolveDescLayout); timeEvolveDescLayout = nullptr; }
        if (fftDescLayout)        { vkDevice.destroyDescriptorSetLayout(fftDescLayout);        fftDescLayout = nullptr; }
        if (mergeDescLayout)      { vkDevice.destroyDescriptorSetLayout(mergeDescLayout);      mergeDescLayout = nullptr; }
        if (oceanTextureDescLayout) { vkDevice.destroyDescriptorSetLayout(oceanTextureDescLayout); oceanTextureDescLayout = nullptr; }

        if (outputSampler) { vkDevice.destroySampler(outputSampler); outputSampler = nullptr; }

        destroyReadbackBuffer();
        destroyTextures();

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
        // On first dispatch they're already in General from transitionImagesInitial
        if (!firstDispatch)
        {
            std::array<vk::ImageMemoryBarrier, 2> barriers{};

            barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barriers[0].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barriers[0].newLayout = vk::ImageLayout::eGeneral;
            barriers[0].image = displacementImage;
            barriers[0].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barriers[1].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barriers[1].newLayout = vk::ImageLayout::eGeneral;
            barriers[1].image = normalImage;
            barriers[1].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barriers);
        }
        firstDispatch = false;

        if (spectrumDirty)
        {
            dispatchSpectrum(cmd);
            spectrumDirty = false;

            // Barrier: spectrum write -> time-evolve read
            insertComputeBarrier(cmd);
        }

        dispatchTimeEvolve(cmd, time);
        insertComputeBarrier(cmd);

        dispatchFFT(cmd);
        insertComputeBarrier(cmd);

        dispatchMerge(cmd);

        // Copy displacement to staging buffer for CPU-side physics readback
        recordReadbackCopy(cmd);
    }

    void OceanFFT::insertBarrier(vk::CommandBuffer cmd)
    {
        // Transition displacement/normal from compute write to fragment read
        std::array<vk::ImageMemoryBarrier, 2> barriers{};

        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[0].oldLayout = vk::ImageLayout::eGeneral;
        barriers[0].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[0].image = displacementImage;
        barriers[0].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[1].oldLayout = vk::ImageLayout::eGeneral;
        barriers[1].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[1].image = normalImage;
        barriers[1].subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, barriers);
    }

    // ========== Private implementation ==========

    void OceanFFT::createTextures()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        uint32_t N = config.resolution;

        // h0 Spectrum: RGBA32F
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                N, N, 1, 1,
                vk::Format::eR32G32B32A32Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(req, h0Image, h0Memory);

            core::ImageViewInfoRequest viewReq(vkDevice, h0Image,
                vk::Format::eR32G32B32A32Sfloat,
                vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
            core::ImageUtilities::createImageView(viewReq, h0View);
        }

        // Field textures: 3 fields x 2 ping-pong = 6 textures (RG32F)
        for (int f = 0; f < 3; ++f)
        {
            for (int p = 0; p < 2; ++p)
            {
                core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                    N, N, 1, 1,
                    vk::Format::eR32G32Sfloat,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eStorage,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
                core::ImageUtilities::createImage(req, fields[f].images[p], fields[f].memory[p]);

                core::ImageViewInfoRequest viewReq(vkDevice, fields[f].images[p],
                    vk::Format::eR32G32Sfloat,
                    vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
                core::ImageUtilities::createImageView(viewReq, fields[f].views[p]);
            }
        }

        // Displacement: RGBA16F (storage + sampled + transfer src for CPU readback)
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                N, N, 1, 1,
                vk::Format::eR16G16B16A16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(req, displacementImage, displacementMemory);

            core::ImageViewInfoRequest viewReq(vkDevice, displacementImage,
                vk::Format::eR16G16B16A16Sfloat,
                vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
            core::ImageUtilities::createImageView(viewReq, displacementView);
        }

        // Normal: RGBA16F (storage + sampled)
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                N, N, 1, 1,
                vk::Format::eR16G16B16A16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(req, normalImage, normalMemory);

            core::ImageViewInfoRequest viewReq(vkDevice, normalImage,
                vk::Format::eR16G16B16A16Sfloat,
                vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
            core::ImageUtilities::createImageView(viewReq, normalView);
        }
    }

    void OceanFFT::createSampler()
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.addressModeU = vk::SamplerAddressMode::eRepeat;
        info.addressModeV = vk::SamplerAddressMode::eRepeat;
        info.addressModeW = vk::SamplerAddressMode::eRepeat;
        info.anisotropyEnable = VK_FALSE;
        info.maxAnisotropy = 1.0f;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;

        outputSampler = device.getLogicalDevice().createSampler(info);
    }

    void OceanFFT::createDescriptorLayouts()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Spectrum init: 1 storage image (writeonly)
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eStorageImage;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eCompute;

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = 1;
            info.pBindings = &binding;
            spectrumDescLayout = vkDevice.createDescriptorSetLayout(info);
        }

        // Time evolve: 1 readonly + 3 writeonly storage images
        {
            std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
            for (uint32_t i = 0; i < 4; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = vk::DescriptorType::eStorageImage;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
            }

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            timeEvolveDescLayout = vkDevice.createDescriptorSetLayout(info);
        }

        // FFT: 2 storage images (input + output)
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            for (uint32_t i = 0; i < 2; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = vk::DescriptorType::eStorageImage;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
            }

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            fftDescLayout = vkDevice.createDescriptorSetLayout(info);
        }

        // Merge: 3 readonly + 2 writeonly storage images
        {
            std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
            for (uint32_t i = 0; i < 5; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = vk::DescriptorType::eStorageImage;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
            }

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            mergeDescLayout = vkDevice.createDescriptorSetLayout(info);
        }

        // Ocean texture output: 2 combined image samplers for graphics pipeline
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            oceanTextureDescLayout = vkDevice.createDescriptorSetLayout(info);
        }
    }

    void OceanFFT::createPipelines()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto createComputePipeline = [&](const char* shaderPath,
                                          vk::DescriptorSetLayout layout,
                                          uint32_t pushConstantSize,
                                          core::Shader*& outShader,
                                          vk::PipelineLayout& outPipelineLayout,
                                          vk::Pipeline& outPipeline)
        {
            auto shader = std::make_unique<core::Shader>(device);
            shader->readShader(shaderPath);

            const auto& stages = shader->getShaderStages();
            if (stages.empty())
            {
                vfLogError("OceanFFT: Failed to load shader {}: {}",
                            shaderPath, shader->getLastCompilationError());
                return std::unique_ptr<core::Shader>{};
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
                return std::unique_ptr<core::Shader>{};
            }

            outPipeline = result.value;
            outShader = shader.get();
            return shader;
        };

        core::Shader* dummy;
        spectrumShader = createComputePipeline(
            "../../resources/shaders/water/ocean_spectrum.glsl",
            spectrumDescLayout, sizeof(SpectrumPushConstants),
            dummy, spectrumPipelineLayout, spectrumPipeline);

        timeEvolveShader = createComputePipeline(
            "../../resources/shaders/water/ocean_time_evolve.glsl",
            timeEvolveDescLayout, sizeof(TimeEvolvePushConstants),
            dummy, timeEvolvePipelineLayout, timeEvolvePipeline);

        fftShader = createComputePipeline(
            "../../resources/shaders/water/ocean_fft.glsl",
            fftDescLayout, sizeof(FFTPushConstants),
            dummy, fftPipelineLayout, fftPipeline);

        mergeShader = createComputePipeline(
            "../../resources/shaders/water/ocean_merge.glsl",
            mergeDescLayout, sizeof(MergePushConstants),
            dummy, mergePipelineLayout, mergePipeline);
    }

    void OceanFFT::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Count descriptors needed:
        // spectrum: 1 storage image
        // timeEvolve: 4 storage images
        // fft: 6 sets x 2 storage images = 12 storage images
        // merge: 5 storage images
        // oceanTexture: 2 combined image samplers
        // Total storage images: 1 + 4 + 12 + 5 = 22
        // Total samplers: 2

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageImage;
        poolSizes[0].descriptorCount = 22;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 10; // 1 + 1 + 6 + 1 + 1
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void OceanFFT::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto allocSet = [&](vk::DescriptorSetLayout layout) -> vk::DescriptorSet
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;
            return vkDevice.allocateDescriptorSets(allocInfo)[0];
        };

        spectrumDescSet = allocSet(spectrumDescLayout);
        timeEvolveDescSet = allocSet(timeEvolveDescLayout);

        for (int i = 0; i < 6; ++i)
            fftDescSets[i] = allocSet(fftDescLayout);

        mergeDescSet = allocSet(mergeDescLayout);
        oceanTextureDescSet = allocSet(oceanTextureDescLayout);
    }

    void OceanFFT::updateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        std::vector<vk::WriteDescriptorSet> writes;
        std::vector<vk::DescriptorImageInfo> imageInfos;
        imageInfos.reserve(32);

        auto addStorageImageWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::ImageView view)
        {
            imageInfos.push_back({nullptr, view, vk::ImageLayout::eGeneral});

            vk::WriteDescriptorSet write{};
            write.dstSet = set;
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eStorageImage;
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
        };

        auto addSampledImageWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::ImageView view)
        {
            imageInfos.push_back({outputSampler, view, vk::ImageLayout::eShaderReadOnlyOptimal});

            vk::WriteDescriptorSet write{};
            write.dstSet = set;
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
        };

        // Spectrum init: binding 0 = h0
        addStorageImageWrite(spectrumDescSet, 0, h0View);

        // Time evolve: binding 0 = h0 (read), 1-3 = field[0..2] ping-pong 0 (write)
        addStorageImageWrite(timeEvolveDescSet, 0, h0View);
        for (int f = 0; f < 3; ++f)
            addStorageImageWrite(timeEvolveDescSet, f + 1, fields[f].views[0]);

        // FFT: 3 fields x 2 directions
        // fftDescSets[f*2 + 0]: input=field[f][0], output=field[f][1]
        // fftDescSets[f*2 + 1]: input=field[f][1], output=field[f][0]
        for (int f = 0; f < 3; ++f)
        {
            addStorageImageWrite(fftDescSets[f * 2 + 0], 0, fields[f].views[0]);
            addStorageImageWrite(fftDescSets[f * 2 + 0], 1, fields[f].views[1]);

            addStorageImageWrite(fftDescSets[f * 2 + 1], 0, fields[f].views[1]);
            addStorageImageWrite(fftDescSets[f * 2 + 1], 1, fields[f].views[0]);
        }

        // Merge: bindings 0-2 = field results (ping-pong 0), 3 = displacement, 4 = normal
        // After even number of FFT stages, result is in ping-pong 0
        for (int f = 0; f < 3; ++f)
            addStorageImageWrite(mergeDescSet, f, fields[f].views[0]);
        addStorageImageWrite(mergeDescSet, 3, displacementView);
        addStorageImageWrite(mergeDescSet, 4, normalView);

        // Ocean textures for graphics sampling
        addSampledImageWrite(oceanTextureDescSet, 0, displacementView);
        addSampledImageWrite(oceanTextureDescSet, 1, normalView);

        vkDevice.updateDescriptorSets(writes, nullptr);
    }

    void OceanFFT::transitionImagesInitial()
    {
        auto cmd = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), device.getStagingCommandPool());

        auto transitionToGeneral = [&](vk::Image image)
        {
            core::ImageUtilities::transitionImageLayout(
                cmd.get(), image,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                vk::ImageAspectFlagBits::eColor);
        };

        transitionToGeneral(h0Image);

        for (int f = 0; f < 3; ++f)
        {
            transitionToGeneral(fields[f].images[0]);
            transitionToGeneral(fields[f].images[1]);
        }

        transitionToGeneral(displacementImage);
        transitionToGeneral(normalImage);

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd);
    }

    void OceanFFT::dispatchSpectrum(vk::CommandBuffer cmd)
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
        pc.cutoffLow = config.patchSize / 2000.0f; // Small wave cutoff
        pc.seed = 42;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, spectrumPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, spectrumPipelineLayout, 0, spectrumDescSet, nullptr);
        cmd.pushConstants(spectrumPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);

        uint32_t groups = (N + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        cmd.dispatch(groups, groups, 1);
    }

    void OceanFFT::dispatchTimeEvolve(vk::CommandBuffer cmd, float time)
    {
        uint32_t N = config.resolution;

        TimeEvolvePushConstants pc{};
        pc.N = N;
        pc.time = time;
        pc.choppiness = config.choppiness;
        pc.patchSize = config.patchSize;
        pc.gravity = config.gravity;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, timeEvolvePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, timeEvolvePipelineLayout, 0, timeEvolveDescSet, nullptr);
        cmd.pushConstants(timeEvolvePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);

        uint32_t groups = (N + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        cmd.dispatch(groups, groups, 1);
    }

    void OceanFFT::dispatchFFT(vk::CommandBuffer cmd)
    {
        uint32_t N = config.resolution;
        uint32_t logN = static_cast<uint32_t>(std::log2(N));
        uint32_t groups = (N + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, fftPipeline);

        // Process each field (Dy=0, Dx=1, Dz=2)
        for (int field = 0; field < 3; ++field)
        {
            uint32_t pingPong = 0; // Start from buffer 0 (time-evolve output)

            // Horizontal passes
            for (uint32_t stage = 0; stage < logN; ++stage)
            {
                FFTPushConstants pc{};
                pc.N = N;
                pc.stage = stage;
                pc.direction = 0; // horizontal

                // Select descriptor set based on ping-pong state
                uint32_t descIdx = field * 2 + pingPong;
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, fftPipelineLayout, 0, fftDescSets[descIdx], nullptr);
                cmd.pushConstants(fftPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
                cmd.dispatch(groups, groups, 1);

                // Barrier between FFT stages
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
                pc.direction = 1; // vertical

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

            // After 2*logN stages (even), pingPong is back to 0 = result in buffer 0
            // This matches the merge descriptor set which reads from fields[*].views[0]
        }
    }

    void OceanFFT::dispatchMerge(vk::CommandBuffer cmd)
    {
        uint32_t N = config.resolution;

        MergePushConstants pc{};
        pc.N = N;
        pc.choppiness = config.choppiness;
        pc.patchSize = config.patchSize;
        pc.foamThreshold = config.foamThreshold;
        pc.displacementScale = config.displacementScale;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mergePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mergePipelineLayout, 0, mergeDescSet, nullptr);
        cmd.pushConstants(mergePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);

        uint32_t groups = (N + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        cmd.dispatch(groups, groups, 1);
    }

    void OceanFFT::insertComputeBarrier(vk::CommandBuffer cmd)
    {
        vk::MemoryBarrier memBarrier{};
        memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, memBarrier, {}, {});
    }

    void OceanFFT::createReadbackBuffer()
    {
        uint32_t N = config.resolution;
        // RGBA16F = 8 bytes per pixel
        vk::DeviceSize bufferSize = N * N * 8;

        core::BufferInfoRequest req(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            bufferSize,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        core::BufferUtilities::createBuffer(req, readbackBuffer, readbackMemory);
    }

    void OceanFFT::destroyReadbackBuffer()
    {
        if (readbackBuffer)
        {
            core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), readbackBuffer, readbackMemory);
            readbackBuffer = nullptr;
            readbackMemory = nullptr;
        }
        cpuDisplacementData.clear();
        readbackReady = false;
    }

    void OceanFFT::recordReadbackCopy(vk::CommandBuffer cmd)
    {
        uint32_t N = config.resolution;

        // Barrier: compute write → transfer read
        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.image = displacementImage;
        barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, barrier);

        // Copy image to buffer
        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{N, N, 1};

        cmd.copyImageToBuffer(displacementImage, vk::ImageLayout::eTransferSrcOptimal,
                              readbackBuffer, region);

        // Barrier: transfer → back to general for next frame's compute
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eGeneral;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, barrier);

        readbackReady = true;
    }

    void OceanFFT::readbackDisplacementData()
    {
        if (!initialized || !readbackReady || !readbackBuffer)
            return;

        // Ensure GPU transfer is complete before reading back
        device.getLogicalDevice().waitIdle();

        uint32_t N = config.resolution;
        vk::DeviceSize bufferSize = N * N * 8; // RGBA16F = 8 bytes per pixel

        void* mapped = device.getLogicalDevice().mapMemory(readbackMemory, 0, bufferSize);
        if (!mapped)
            return;

        const uint16_t* halfData = static_cast<const uint16_t*>(mapped);
        cpuDisplacementData.resize(N * N);

        for (uint32_t i = 0; i < N * N; ++i)
        {
            // Convert half-float to float manually
            auto halfToFloat = [](uint16_t h) -> float
            {
                uint32_t sign = (h >> 15) & 0x1;
                uint32_t exp = (h >> 10) & 0x1F;
                uint32_t mant = h & 0x3FF;

                if (exp == 0)
                {
                    if (mant == 0) return sign ? -0.0f : 0.0f;
                    // Denormalized
                    float f = std::ldexp(static_cast<float>(mant), -24);
                    return sign ? -f : f;
                }
                if (exp == 31)
                {
                    if (mant == 0) return sign ? -INFINITY : INFINITY;
                    return NAN;
                }

                float f = std::ldexp(static_cast<float>(mant | 0x400), static_cast<int>(exp) - 25);
                float result = sign ? -f : f;
                return std::isnan(result) ? 0.0f : result;
            };

            cpuDisplacementData[i] = glm::vec4(
                halfToFloat(halfData[i * 4 + 0]),  // dx
                halfToFloat(halfData[i * 4 + 1]),  // dy
                halfToFloat(halfData[i * 4 + 2]),  // dz
                halfToFloat(halfData[i * 4 + 3])   // foam
            );
        }

        device.getLogicalDevice().unmapMemory(readbackMemory);
    }

    float OceanFFT::sampleHeightAt(const glm::vec2& worldXZ) const
    {
        if (cpuDisplacementData.empty() || config.patchSize <= 0.0f)
            return 0.0f;

        uint32_t N = config.resolution;

        // World position to UV (repeating patch)
        float u = worldXZ.x / config.patchSize;
        float v = worldXZ.y / config.patchSize;

        // Wrap to [0, 1)
        u = u - std::floor(u);
        v = v - std::floor(v);

        // UV to texel coordinates (bilinear)
        float fx = u * N - 0.5f;
        float fy = v * N - 0.5f;

        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float fracX = fx - x0;
        float fracY = fy - y0;

        // Wrap texel indices
        auto wrap = [N](int c) -> uint32_t { return static_cast<uint32_t>(((c % static_cast<int>(N)) + N) % N); };
        uint32_t x0w = wrap(x0), x1w = wrap(x0 + 1);
        uint32_t y0w = wrap(y0), y1w = wrap(y0 + 1);

        // Bilinear interpolation of dy (y component = index 1)
        float h00 = cpuDisplacementData[y0w * N + x0w].y;
        float h10 = cpuDisplacementData[y0w * N + x1w].y;
        float h01 = cpuDisplacementData[y1w * N + x0w].y;
        float h11 = cpuDisplacementData[y1w * N + x1w].y;

        float h0 = h00 + fracX * (h10 - h00);
        float h1 = h01 + fracX * (h11 - h01);

        return h0 + fracY * (h1 - h0);
    }

    void OceanFFT::destroyTextures()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto destroyImage = [&](vk::Image& img, vk::DeviceMemory& mem, vk::ImageView& view)
        {
            if (view) { vkDevice.destroyImageView(view); view = nullptr; }
            if (img)  { vkDevice.destroyImage(img); img = nullptr; }
            if (mem)  { vkDevice.freeMemory(mem); mem = nullptr; }
        };

        destroyImage(h0Image, h0Memory, h0View);

        for (int f = 0; f < 3; ++f)
        {
            destroyImage(fields[f].images[0], fields[f].memory[0], fields[f].views[0]);
            destroyImage(fields[f].images[1], fields[f].memory[1], fields[f].views[1]);
        }

        destroyImage(displacementImage, displacementMemory, displacementView);
        destroyImage(normalImage, normalMemory, normalView);
    }

    void OceanFFT::destroyPipelines()
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
}
