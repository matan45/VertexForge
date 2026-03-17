#include "CloudNoise.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/MemoryUtilities.hpp"

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::cloud
{
    CloudNoise::CloudNoise(core::Device& device)
        : device{device}
    {
    }

    CloudNoise::~CloudNoise()
    {
        cleanup();
    }

    void CloudNoise::init()
    {
        // Create images
        create3DImage(128, 128, 128, shapeImage, shapeMemory, shapeView);
        create3DImage(32, 32, 32, detailImage, detailMemory, detailView);
        create2DImage(1024, 1024, weatherImage, weatherMemory, weatherView);

        createSampler();
        createNoiseGenPipeline();
        createWeatherGenPipeline();

        initialized = true;
    }

    void CloudNoise::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        // Clean up shaders
        auto cleanShader = [](std::shared_ptr<core::Shader>& s) { if (s) { s->cleanUp(); s.reset(); } };
        cleanShader(noiseGenShader);
        cleanShader(weatherGenShader);

        // Destroy pipelines
        if (noiseGenPipeline) { dev.destroyPipeline(noiseGenPipeline); noiseGenPipeline = nullptr; }
        if (noiseGenPipelineLayout) { dev.destroyPipelineLayout(noiseGenPipelineLayout); noiseGenPipelineLayout = nullptr; }
        if (weatherGenPipeline) { dev.destroyPipeline(weatherGenPipeline); weatherGenPipeline = nullptr; }
        if (weatherGenPipelineLayout) { dev.destroyPipelineLayout(weatherGenPipelineLayout); weatherGenPipelineLayout = nullptr; }

        // Destroy descriptor pools/layouts
        auto destroyDS = [&](vk::DescriptorPool& pool, vk::DescriptorSetLayout& layout) {
            if (pool) { dev.destroyDescriptorPool(pool); pool = nullptr; }
            if (layout) { dev.destroyDescriptorSetLayout(layout); layout = nullptr; }
        };
        destroyDS(noiseGenDSPool, noiseGenDSLayout);
        destroyDS(weatherGenDSPool, weatherGenDSLayout);

        // Destroy sampler
        if (noiseSampler) { dev.destroySampler(noiseSampler); noiseSampler = nullptr; }

        // Destroy images
        destroyImage(shapeImage, shapeMemory, shapeView);
        destroyImage(detailImage, detailMemory, detailView);
        destroyImage(weatherImage, weatherMemory, weatherView);

        initialized = false;
        generated = false;
    }

    void CloudNoise::generate(const vk::CommandBuffer& cmd)
    {
        if (!initialized || generated) return;

        // Transition all images from UNDEFINED to GENERAL
        core::ImageUtilities::transitionImageLayout(cmd, shapeImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::ImageAspectFlagBits::eColor);
        core::ImageUtilities::transitionImageLayout(cmd, detailImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::ImageAspectFlagBits::eColor);
        core::ImageUtilities::transitionImageLayout(cmd, weatherImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::ImageAspectFlagBits::eColor);

        // Pass 0: Generate shape noise (128^3)
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, noiseGenPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, noiseGenPipelineLayout, 0, noiseGenDS, nullptr);

        uint32_t pass = 0;
        cmd.pushConstants(noiseGenPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(uint32_t), &pass);
        cmd.dispatch(128 / 8, 128 / 8, 128 / 8); // (16, 16, 16)

        // Barrier: shape noise must be written before detail noise reads (shared pipeline)
        vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                            {}, computeBarrier, {}, {});

        // Pass 1: Generate detail noise (32^3)
        pass = 1;
        cmd.pushConstants(noiseGenPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(uint32_t), &pass);
        cmd.dispatch(32 / 8, 32 / 8, 32 / 8); // (4, 4, 4)

        // Barrier: detail noise must be written before weather gen
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                            {}, computeBarrier, {}, {});

        // Generate weather map (1024x1024)
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, weatherGenPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, weatherGenPipelineLayout, 0, weatherGenDS, nullptr);
        cmd.dispatch(1024 / 16, 1024 / 16, 1); // (64, 64, 1)

        // Final barrier: ensure all writes are visible to subsequent fragment shader reads
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                            {}, computeBarrier, {}, {});

        generated = true;
    }

    // ---------- Private helpers ----------

    void CloudNoise::create3DImage(uint32_t width, uint32_t height, uint32_t depth,
                                    vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e3D;
        imageInfo.extent = vk::Extent3D{width, height, depth};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR8G8B8A8Unorm;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        image = dev.createImage(imageInfo);
        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        memory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(image, memory, 0);

        core::ImageViewInfoRequest viewReq(dev, image);
        viewReq.format = vk::Format::eR8G8B8A8Unorm;
        viewReq.imageType = vk::ImageViewType::e3D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    void CloudNoise::create2DImage(uint32_t width, uint32_t height,
                                    vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.extent = vk::Extent3D{width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR8G8B8A8Unorm;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        image = dev.createImage(imageInfo);
        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        memory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(image, memory, 0);

        core::ImageViewInfoRequest viewReq(dev, image);
        viewReq.format = vk::Format::eR8G8B8A8Unorm;
        viewReq.imageType = vk::ImageViewType::e2D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    void CloudNoise::destroyImage(vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        if (view) { dev.destroyImageView(view); view = nullptr; }
        if (image) { dev.destroyImage(image); image = nullptr; }
        if (memory) { dev.freeMemory(memory); memory = nullptr; }
    }

    void CloudNoise::createSampler()
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        info.addressModeU = vk::SamplerAddressMode::eRepeat;
        info.addressModeV = vk::SamplerAddressMode::eRepeat;
        info.addressModeW = vk::SamplerAddressMode::eRepeat;
        info.anisotropyEnable = VK_FALSE;
        noiseSampler = device.getLogicalDevice().createSampler(info);
    }

    void CloudNoise::createNoiseGenPipeline()
    {
        auto& dev = device.getLogicalDevice();

        // Descriptor set layout: binding 0 = shape (storage image), binding 1 = detail (storage image)
        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute}
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        noiseGenDSLayout = dev.createDescriptorSetLayout(layoutInfo);

        // Descriptor pool
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 2}
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        noiseGenDSPool = dev.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = noiseGenDSPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &noiseGenDSLayout;
        noiseGenDS = dev.allocateDescriptorSets(allocInfo)[0];

        // Push constant for pass index
        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(uint32_t);

        // Pipeline layout
        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &noiseGenDSLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pushRange;
        noiseGenPipelineLayout = dev.createPipelineLayout(plInfo);

        // Shader
        noiseGenShader = std::make_shared<core::Shader>(device);
        noiseGenShader->readShader("../../resources/shaders/cloud/cloud_noise_gen.glsl");

        // Compute pipeline
        const auto& stages = noiseGenShader->getShaderStages();
        vk::ComputePipelineCreateInfo cpInfo{};
        cpInfo.stage = stages[0];
        cpInfo.layout = noiseGenPipelineLayout;
        noiseGenPipeline = dev.createComputePipeline(nullptr, cpInfo).value;

        // Write descriptors
        vk::DescriptorImageInfo shapeImgInfo{nullptr, shapeView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo detailImgInfo{nullptr, detailView, vk::ImageLayout::eGeneral};

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0] = {noiseGenDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &shapeImgInfo};
        writes[1] = {noiseGenDS, 1, 0, 1, vk::DescriptorType::eStorageImage, &detailImgInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void CloudNoise::createWeatherGenPipeline()
    {
        auto& dev = device.getLogicalDevice();

        // Descriptor set layout: binding 0 = weather map (storage image)
        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute}
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        weatherGenDSLayout = dev.createDescriptorSetLayout(layoutInfo);

        // Descriptor pool
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1}
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        weatherGenDSPool = dev.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = weatherGenDSPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &weatherGenDSLayout;
        weatherGenDS = dev.allocateDescriptorSets(allocInfo)[0];

        // Pipeline layout (no push constants)
        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &weatherGenDSLayout;
        weatherGenPipelineLayout = dev.createPipelineLayout(plInfo);

        // Shader
        weatherGenShader = std::make_shared<core::Shader>(device);
        weatherGenShader->readShader("../../resources/shaders/cloud/cloud_weather_gen.glsl");

        // Compute pipeline
        const auto& stages = weatherGenShader->getShaderStages();
        vk::ComputePipelineCreateInfo cpInfo{};
        cpInfo.stage = stages[0];
        cpInfo.layout = weatherGenPipelineLayout;
        weatherGenPipeline = dev.createComputePipeline(nullptr, cpInfo).value;

        // Write descriptors
        vk::DescriptorImageInfo weatherImgInfo{nullptr, weatherView, vk::ImageLayout::eGeneral};

        std::array<vk::WriteDescriptorSet, 1> writes{};
        writes[0] = {weatherGenDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &weatherImgInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }
}
