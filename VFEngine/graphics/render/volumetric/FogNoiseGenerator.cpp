#include "FogNoiseGenerator.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/MemoryUtilities.hpp"
#include "print/Log.hpp"
#include <array>

// Windows defines MemoryBarrier as a macro
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::volumetric
{
    static constexpr uint32_t NOISE_SIZE = 64;

    FogNoiseGenerator::FogNoiseGenerator(core::Device& device)
        : device(device)
    {
    }

    FogNoiseGenerator::~FogNoiseGenerator()
    {
        if (initialized)
            cleanup();
    }

    void FogNoiseGenerator::init()
    {
        if (initialized) return;

        createImage();
        createSampler();
        createComputePipeline();

        initialized = true;
    }

    void FogNoiseGenerator::cleanup()
    {
        if (!initialized) return;

        auto& dev = device.getLogicalDevice();

        if (genShader) { genShader->cleanUp(); genShader.reset(); }
        if (genPipeline) { dev.destroyPipeline(genPipeline); genPipeline = nullptr; }
        if (genPipelineLayout) { dev.destroyPipelineLayout(genPipelineLayout); genPipelineLayout = nullptr; }
        if (genDSPool) { dev.destroyDescriptorPool(genDSPool); genDSPool = nullptr; }
        if (genDSLayout) { dev.destroyDescriptorSetLayout(genDSLayout); genDSLayout = nullptr; }
        if (noiseSampler) { dev.destroySampler(noiseSampler); noiseSampler = nullptr; }

        destroyImage();

        initialized = false;
        generated = false;
    }

    void FogNoiseGenerator::generate(const vk::UniqueCommandBuffer& cmd)
    {
        if (!initialized || generated) return;

        // Transition to GENERAL for compute write
        core::ImageUtilities::transitionImageLayout(*cmd, noiseImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::ImageAspectFlagBits::eColor);

        // Dispatch compute shader
        cmd->bindPipeline(vk::PipelineBindPoint::eCompute, genPipeline);
        cmd->bindDescriptorSets(vk::PipelineBindPoint::eCompute, genPipelineLayout, 0, genDS, nullptr);
        cmd->dispatch(NOISE_SIZE / 8, NOISE_SIZE / 8, NOISE_SIZE / 8);

        // Barrier: compute write -> shader read
        vk::MemoryBarrier barrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
        cmd->pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                            {}, barrier, {}, {});

        generated = true;
    }

    void FogNoiseGenerator::createImage()
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e3D;
        imageInfo.extent = vk::Extent3D{NOISE_SIZE, NOISE_SIZE, NOISE_SIZE};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR8G8B8A8Unorm;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        noiseImage = dev.createImage(imageInfo);
        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(noiseImage);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        noiseMemory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(noiseImage, noiseMemory, 0);

        core::ImageViewInfoRequest viewReq(dev, noiseImage);
        viewReq.format = vk::Format::eR8G8B8A8Unorm;
        viewReq.imageType = vk::ImageViewType::e3D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, noiseView);
    }

    void FogNoiseGenerator::destroyImage()
    {
        auto& dev = device.getLogicalDevice();
        if (noiseView) { dev.destroyImageView(noiseView); noiseView = nullptr; }
        if (noiseImage) { dev.destroyImage(noiseImage); noiseImage = nullptr; }
        if (noiseMemory) { dev.freeMemory(noiseMemory); noiseMemory = nullptr; }
    }

    void FogNoiseGenerator::createSampler()
    {
        auto& dev = device.getLogicalDevice();

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.maxLod = 1.0f;

        noiseSampler = dev.createSampler(samplerInfo);
    }

    void FogNoiseGenerator::createComputePipeline()
    {
        auto& dev = device.getLogicalDevice();

        // Descriptor set layout: binding 0 = fog noise (storage image)
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eStorageImage;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        genDSLayout = dev.createDescriptorSetLayout(layoutInfo);

        // Descriptor pool
        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eStorageImage, 1};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        genDSPool = dev.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = genDSPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &genDSLayout;
        genDS = dev.allocateDescriptorSets(allocInfo)[0];

        // Pipeline layout
        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &genDSLayout;
        genPipelineLayout = dev.createPipelineLayout(plInfo);

        // Shader
        genShader = std::make_shared<core::Shader>(device);
        genShader->readShader("../../resources/shaders/volumetric/fog_noise_gen.glsl");

        const auto& stages = genShader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("FogNoiseGenerator: Failed to load shader");
            return;
        }

        // Compute pipeline
        vk::ComputePipelineCreateInfo cpInfo{};
        cpInfo.stage = stages[0];
        cpInfo.layout = genPipelineLayout;
        genPipeline = dev.createComputePipeline(nullptr, cpInfo).value;

        // Write descriptor
        vk::DescriptorImageInfo imgInfo{nullptr, noiseView, vk::ImageLayout::eGeneral};
        vk::WriteDescriptorSet write{genDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &imgInfo};
        dev.updateDescriptorSets(1, &write, 0, nullptr);
    }
}
