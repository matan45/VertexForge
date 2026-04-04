#include "VFXBillboardPipeline.hpp"
#include "../quad/VFXQuadData.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/ImageUtilities.hpp"

namespace render::vfx
{
    void VFXBillboardPipeline::createDescriptorSetLayout()
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

    void VFXBillboardPipeline::createDescriptorPool()
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

    void VFXBillboardPipeline::createDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        updateDescriptorSet();
    }

    void VFXBillboardPipeline::updateDescriptorSet()
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

    void VFXBillboardPipeline::createPipeline()
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
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {swapChain.getSceneColorFormat()},
            .depthAttachmentFormat = swapChain.getSwapchainDepthStencilFormat(),
            .shaderStages = vfxShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding},
            .vertexAttributes = std::move(allAttribs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(VFXFlipbookPushConstants),
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

    void VFXBillboardPipeline::createBuffers()
    {
        core::BufferInfoRequest uboRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(VFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());

        constexpr vk::DeviceSize vertexBufferSize = sizeof(VFXQuadVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());

        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());

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
        core::BufferUtilities::createBuffer(instanceRequest, instanceBuffer, instanceBufferAllocation, device.getMemoryManager());
    }

    void VFXBillboardPipeline::createDefaultTexture()
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
        core::ImageUtilities::createImage(imageInfo, defaultTextureImage, defaultTextureAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            defaultTextureImage,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, defaultTextureImageView);

        const std::array<uint8_t, 4> whitePixel = {255, 255, 255, 255};
        core::ImageUtilities::uploadStagedPixelData(device, defaultTextureImage, whitePixel.data(), whitePixel.size(), texSize, texSize);
    }

    void VFXBillboardPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }
}
