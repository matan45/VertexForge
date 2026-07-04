#include "VFXRibbonPreviewPipeline.hpp"
#include "../quad/VFXQuadData.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "print/Log.hpp"


namespace render::vfx
{
    VFXRibbonPreviewPipeline::VFXRibbonPreviewPipeline(core::Device& device, core::SwapChain& swapChain,
                                                         core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
    {
    }

    VFXRibbonPreviewPipeline::~VFXRibbonPreviewPipeline()
    {
        cleanUp();
    }

    void VFXRibbonPreviewPipeline::init()
    {
        if (initialized)
        {
            return;
        }

        try
        {
            loadShader();
            if (!ribbonShader || ribbonShader->getShaderStages().empty())
            {
                vfLogError("VFXRibbonPreviewPipeline: Failed to load shader");
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                vfLogError("VFXRibbonPreviewPipeline: Failed to create descriptor set layout");
                cleanUp();
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                vfLogError("VFXRibbonPreviewPipeline: Failed to create descriptor pool");
                cleanUp();
                return;
            }

            createBuffers();
            if (!cameraUBO || !instanceBuffer)
            {
                vfLogError("VFXRibbonPreviewPipeline: Failed to create buffers");
                cleanUp();
                return;
            }

            createDefaultTexture();
            createSampler();
            createDescriptorSet();
            createPipeline();

            if (!graphicsPipeline || !pipelineLayout)
            {
                vfLogError("VFXRibbonPreviewPipeline: Failed to create graphics pipeline");
                cleanUp();
                return;
            }

            initialized = true;
        }
        catch (const vk::SystemError& e)
        {
            vfLogError("VFXRibbonPreviewPipeline: Vulkan error during init - {}", e.what());
            cleanUp();
        }
        catch (const std::exception& e)
        {
            vfLogError("VFXRibbonPreviewPipeline: Exception during init - {}", e.what());
            cleanUp();
        }
    }

    void VFXRibbonPreviewPipeline::loadShader()
    {
        ribbonShader = std::make_shared<core::Shader>(device);
        ribbonShader->readShader("../../resources/shaders/vfx/vfx_ribbon_preview.glsl");
    }

    void VFXRibbonPreviewPipeline::recreate()
    {
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        if (multiplyPipeline) { device.getLogicalDevice().destroyPipeline(multiplyPipeline); multiplyPipeline = nullptr; } // VK-1472
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void VFXRibbonPreviewPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline) { dev.destroyPipeline(graphicsPipeline); graphicsPipeline = nullptr; }
        if (multiplyPipeline) { dev.destroyPipeline(multiplyPipeline); multiplyPipeline = nullptr; } // VK-1472
        if (pipelineLayout) { dev.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }

        if (descriptorPool)
        {
            if (descriptorSet)
            {
                dev.freeDescriptorSets(descriptorPool, descriptorSet);
                descriptorSet = nullptr;
            }
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout) { dev.destroyDescriptorSetLayout(descriptorSetLayout); descriptorSetLayout = nullptr; }

        cameraUBOMapped = nullptr;
        core::BufferUtilities::destroyBuffer(dev, cameraUBO, cameraUBOAllocation, device.getMemoryManager());

        instanceBufferMapped = nullptr;
        core::BufferUtilities::destroyBuffer(dev, instanceBuffer, instanceBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::destroyBuffer(dev, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(dev, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());

        customTexture.reset();
        currentTexturePath.clear();

        if (textureSampler) { dev.destroySampler(textureSampler); textureSampler = nullptr; }
        if (defaultTextureImageView) { dev.destroyImageView(defaultTextureImageView); defaultTextureImageView = nullptr; }
        if (defaultTextureImage)
        {
            dev.destroyImage(defaultTextureImage);
            defaultTextureImage = nullptr;
        }
        if (defaultTextureAllocation) { device.getMemoryManager().free(defaultTextureAllocation); defaultTextureAllocation = {}; }

        if (ribbonShader)
        {
            ribbonShader->cleanUp();
            ribbonShader.reset();
        }

        currentInstanceCount = 0;
        initialized = false;
    }

    void VFXRibbonPreviewPipeline::createDescriptorSetLayout()
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

    void VFXRibbonPreviewPipeline::createDescriptorPool()
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

    void VFXRibbonPreviewPipeline::createDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        updateDescriptorSet();
    }

    void VFXRibbonPreviewPipeline::createPipeline()
    {
        auto vertexBinding = VFXQuadVertex::getBindingDescription();
        auto instanceBinding = VFXRibbonSegmentData::getBindingDescription();

        auto vertexAttribs = VFXQuadVertex::getAttributeDescriptions();
        auto instanceAttribs = VFXRibbonSegmentData::getAttributeDescriptions();

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {swapChain.getSceneColorFormat()},
            .depthAttachmentFormat = swapChain.getSwapchainDepthStencilFormat(),
            .shaderStages = ribbonShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding},
            .vertexAttributes = std::move(allAttribs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(VFXRibbonPreviewPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true,
            .srcColorBlendFactor = vk::BlendFactor::eOne,            // VK-1472: premultiplied shared state
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;

        // VK-1472: Multiply blend variant (dst*src) reuses the shared layout.
        core::GraphicsPipelineConfig multiplyConfig = config;
        multiplyConfig.existingPipelineLayout = pipelineLayout;
        multiplyConfig.srcColorBlendFactor = vk::BlendFactor::eDstColor;
        multiplyConfig.dstColorBlendFactor = vk::BlendFactor::eZero;
        multiplyPipeline = core::PipelineUtilities::createGraphicsPipeline(multiplyConfig).pipeline;
    }

    void VFXRibbonPreviewPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(VFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
        cameraUBOMapped = cameraUBOAllocation.mappedPtr;

        constexpr vk::DeviceSize vertexBufferSize = sizeof(VFXQuadVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(vkDevice, device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());

        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(vkDevice, device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            vkDevice, device.getPhysicalDevice(),
            device.getGraphicsQueue(), device.getStagingCommandPool(),
            quadVertexBuffer, QUAD_VERTICES.data(), vertexBufferSize
        );
        core::BufferUtilities::copyToBuffer(
            vkDevice, device.getPhysicalDevice(),
            device.getGraphicsQueue(), device.getStagingCommandPool(),
            quadIndexBuffer, QUAD_INDICES.data(), indexBufferSize
        );

        vk::DeviceSize instanceBufferSize = sizeof(VFXRibbonSegmentData) * maxInstances;
        core::BufferInfoRequest instanceRequest(vkDevice, device.getPhysicalDevice());
        instanceRequest.size = instanceBufferSize;
        instanceRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        instanceRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                     vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(instanceRequest, instanceBuffer, instanceBufferAllocation, device.getMemoryManager());
        instanceBufferMapped = instanceBufferAllocation.mappedPtr;
    }

    void VFXRibbonPreviewPipeline::createDefaultTexture()
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

    void VFXRibbonPreviewPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }

}
