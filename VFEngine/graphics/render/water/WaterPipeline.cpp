#include "WaterPipeline.hpp"
#include "WaterMeshBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Log.hpp"
#include <cmath>
#include <cstring>

namespace render::water
{
    WaterPipeline::WaterPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
    }

    WaterPipeline::~WaterPipeline()
    {
        cleanup();
    }

    void WaterPipeline::init(const WaterPipelineLayoutConfig& config)
    {
        if (initialized)
            return;

        cachedIBLLayout = config.iblLayout;
        cachedLightDataLayout = config.lightDataLayout;
        cachedClusterGridLayout = config.clusterGridLayout;
        cachedCullingOutputLayout = config.cullingOutputLayout;
        cachedShadowDataLayout = config.shadowDataLayout;
        cachedShadowTextureLayout = config.shadowTextureLayout;

        loadShader();
        createWaterTileDescriptor();
        createDuDvTexture();
        createDuDvDescriptor();

        if (config.oceanTextureLayout)
        {
            oceanTextureLayout = config.oceanTextureLayout;
        }
        else
        {
            createOceanDummyTexture();
        }

        createGraphicsPipeline(config);

        initialized = true;
    }

    void WaterPipeline::recreate(const WaterPipelineLayoutConfig& config)
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();

        cachedIBLLayout = config.iblLayout;
        cachedLightDataLayout = config.lightDataLayout;
        cachedClusterGridLayout = config.clusterGridLayout;
        cachedCullingOutputLayout = config.cullingOutputLayout;
        cachedShadowDataLayout = config.shadowDataLayout;
        cachedShadowTextureLayout = config.shadowTextureLayout;

        if (config.oceanTextureLayout)
        {
            oceanTextureLayout = config.oceanTextureLayout;
        }
        else
        {
            oceanTextureLayout = oceanDummyLayout;
        }

        if (graphicsPipeline)
        {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        createGraphicsPipeline(config);
        vfLogInfo("WaterPipeline: Recreated after resize");
    }

    void WaterPipeline::cleanup()
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();

        if (graphicsPipeline)
        {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (dudvSampler)
        {
            vkDevice.destroySampler(dudvSampler);
            dudvSampler = nullptr;
        }

        if (dudvImageView)
        {
            vkDevice.destroyImageView(dudvImageView);
            dudvImageView = nullptr;
        }

        if (dudvImage)
        {
            vkDevice.destroyImage(dudvImage);
            dudvImage = nullptr;
        }

        if (dudvImageMemory)
        {
            vkDevice.freeMemory(dudvImageMemory);
            dudvImageMemory = nullptr;
        }

        if (dudvTexturePool)
        {
            vkDevice.destroyDescriptorPool(dudvTexturePool);
            dudvTexturePool = nullptr;
        }

        if (dudvTextureLayout)
        {
            vkDevice.destroyDescriptorSetLayout(dudvTextureLayout);
            dudvTextureLayout = nullptr;
        }

        if (waterTilePool)
        {
            vkDevice.destroyDescriptorPool(waterTilePool);
            waterTilePool = nullptr;
        }

        if (waterTileLayout)
        {
            vkDevice.destroyDescriptorSetLayout(waterTileLayout);
            waterTileLayout = nullptr;
        }

        // Ocean dummy resources (only destroy what we created via createOceanDummyTexture)
        if (oceanDummySampler) { vkDevice.destroySampler(oceanDummySampler); oceanDummySampler = nullptr; }
        if (oceanDummyView)    { vkDevice.destroyImageView(oceanDummyView); oceanDummyView = nullptr; }
        if (oceanDummyImage)   { vkDevice.destroyImage(oceanDummyImage); oceanDummyImage = nullptr; }
        if (oceanDummyMemory)  { vkDevice.freeMemory(oceanDummyMemory); oceanDummyMemory = nullptr; }
        if (oceanDummyPool)
        {
            vkDevice.destroyDescriptorPool(oceanDummyPool);
            oceanDummyPool = nullptr;
        }
        if (oceanDummyLayout)
        {
            vkDevice.destroyDescriptorSetLayout(oceanDummyLayout);
            oceanDummyLayout = nullptr;
        }
        oceanTextureLayout = nullptr;

        if (waterShader)
        {
            waterShader->cleanUp();
            waterShader.reset();
        }

        lastDescriptorTileCount = 0;
        initialized = false;
    }

    void WaterPipeline::loadShader()
    {
        waterShader = std::make_shared<core::Shader>(device);
        waterShader->readShader("../../resources/shaders/water/water.glsl");
    }

    void WaterPipeline::createWaterTileDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Layout: 1 SSBO binding for per-tile data (vertex stage reads it via gl_InstanceIndex)
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eStorageBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eVertex;
        binding.pImmutableSamplers = nullptr;

        std::array<vk::DescriptorBindingFlags, 1> bindingFlags;
        bindingFlags.fill(vk::DescriptorBindingFlagBits::eUpdateAfterBind);
        vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        flagsInfo.pBindingFlags = bindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.pNext = &flagsInfo;
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        waterTileLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        waterTilePool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = waterTilePool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &waterTileLayout;

        waterTileDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
    }

    // Simple hash-based noise for procedural dudv generation
    namespace
    {
        float hashNoise(float x, float y)
        {
            // Simple hash: fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453)
            float dot = x * 12.9898f + y * 78.233f;
            float s = std::sin(dot) * 43758.5453f;
            return s - std::floor(s);
        }

        float smoothNoise(float x, float y)
        {
            float ix = std::floor(x);
            float iy = std::floor(y);
            float fx = x - ix;
            float fy = y - iy;

            // Smoothstep
            fx = fx * fx * (3.0f - 2.0f * fx);
            fy = fy * fy * (3.0f - 2.0f * fy);

            float a = hashNoise(ix, iy);
            float b = hashNoise(ix + 1.0f, iy);
            float c = hashNoise(ix, iy + 1.0f);
            float d = hashNoise(ix + 1.0f, iy + 1.0f);

            float ab = a + (b - a) * fx;
            float cd = c + (d - c) * fx;
            return ab + (cd - ab) * fy;
        }

        float fbmNoise(float x, float y, int octaves)
        {
            float value = 0.0f;
            float amplitude = 0.5f;
            float frequency = 1.0f;
            for (int i = 0; i < octaves; ++i)
            {
                value += amplitude * smoothNoise(x * frequency, y * frequency);
                amplitude *= 0.5f;
                frequency *= 2.0f;
            }
            return value;
        }
    }

    void WaterPipeline::createDuDvTexture()
    {
        constexpr uint32_t texSize = 256;

        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            texSize, texSize, 1, 1,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, dudvImage, dudvImageMemory);

        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            dudvImage,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, dudvImageView);

        // Generate noise-based dudv pixel data
        std::vector<uint8_t> pixelData(texSize * texSize * 4);
        constexpr float noiseScale = 8.0f;

        for (uint32_t y = 0; y < texSize; ++y)
        {
            for (uint32_t x = 0; x < texSize; ++x)
            {
                float nx = static_cast<float>(x) / static_cast<float>(texSize) * noiseScale;
                float ny = static_cast<float>(y) / static_cast<float>(texSize) * noiseScale;

                // Two independent noise channels for RG displacement
                float r = fbmNoise(nx, ny, 4);
                float g = fbmNoise(nx + 5.2f, ny + 1.3f, 4);

                uint32_t idx = (y * texSize + x) * 4;
                pixelData[idx + 0] = static_cast<uint8_t>(r * 255.0f);
                pixelData[idx + 1] = static_cast<uint8_t>(g * 255.0f);
                pixelData[idx + 2] = 128; // neutral B
                pixelData[idx + 3] = 255; // full alpha
            }
        }

        vk::DeviceSize imageSize = pixelData.size();

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        void* data;
        vk::Result mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
        if (mapResult == vk::Result::eSuccess)
        {
            std::memcpy(data, pixelData.data(), imageSize);
            device.getLogicalDevice().unmapMemory(stagingMemory);
        }

        auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), device.getStagingCommandPool());

        core::ImageUtilities::transitionImageLayout(cmd.get(), dudvImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{texSize, texSize, 1};

        cmd->copyBufferToImage(stagingBuffer, dudvImage, vk::ImageLayout::eTransferDstOptimal, region);

        core::ImageUtilities::transitionImageLayout(cmd.get(), dudvImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device, cmd);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingMemory);

        // Create sampler with repeat wrapping for seamless tiling
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        dudvSampler = device.getLogicalDevice().createSampler(samplerInfo);

        vfLogInfo("WaterPipeline: Procedural DuDv texture created ({}x{})", texSize, texSize);
    }

    void WaterPipeline::createDuDvDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Layout: 1 combined image sampler for dudv texture
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        binding.pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        dudvTextureLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        dudvTexturePool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = dudvTexturePool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &dudvTextureLayout;

        dudvTextureDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = dudvImageView;
        imageInfo.sampler = dudvSampler;

        vk::WriteDescriptorSet write{};
        write.dstSet = dudvTextureDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkDevice.updateDescriptorSets(write, nullptr);
    }

    void WaterPipeline::createOceanDummyTexture()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create a 1x1 RGBA16F dummy texture for when ocean FFT is not active
        core::ImageInfoRequest imgReq(vkDevice, device.getPhysicalDevice(),
            1, 1, 1, 1,
            vk::Format::eR16G16B16A16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::ImageUtilities::createImage(imgReq, oceanDummyImage, oceanDummyMemory);

        core::ImageViewInfoRequest viewReq(vkDevice, oceanDummyImage,
            vk::Format::eR16G16B16A16Sfloat,
            vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
        core::ImageUtilities::createImageView(viewReq, oceanDummyView);

        // Transition to shader read optimal
        auto cmd = core::Utilities::beginSingleTimeCommands(vkDevice, device.getStagingCommandPool());
        core::ImageUtilities::transitionImageLayout(cmd.get(), oceanDummyImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
        core::Utilities::endSingleTimeCommands(device, cmd);

        // Create sampler
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        oceanDummySampler = vkDevice.createSampler(samplerInfo);

        // Create descriptor set layout (2 combined image samplers for vertex + fragment)
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        oceanDummyLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        oceanTextureLayout = oceanDummyLayout;

        // Create descriptor pool + set
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        oceanDummyPool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = oceanDummyPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &oceanDummyLayout;
        oceanDummyDescSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        // Update with dummy texture
        std::array<vk::DescriptorImageInfo, 2> imageInfos{};
        imageInfos[0] = {oceanDummySampler, oceanDummyView, vk::ImageLayout::eShaderReadOnlyOptimal};
        imageInfos[1] = {oceanDummySampler, oceanDummyView, vk::ImageLayout::eShaderReadOnlyOptimal};

        std::array<vk::WriteDescriptorSet, 2> writes{};
        for (int i = 0; i < 2; ++i)
        {
            writes[i].dstSet = oceanDummyDescSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkDevice.updateDescriptorSets(writes, nullptr);
    }

    void WaterPipeline::createGraphicsPipeline(const WaterPipelineLayoutConfig& layoutConfig)
    {
        vk::VertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(WaterVertex);
        vertexBinding.inputRate = vk::VertexInputRate::eVertex;

        std::vector<vk::VertexInputAttributeDescription> vertexAttributes(2);

        vertexAttributes[0].binding = 0;
        vertexAttributes[0].location = 0;
        vertexAttributes[0].format = vk::Format::eR32G32B32Sfloat;
        vertexAttributes[0].offset = offsetof(WaterVertex, position);

        vertexAttributes[1].binding = 0;
        vertexAttributes[1].location = 1;
        vertexAttributes[1].format = vk::Format::eR32G32Sfloat;
        vertexAttributes[1].offset = offsetof(WaterVertex, texCoord);

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = layoutConfig.renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = waterShader->getShaderStages(),
            .vertexBindings = {vertexBinding},
            .vertexAttributes = std::move(vertexAttributes),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {
                layoutConfig.iblLayout,
                waterTileLayout,
                dudvTextureLayout,
                layoutConfig.lightDataLayout,
                layoutConfig.clusterGridLayout,
                layoutConfig.cullingOutputLayout,
                layoutConfig.shadowDataLayout,
                layoutConfig.shadowTextureLayout,
                oceanTextureLayout
            },
            .pushConstantSize = sizeof(WaterPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void WaterPipeline::updateDescriptors(vk::Buffer tileSSBO, uint32_t tileCount)
    {
        if (!tileSSBO || tileCount == 0)
            return;

        if (tileCount == lastDescriptorTileCount)
            return;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = tileSSBO;
        bufferInfo.offset = 0;
        bufferInfo.range = tileCount * sizeof(WaterTileGPUData);

        vk::WriteDescriptorSet write{};
        write.dstSet = waterTileDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        device.getLogicalDevice().updateDescriptorSets(write, nullptr);
        lastDescriptorTileCount = tileCount;
    }

    void WaterPipeline::render(vk::CommandBuffer cmd, const WaterRenderDescriptors& descriptors,
                                WaterMeshBuffer& meshBuffer,
                                const WaterPushConstants& pushConstants)
    {
        if (!initialized || meshBuffer.getTileCount() == 0)
            return;

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        vk::Buffer vertexBuffers[] = {meshBuffer.getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        cmd.bindIndexBuffer(meshBuffer.getIndexBuffer(), 0, vk::IndexType::eUint32);

        // Use real ocean desc set if provided, otherwise the dummy
        vk::DescriptorSet oceanDescSet = descriptors.oceanTextureDescSet
            ? descriptors.oceanTextureDescSet
            : oceanDummyDescSet;

        std::array<vk::DescriptorSet, 9> descriptorSets = {
            descriptors.iblDescSet,
            waterTileDescriptorSet,
            dudvTextureDescriptorSet,
            descriptors.lightDataDescSet,
            descriptors.clusterGridDescSet,
            descriptors.cullingOutputDescSet,
            descriptors.shadowDataDescSet,
            descriptors.shadowTextureDescSet,
            oceanDescSet
        };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                0, descriptorSets, nullptr);

        cmd.pushConstants(pipelineLayout,
                           vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                           0, sizeof(WaterPushConstants), &pushConstants);

        cmd.drawIndexed(meshBuffer.getIndexCount(), meshBuffer.getTileCount(), 0, 0, 0);
    }
}
