#include "VFXScenePipeline.hpp"
#include "VFXQuadData.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"
#include <filesystem>

namespace render::vfx
{
    VFXScenePipeline::VFXScenePipeline(core::Device& device, core::SwapChain& swapChain)
        : device{device}
        , swapChain{swapChain}
    {
    }

    VFXScenePipeline::~VFXScenePipeline() = default;

    void VFXScenePipeline::init(vk::RenderPass renderPass)
    {
        externalRenderPass = renderPass;

        loadShader();
        createDescriptorSetLayout();
        createDescriptorPool();
        createBuffers();
        createDefaultTexture();
        createSampler();
        createDescriptorSet();
        createPipeline();

        initialized = true;
    }

    void VFXScenePipeline::loadShader()
    {
        vfxShader = std::make_shared<core::Shader>(device);
        vfxShader->readShader("../../resources/shaders/vfx/vfx_billboard.glsl");
    }

    void VFXScenePipeline::recreate(vk::RenderPass renderPass)
    {
        externalRenderPass = renderPass;

        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void VFXScenePipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline) dev.destroyPipeline(graphicsPipeline);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);

        if (descriptorPool)
        {
            if (descriptorSet)
                dev.freeDescriptorSets(descriptorPool, descriptorSet);
            dev.destroyDescriptorPool(descriptorPool);
        }
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);

        if (cameraUBO)
        {
            dev.destroyBuffer(cameraUBO);
            dev.freeMemory(cameraUBOMemory);
            cameraUBO = nullptr;
        }
        if (quadVertexBuffer)
        {
            dev.destroyBuffer(quadVertexBuffer);
            dev.freeMemory(quadVertexBufferMemory);
            quadVertexBuffer = nullptr;
        }
        if (quadIndexBuffer)
        {
            dev.destroyBuffer(quadIndexBuffer);
            dev.freeMemory(quadIndexBufferMemory);
            quadIndexBuffer = nullptr;
        }
        if (instanceBuffer)
        {
            dev.destroyBuffer(instanceBuffer);
            dev.freeMemory(instanceBufferMemory);
            instanceBuffer = nullptr;
        }

        customTexture.reset();
        currentTexturePath.clear();

        if (textureSampler) dev.destroySampler(textureSampler);
        if (defaultTextureImageView) dev.destroyImageView(defaultTextureImageView);
        if (defaultTextureImage)
        {
            dev.destroyImage(defaultTextureImage);
            dev.freeMemory(defaultTextureMemory);
        }

        if (vfxShader)
        {
            vfxShader->cleanUp();
            vfxShader.reset();
        }

        currentInstanceCount = 0;
        initialized = false;
    }

    void VFXScenePipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
        bindings[0].pImmutableSamplers = nullptr;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VFXScenePipeline::createDescriptorPool()
    {
        std::vector<vk::DescriptorPoolSize> poolSizes(2);
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VFXScenePipeline::createDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        updateDescriptorSet();
    }

    void VFXScenePipeline::updateDescriptorSet()
    {
        vk::DescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = cameraUBO;
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(VFXCameraUBO);

        vk::WriteDescriptorSet uboWrite{};
        uboWrite.dstSet = descriptorSet;
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &uboBufferInfo;

        vk::DescriptorImageInfo textureImageInfo{};
        textureImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        if (customTexture)
        {
            textureImageInfo.imageView = customTexture->getImageView();
            textureImageInfo.sampler = customTexture->getSampler();
        }
        else
        {
            textureImageInfo.imageView = defaultTextureImageView;
            textureImageInfo.sampler = textureSampler;
        }

        vk::WriteDescriptorSet textureWrite{};
        textureWrite.dstSet = descriptorSet;
        textureWrite.dstBinding = 1;
        textureWrite.dstArrayElement = 0;
        textureWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        textureWrite.descriptorCount = 1;
        textureWrite.pImageInfo = &textureImageInfo;

        std::array<vk::WriteDescriptorSet, 2> descriptorWrites = {uboWrite, textureWrite};
        device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    void VFXScenePipeline::createPipeline()
    {
        auto vertexBinding = VFXQuadVertex::getBindingDescription();
        auto instanceBinding = VFXInstanceData::getBindingDescription();

        auto vertexAttribs = VFXQuadVertex::getAttributeDescriptions();
        auto instanceAttribs = VFXInstanceData::getAttributeDescriptions();

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = externalRenderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = vfxShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding},
            .vertexAttributes = std::move(allAttribs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void VFXScenePipeline::createBuffers()
    {
        core::BufferInfoRequest uboRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(VFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOMemory);

        constexpr vk::DeviceSize vertexBufferSize = sizeof(VFXQuadVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferMemory);

        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            quadVertexBuffer,
            QUAD_VERTICES.data(),
            vertexBufferSize
        );

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            quadIndexBuffer,
            QUAD_INDICES.data(),
            indexBufferSize
        );

        vk::DeviceSize instanceBufferSize = sizeof(VFXInstanceData) * maxInstances;
        core::BufferInfoRequest instanceRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        instanceRequest.size = instanceBufferSize;
        instanceRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        instanceRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                     vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(instanceRequest, instanceBuffer, instanceBufferMemory);
    }

    void VFXScenePipeline::createDefaultTexture()
    {
        constexpr uint32_t texSize = 1;

        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            texSize, texSize, 1, 1,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, defaultTextureImage, defaultTextureMemory);

        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            defaultTextureImage,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, defaultTextureImageView);

        std::vector<uint8_t> pixelData = {255, 255, 255, 255};
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

        core::ImageUtilities::transitionImageLayout(cmd.get(), defaultTextureImage,
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

        cmd->copyBufferToImage(stagingBuffer, defaultTextureImage, vk::ImageLayout::eTransferDstOptimal, region);

        core::ImageUtilities::transitionImageLayout(cmd.get(), defaultTextureImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingMemory);
    }

    void VFXScenePipeline::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
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

        textureSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void VFXScenePipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                            const glm::vec3& cameraPos, float time) const
    {
        VFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(cameraUBOMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            std::memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(cameraUBOMemory);
        }
    }

    void VFXScenePipeline::setParticleInstances(const std::vector<VFXInstanceData>& instances)
    {
        if (instances.empty())
        {
            currentInstanceCount = 0;
            return;
        }

        currentInstanceCount = static_cast<uint32_t>(std::min(instances.size(),
                                                              static_cast<size_t>(maxInstances)));

        void* data;
        vk::DeviceSize bufferSize = sizeof(VFXInstanceData) * currentInstanceCount;
        vk::Result result = device.getLogicalDevice().mapMemory(instanceBufferMemory, 0, bufferSize, {}, &data);
        if (result == vk::Result::eSuccess)
        {
            std::memcpy(data, instances.data(), bufferSize);
            device.getLogicalDevice().unmapMemory(instanceBufferMemory);
        }
    }

    void VFXScenePipeline::setTexture(const std::string& texturePath)
    {
        if (texturePath == currentTexturePath)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        customTexture.reset();
        currentTexturePath.clear();

        if (texturePath.empty() || !std::filesystem::exists(texturePath))
        {
            if (!texturePath.empty())
            {
                loggerWarning("VFX scene texture not found: {}", texturePath);
            }
            updateDescriptorSet();
            return;
        }

        try
        {
            customTexture = std::make_unique<core::Texture>(device);
            customTexture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Srgb, false);
            currentTexturePath = texturePath;
            loggerInfo("VFX scene texture loaded: {}", texturePath);
        }
        catch (const std::exception& e)
        {
            loggerError("Failed to load VFX scene texture '{}': {}", texturePath, e.what());
            customTexture.reset();
            currentTexturePath.clear();
        }

        updateDescriptorSet();
    }

    void VFXScenePipeline::recordCommandsInline(const vk::CommandBuffer& commandBuffer) const
    {
        if (!initialized || currentInstanceCount == 0)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, descriptorSet, nullptr);

        vk::Buffer vertexBuffers[] = {quadVertexBuffer, instanceBuffer};
        vk::DeviceSize offsets[] = {0, 0};
        commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);

        commandBuffer.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        commandBuffer.drawIndexed(VFXConstants::QUAD_INDEX_COUNT, currentInstanceCount, 0, 0, 0);
    }
}
