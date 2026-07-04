#include "VFXMeshPreviewPipeline.hpp"
#include "../../mesh/MeshTypes.hpp"
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
    VFXMeshPreviewPipeline::VFXMeshPreviewPipeline(core::Device& device, core::SwapChain& swapChain,
                                                     core::OffscreenResources& offscreenResources,
                                                     render::mesh::MeshGPUCache& meshCache)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
        , meshCache{meshCache}
    {
    }

    VFXMeshPreviewPipeline::~VFXMeshPreviewPipeline()
    {
        cleanUp();
    }

    void VFXMeshPreviewPipeline::init()
    {
        if (initialized)
        {
            return;
        }

        try
        {
            loadShader();
            if (!meshShader || meshShader->getShaderStages().empty())
            {
                vfLogError("VFXMeshPreviewPipeline: Failed to load shader");
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                vfLogError("VFXMeshPreviewPipeline: Failed to create descriptor set layout");
                cleanUp();
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                vfLogError("VFXMeshPreviewPipeline: Failed to create descriptor pool");
                cleanUp();
                return;
            }

            createBuffers();
            if (!cameraUBO || !instanceBuffer)
            {
                vfLogError("VFXMeshPreviewPipeline: Failed to create buffers");
                cleanUp();
                return;
            }

            createDefaultTexture();
            createSampler();
            createDescriptorSet();

            createPipeline();
            if (!graphicsPipeline || !pipelineLayout)
            {
                vfLogError("VFXMeshPreviewPipeline: Failed to create graphics pipeline");
                cleanUp();
                return;
            }

            initialized = true;
        }
        catch (const vk::SystemError& e)
        {
            vfLogError("VFXMeshPreviewPipeline: Vulkan error during init - {}", e.what());
            cleanUp();
        }
        catch (const std::exception& e)
        {
            vfLogError("VFXMeshPreviewPipeline: Exception during init - {}", e.what());
            cleanUp();
        }
    }

    void VFXMeshPreviewPipeline::loadShader()
    {
        meshShader = std::make_shared<core::Shader>(device);
        meshShader->readShader("../../resources/shaders/vfx/vfx_mesh_preview.glsl");
    }

    void VFXMeshPreviewPipeline::recreate()
    {
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        if (multiplyPipeline) { device.getLogicalDevice().destroyPipeline(multiplyPipeline); multiplyPipeline = nullptr; } // VK-1472
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void VFXMeshPreviewPipeline::cleanUp()
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

        customTexture.reset();
        currentTexturePath.clear();
        currentMeshPath.clear();
        currentMeshId.clear();
        meshVertexBuffer = nullptr;
        meshIndexBuffer = nullptr;
        meshIndexCount = 0;

        if (textureSampler) { dev.destroySampler(textureSampler); textureSampler = nullptr; }
        if (defaultTextureImageView) { dev.destroyImageView(defaultTextureImageView); defaultTextureImageView = nullptr; }
        if (defaultTextureImage)
        {
            dev.destroyImage(defaultTextureImage);
            defaultTextureImage = nullptr;
        }
        if (defaultTextureAllocation) { device.getMemoryManager().free(defaultTextureAllocation); defaultTextureAllocation = {}; }

        if (meshShader)
        {
            meshShader->cleanUp();
            meshShader.reset();
        }

        currentInstanceCount = 0;
        initialized = false;
    }

    void VFXMeshPreviewPipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VFXMeshPreviewPipeline::createDescriptorPool()
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

    void VFXMeshPreviewPipeline::createDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        updateDescriptorSet();
    }

    void VFXMeshPreviewPipeline::updateDescriptorSet()
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

    void VFXMeshPreviewPipeline::createPipeline()
    {
        auto meshBinding = render::mesh::MeshVertexInput::getBindingDescription();
        auto meshAttribs = render::mesh::MeshVertexInput::getAttributeDescriptions();

        vk::VertexInputBindingDescription instanceBinding{};
        instanceBinding.binding = 1;
        instanceBinding.stride = sizeof(VFXInstanceData);
        instanceBinding.inputRate = vk::VertexInputRate::eInstance;

        std::array<vk::VertexInputAttributeDescription, 8> instanceAttribs{};
        // location 3: worldPosAndSize (vec4)
        instanceAttribs[0].binding = 1;
        instanceAttribs[0].location = 3;
        instanceAttribs[0].format = vk::Format::eR32G32B32A32Sfloat;
        instanceAttribs[0].offset = offsetof(VFXInstanceData, worldPosition);

        // location 4: color (vec4)
        instanceAttribs[1].binding = 1;
        instanceAttribs[1].location = 4;
        instanceAttribs[1].format = vk::Format::eR32G32B32A32Sfloat;
        instanceAttribs[1].offset = offsetof(VFXInstanceData, color);

        // location 5: lifetimeRatio (float)
        instanceAttribs[2].binding = 1;
        instanceAttribs[2].location = 5;
        instanceAttribs[2].format = vk::Format::eR32Sfloat;
        instanceAttribs[2].offset = offsetof(VFXInstanceData, lifetimeRatio);

        // location 6: rotation (float)
        instanceAttribs[3].binding = 1;
        instanceAttribs[3].location = 6;
        instanceAttribs[3].format = vk::Format::eR32Sfloat;
        instanceAttribs[3].offset = offsetof(VFXInstanceData, rotation);

        // location 7: glowIntensity (float)
        instanceAttribs[4].binding = 1;
        instanceAttribs[4].location = 7;
        instanceAttribs[4].format = vk::Format::eR32Sfloat;
        instanceAttribs[4].offset = offsetof(VFXInstanceData, glowIntensity);

        // VK-1476: orientation inputs (mesh preview only) — mirrors runtime GPUParticle fields.
        // location 8: velocity (vec3)
        instanceAttribs[5].binding = 1;
        instanceAttribs[5].location = 8;
        instanceAttribs[5].format = vk::Format::eR32G32B32Sfloat;
        instanceAttribs[5].offset = offsetof(VFXInstanceData, velocity);

        // location 9: spawnSeed (uint)
        instanceAttribs[6].binding = 1;
        instanceAttribs[6].location = 9;
        instanceAttribs[6].format = vk::Format::eR32Uint;
        instanceAttribs[6].offset = offsetof(VFXInstanceData, spawnSeed);

        // location 10: age (float, seconds since spawn)
        instanceAttribs[7].binding = 1;
        instanceAttribs[7].location = 10;
        instanceAttribs[7].format = vk::Format::eR32Sfloat;
        instanceAttribs[7].offset = offsetof(VFXInstanceData, age);

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), meshAttribs.begin(), meshAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {swapChain.getSceneColorFormat()},
            .depthAttachmentFormat = swapChain.getSwapchainDepthStencilFormat(),
            .shaderStages = meshShader->getShaderStages(),
            .vertexBindings = {meshBinding, instanceBinding},
            .vertexAttributes = std::move(allAttribs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(VFXMeshPreviewPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = true,
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

    void VFXMeshPreviewPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(VFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
        cameraUBOMapped = cameraUBOAllocation.mappedPtr;

        vk::DeviceSize instanceBufferSize = sizeof(VFXInstanceData) * maxInstances;
        core::BufferInfoRequest instanceRequest(vkDevice, device.getPhysicalDevice());
        instanceRequest.size = instanceBufferSize;
        instanceRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        instanceRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                     vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(instanceRequest, instanceBuffer, instanceBufferAllocation, device.getMemoryManager());
        instanceBufferMapped = instanceBufferAllocation.mappedPtr;
    }

    void VFXMeshPreviewPipeline::createDefaultTexture()
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

    void VFXMeshPreviewPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }

}
