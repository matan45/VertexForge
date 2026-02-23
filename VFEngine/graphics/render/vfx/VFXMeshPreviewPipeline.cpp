#include "VFXMeshPreviewPipeline.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"

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
                loggerError("VFXMeshPreviewPipeline: Failed to load shader");
                return;
            }

            createRenderPass();
            if (!renderPass)
            {
                loggerError("VFXMeshPreviewPipeline: Failed to create render pass");
                cleanUp();
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                loggerError("VFXMeshPreviewPipeline: Failed to create descriptor set layout");
                cleanUp();
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                loggerError("VFXMeshPreviewPipeline: Failed to create descriptor pool");
                cleanUp();
                return;
            }

            createBuffers();
            if (!cameraUBO || !instanceBuffer)
            {
                loggerError("VFXMeshPreviewPipeline: Failed to create buffers");
                cleanUp();
                return;
            }

            createDefaultTexture();
            createSampler();
            createDescriptorSet();

            createPipeline();
            if (!graphicsPipeline || !pipelineLayout)
            {
                loggerError("VFXMeshPreviewPipeline: Failed to create graphics pipeline");
                cleanUp();
                return;
            }

            createFramebuffers();

            initialized = true;
            loggerInfo("VFXMeshPreviewPipeline initialized");
        }
        catch (const vk::SystemError& e)
        {
            loggerError("VFXMeshPreviewPipeline: Vulkan error during init - {}", e.what());
            cleanUp();
        }
        catch (const std::exception& e)
        {
            loggerError("VFXMeshPreviewPipeline: Exception during init - {}", e.what());
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
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createRenderPass();
        createPipeline();
        createFramebuffers();
    }

    void VFXMeshPreviewPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& framebuffer : framebuffers)
        {
            dev.destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        if (graphicsPipeline) { dev.destroyPipeline(graphicsPipeline); graphicsPipeline = nullptr; }
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

        if (renderPass) { dev.destroyRenderPass(renderPass); renderPass = nullptr; }

        if (cameraUBOMapped && cameraUBOMemory)
        {
            dev.unmapMemory(cameraUBOMemory);
            cameraUBOMapped = nullptr;
        }
        if (cameraUBO)
        {
            dev.destroyBuffer(cameraUBO);
            dev.freeMemory(cameraUBOMemory);
            cameraUBO = nullptr;
        }
        if (instanceBufferMapped && instanceBufferMemory)
        {
            dev.unmapMemory(instanceBufferMemory);
            instanceBufferMapped = nullptr;
        }
        if (instanceBuffer)
        {
            dev.destroyBuffer(instanceBuffer);
            dev.freeMemory(instanceBufferMemory);
            instanceBuffer = nullptr;
        }

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
            dev.freeMemory(defaultTextureMemory);
            defaultTextureImage = nullptr;
            defaultTextureMemory = nullptr;
        }

        if (meshShader)
        {
            meshShader->cleanUp();
            meshShader.reset();
        }

        currentInstanceCount = 0;
        initialized = false;
    }

    void VFXMeshPreviewPipeline::createRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                  vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dependency.srcAccessMask = vk::AccessFlags{};
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                  vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                                   vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
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

        std::array<vk::VertexInputAttributeDescription, 5> instanceAttribs{};
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

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), meshAttribs.begin(), meshAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
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
            .blendEnable = true
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void VFXMeshPreviewPipeline::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());
        vk::ImageView depth = offscreenResources.depthImage.depthImageView;

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView colorView = offscreenResources.colorImages[i].colorImageView;
            std::array<vk::ImageView, 2> attachments = {colorView, depth};

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChain.getSwapchainExtent().width;
            framebufferInfo.height = swapChain.getSwapchainExtent().height;
            framebufferInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
        }
    }

    void VFXMeshPreviewPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(VFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOMemory);
        cameraUBOMapped = vkDevice.mapMemory(cameraUBOMemory, 0, sizeof(VFXCameraUBO));

        vk::DeviceSize instanceBufferSize = sizeof(VFXInstanceData) * maxInstances;
        core::BufferInfoRequest instanceRequest(vkDevice, device.getPhysicalDevice());
        instanceRequest.size = instanceBufferSize;
        instanceRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        instanceRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                     vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(instanceRequest, instanceBuffer, instanceBufferMemory);
        instanceBufferMapped = vkDevice.mapMemory(instanceBufferMemory, 0, instanceBufferSize);
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
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D{texSize, texSize, 1};

        cmd->copyBufferToImage(stagingBuffer, defaultTextureImage, vk::ImageLayout::eTransferDstOptimal, region);

        core::ImageUtilities::transitionImageLayout(cmd.get(), defaultTextureImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingMemory);
    }

    void VFXMeshPreviewPipeline::createSampler()
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

}
