#include "VFXDistortionPipeline.hpp"
#include "../quad/VFXQuadData.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
#include "../bindless/VFXBindlessTextures.hpp"
#include "print/Log.hpp"

#include <cassert>

namespace render::vfx
{
    void VFXDistortionPipeline::loadShader()
    {
        distortionShader = std::make_shared<core::Shader>(device);
        distortionShader->readShader("../../resources/shaders/vfx/vfx_distortion.glsl");

        if (distortionShader->getShaderStages().empty())
        {
            vfLogError("VFXDistortionPipeline: Failed to load shader: {}",
                        distortionShader->getLastCompilationError());
        }
    }

    void VFXDistortionPipeline::createDescriptorSetLayout()
    {
        // VK-1481: binding 1 (per-emitter distortion texture) removed — textures now live in the
        // shared bindless set (set 1). The numeric gap at binding 1 is legal.
        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};

        // Binding 0: Camera UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // Binding 2: Particle SSBO
        bindings[1].binding = 2;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eVertex;

        // Binding 3: Emitter config SSBO
        bindings[2].binding = 3;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // Binding 4: Scene depth texture
        bindings[3].binding = 4;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // VK-1481 Phase 2, Binding 8: per-emitter distortion render-data SSBO (merged draw reads by emitterSlot)
        bindings[4].binding = 8;
        bindings[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VFXDistortionPipeline::createDescriptorPool()
    {
        // VK-1481: a single set-0 (no per-texture cloning). Depth is the only combined-image-sampler.
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;                      // camera UBO
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 1;                      // scene depth
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = 3;                      // particle + config + render-data SSBO

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VFXDistortionPipeline::allocateDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        defaultDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void VFXDistortionPipeline::createPipeline()
    {
        auto vertexBinding = VFXQuadVertex::getBindingDescription();
        auto vertexAttribs = VFXQuadVertex::getAttributeDescriptions();

        // VK-1481: the distortion pipeline has no lighting sets, so the local set 0 is the only set
        // before the shared bindless texture set, which is therefore appended at set index 1.
        std::vector<vk::DescriptorSetLayout> layouts = {descriptorSetLayout};
        assert(bindless && "VFXDistortionPipeline: bindless table must be set before init()");
        assert(layouts.size() == 1 && "VFX distortion expects only set 0 before the bindless set 1");
        layouts.push_back(bindless->getDescriptorSetLayout());

        // Additive blending for distortion vectors: srcColor=One, dstColor=One
        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat = depthFormat,
            .shaderStages = distortionShader->getShaderStages(),
            .vertexBindings = {vertexBinding},
            .vertexAttributes = {vertexAttribs.begin(), vertexAttribs.end()},
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = layouts,
            .pushConstantSize = sizeof(GPUVFXMergedPushConstants), // VK-1481 Phase 2: {runBaseSlot}
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eOne,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eOne,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void VFXDistortionPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(GPUVFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
        cameraUBOMapped = cameraUBOAllocation.mappedPtr;

        // VK-1481 Phase 2: per-emitter render-data SSBO (host-visible, mapped, one slot per emitter).
        core::BufferInfoRequest renderDataRequest(vkDevice, device.getPhysicalDevice());
        renderDataRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        renderDataRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                       vk::MemoryPropertyFlagBits::eHostCoherent;
        renderDataRequest.size = sizeof(VFXDistortionRenderData) * GPUVFXConstants::MAX_EMITTERS;
        core::BufferUtilities::createBuffer(renderDataRequest, renderDataBuffer, renderDataBufferAllocation, device.getMemoryManager());
        renderDataMapped = renderDataBufferAllocation.mappedPtr;

        constexpr vk::DeviceSize vertexBufferSize = sizeof(VFXQuadVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(vkDevice, device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                              vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());

        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(vkDevice, device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                             vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            vkDevice, device.getPhysicalDevice(),
            device.getGraphicsQueue(), device.getStagingCommandPool(),
            quadVertexBuffer, QUAD_VERTICES.data(), vertexBufferSize);

        core::BufferUtilities::copyToBuffer(
            vkDevice, device.getPhysicalDevice(),
            device.getGraphicsQueue(), device.getStagingCommandPool(),
            quadIndexBuffer, QUAD_INDICES.data(), indexBufferSize);
    }

    void VFXDistortionPipeline::createDefaultTexture()
    {
        auto vkDevice = device.getLogicalDevice();
        constexpr uint32_t texSize = 1;

        core::ImageInfoRequest imageInfo(
            vkDevice, device.getPhysicalDevice(),
            texSize, texSize, 1, 1,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::ImageUtilities::createImage(imageInfo, defaultTextureImage, defaultTextureAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewInfo(
            vkDevice, defaultTextureImage,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D);
        core::ImageUtilities::createImageView(viewInfo, defaultTextureImageView);

        // Default: (0.5, 0.5, 1.0, 1.0) = no distortion in normal-map encoding
        const std::array<uint8_t, 4> neutralPixel = {128, 128, 255, 255};
        core::ImageUtilities::uploadStagedPixelData(device, defaultTextureImage, neutralPixel.data(), neutralPixel.size(), texSize, texSize);
    }

    void VFXDistortionPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }

    void VFXDistortionPipeline::createDepthSampler()
    {
        depthSampler = core::ImageUtilities::createVFXDepthSampler(device.getLogicalDevice());
    }
}
