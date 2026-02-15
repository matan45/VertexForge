#include "SkinnedMeshPipeline.hpp"
#include "../ibl/DefaultIBLTextureFactory.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"

namespace render::mesh
{
    void SkinnedMeshPipeline::createRenderPass()
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

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void SkinnedMeshPipeline::createDescriptorSetLayouts()
    {
        {
            std::vector<vk::DescriptorSetLayoutBinding> bindings(4);

            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[2].binding = 2;
            bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[3].binding = 3;
            bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[3].descriptorCount = 1;
            bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            cameraIBLDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
        }

        {
            vk::DescriptorSetLayoutBinding textureBinding{};
            textureBinding.binding = 0;
            textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            textureBinding.descriptorCount = 16;
            textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &textureBinding;

            textureDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
        }

        {
            vk::DescriptorSetLayoutBinding boneBinding{};
            boneBinding.binding = 0;
            boneBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
            boneBinding.descriptorCount = 1;
            boneBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &boneBinding;

            boneDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
        }
    }

    void SkinnedMeshPipeline::createDescriptorPools()
    {
        {
            std::array<vk::DescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
            poolSizes[0].descriptorCount = 1;
            poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
            poolSizes[1].descriptorCount = 3; // irradiance, prefilter, brdfLUT

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = 1;

            cameraIBLDescriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
        }

        {
            vk::DescriptorPoolSize poolSize{};
            poolSize.type = vk::DescriptorType::eCombinedImageSampler;
            poolSize.descriptorCount = 16;

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            poolInfo.maxSets = 1;

            textureDescriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
        }

        {
            vk::DescriptorPoolSize poolSize{};
            poolSize.type = vk::DescriptorType::eStorageBuffer;
            poolSize.descriptorCount = 1;

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            poolInfo.maxSets = 1;

            boneDescriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
        }
    }

    void SkinnedMeshPipeline::createCameraUBO()
    {
        vk::DeviceSize bufferSize = sizeof(CameraUBO);

        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.size = bufferSize;
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufferRequest, cameraUBO, cameraUBOMemory);
    }

    void SkinnedMeshPipeline::createBoneSSBO()
    {
        vk::DeviceSize bufferSize = sizeof(BoneMatricesSSBO);

        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.size = bufferSize;
        bufferRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufferRequest, boneSSBO, boneSSBOMemory);

        vk::Result result = device.getLogicalDevice().mapMemory(boneSSBOMemory, 0, bufferSize, {}, &boneSSBOMapped);
        if (result != vk::Result::eSuccess)
        {
            loggerError("Failed to map bone SSBO memory");
            boneSSBOMapped = nullptr;
        }
        else
        {
            BoneMatricesSSBO initData{};
            for (int i = 0; i < MAX_BONES; ++i)
            {
                initData.boneMatrices[i] = glm::mat4(1.0f);
            }
            initData.activeBoneCount = 0;
            memcpy(boneSSBOMapped, &initData, sizeof(BoneMatricesSSBO));
        }
    }

    void SkinnedMeshPipeline::createDescriptorSets()
    {
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = cameraIBLDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &cameraIBLDescriptorSetLayout;

            cameraIBLDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

            vk::DescriptorBufferInfo uboBufferInfo{};
            uboBufferInfo.buffer = cameraUBO;
            uboBufferInfo.offset = 0;
            uboBufferInfo.range = sizeof(CameraUBO);

            vk::WriteDescriptorSet uboWrite{};
            uboWrite.dstSet = cameraIBLDescriptorSet;
            uboWrite.dstBinding = 0;
            uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
            uboWrite.descriptorCount = 1;
            uboWrite.pBufferInfo = &uboBufferInfo;

            const auto& irradiance = defaultIBLFactory->getIrradiance();
            const auto& prefilter = defaultIBLFactory->getPrefilter();
            const auto& brdfLUT = defaultIBLFactory->getBrdfLUT();

            vk::DescriptorImageInfo irradianceInfo{};
            irradianceInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            irradianceInfo.imageView = irradiance.imageView;
            irradianceInfo.sampler = irradiance.sampler;

            vk::WriteDescriptorSet irradianceWrite{};
            irradianceWrite.dstSet = cameraIBLDescriptorSet;
            irradianceWrite.dstBinding = 1;
            irradianceWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            irradianceWrite.descriptorCount = 1;
            irradianceWrite.pImageInfo = &irradianceInfo;

            vk::DescriptorImageInfo prefilterInfo{};
            prefilterInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            prefilterInfo.imageView = prefilter.imageView;
            prefilterInfo.sampler = prefilter.sampler;

            vk::WriteDescriptorSet prefilterWrite{};
            prefilterWrite.dstSet = cameraIBLDescriptorSet;
            prefilterWrite.dstBinding = 2;
            prefilterWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            prefilterWrite.descriptorCount = 1;
            prefilterWrite.pImageInfo = &prefilterInfo;

            vk::DescriptorImageInfo brdfInfo{};
            brdfInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            brdfInfo.imageView = brdfLUT.imageView;
            brdfInfo.sampler = brdfLUT.sampler;

            vk::WriteDescriptorSet brdfWrite{};
            brdfWrite.dstSet = cameraIBLDescriptorSet;
            brdfWrite.dstBinding = 3;
            brdfWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            brdfWrite.descriptorCount = 1;
            brdfWrite.pImageInfo = &brdfInfo;

            std::array<vk::WriteDescriptorSet, 4> descriptorWrites = {
                uboWrite, irradianceWrite, prefilterWrite, brdfWrite
            };
            device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = boneDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &boneDescriptorSetLayout;

            boneDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

            vk::DescriptorBufferInfo boneBufferInfo{};
            boneBufferInfo.buffer = boneSSBO;
            boneBufferInfo.offset = 0;
            boneBufferInfo.range = sizeof(BoneMatricesSSBO);

            vk::WriteDescriptorSet boneWrite{};
            boneWrite.dstSet = boneDescriptorSet;
            boneWrite.dstBinding = 0;
            boneWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
            boneWrite.descriptorCount = 1;
            boneWrite.pBufferInfo = &boneBufferInfo;

            device.getLogicalDevice().updateDescriptorSets(boneWrite, nullptr);
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = textureDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout;

            textureDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

            std::array<vk::DescriptorImageInfo, 16> imageInfos;
            for (auto& info : imageInfos)
            {
                info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                info.imageView = defaultIBLFactory->getBrdfLUT().imageView;
                info.sampler = defaultIBLFactory->getBrdfLUT().sampler;
            }

            vk::WriteDescriptorSet textureWrite{};
            textureWrite.dstSet = textureDescriptorSet;
            textureWrite.dstBinding = 0;
            textureWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            textureWrite.descriptorCount = 16;
            textureWrite.pImageInfo = imageInfos.data();

            device.getLogicalDevice().updateDescriptorSets(textureWrite, nullptr);
        }
    }

    void SkinnedMeshPipeline::createPipelineLayout()
    {
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SkinnedMeshPushConstants);

        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            cameraIBLDescriptorSetLayout,
            textureDescriptorSetLayout,
            boneDescriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    }

    void SkinnedMeshPipeline::createGraphicsPipeline()
    {
        auto bindingDescription = SkinnedMeshVertexInput::getBindingDescription();
        auto attributeDescriptions = SkinnedMeshVertexInput::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
        viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D(0, 0);
        scissor.extent = swapChain.getSwapchainExtent();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(skinnedMeshShader->getShaderStages().size());
        pipelineInfo.pStages = skinnedMeshShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void SkinnedMeshPipeline::createFramebuffers()
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
}
