#include "SkinnedMeshPipeline.hpp"
#include "../ibl/DefaultIBLTextureFactory.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Logger.hpp"
#include <stdexcept>

namespace render::mesh
{
    SkinnedMeshPipeline::SkinnedMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                             core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
    }

    SkinnedMeshPipeline::~SkinnedMeshPipeline()
    {
        cleanUp();
    }

    void SkinnedMeshPipeline::init()
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayouts();
        createDescriptorPools();
        createCameraUBO();
        createBoneSSBO();

        // Create default IBL textures
        defaultIBLFactory = std::make_unique<ibl::DefaultIBLTextureFactory>(device);
        defaultIBLFactory->createDefaultTextures(device.getStagingCommandPool());

        createDescriptorSets();
        createPipelineLayout();
        createGraphicsPipeline();
        createFramebuffers();
        usingDefaultTextures = true;
    }

    void SkinnedMeshPipeline::loadShaders()
    {
        skinnedMeshShader = std::make_shared<core::Shader>(device);
        skinnedMeshShader->readShader("../../resources/shaders/mesh/skinned_mesh.glsl");

        if (skinnedMeshShader->getShaderStages().empty())
        {
            throw std::runtime_error("Failed to load skinned_mesh.glsl shader");
        }
    }

    void SkinnedMeshPipeline::recreate()
    {
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);

        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
    }

    void SkinnedMeshPipeline::cleanUp()
    {
        auto logicalDevice = device.getLogicalDevice();

        logicalDevice.waitIdle();

        destroyMeshGPUBuffers();
        loadedMesh.reset();

        for (auto& framebuffer : framebuffers)
        {
            if (framebuffer)
            {
                logicalDevice.destroyFramebuffer(framebuffer);
            }
        }
        framebuffers.clear();

        if (graphicsPipeline)
        {
            logicalDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }
        if (pipelineLayout)
        {
            logicalDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }
        if (renderPass)
        {
            logicalDevice.destroyRenderPass(renderPass);
            renderPass = nullptr;
        }

        // Cleanup camera UBO
        if (cameraUBO)
        {
            logicalDevice.destroyBuffer(cameraUBO);
            cameraUBO = nullptr;
        }
        if (cameraUBOMemory)
        {
            logicalDevice.freeMemory(cameraUBOMemory);
            cameraUBOMemory = nullptr;
        }

        // Cleanup bone SSBO
        if (boneSSBOMapped)
        {
            logicalDevice.unmapMemory(boneSSBOMemory);
            boneSSBOMapped = nullptr;
        }
        if (boneSSBO)
        {
            logicalDevice.destroyBuffer(boneSSBO);
            boneSSBO = nullptr;
        }
        if (boneSSBOMemory)
        {
            logicalDevice.freeMemory(boneSSBOMemory);
            boneSSBOMemory = nullptr;
        }

        // Cleanup descriptor pools
        if (cameraIBLDescriptorPool)
        {
            logicalDevice.destroyDescriptorPool(cameraIBLDescriptorPool);
            cameraIBLDescriptorPool = nullptr;
        }
        if (textureDescriptorPool)
        {
            logicalDevice.destroyDescriptorPool(textureDescriptorPool);
            textureDescriptorPool = nullptr;
        }
        if (boneDescriptorPool)
        {
            logicalDevice.destroyDescriptorPool(boneDescriptorPool);
            boneDescriptorPool = nullptr;
        }

        // Cleanup descriptor set layouts
        if (cameraIBLDescriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(cameraIBLDescriptorSetLayout);
            cameraIBLDescriptorSetLayout = nullptr;
        }
        if (textureDescriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(textureDescriptorSetLayout);
            textureDescriptorSetLayout = nullptr;
        }
        if (boneDescriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(boneDescriptorSetLayout);
            boneDescriptorSetLayout = nullptr;
        }

        // Reset descriptor sets (they're freed when pool is destroyed)
        cameraIBLDescriptorSet = nullptr;
        textureDescriptorSet = nullptr;
        boneDescriptorSet = nullptr;

        defaultIBLFactory.reset();

        if (skinnedMeshShader)
        {
            skinnedMeshShader->cleanUp();
            skinnedMeshShader.reset();
        }
    }

    void SkinnedMeshPipeline::createRenderPass()
    {
        // Color attachment - clear to background color each frame
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

        // Depth attachment
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
        // Set 0: Camera UBO + IBL textures
        {
            std::vector<vk::DescriptorSetLayoutBinding> bindings(4);

            // Binding 0: Camera UBO
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

            // Binding 1: Irradiance cubemap
            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            // Binding 2: Prefilter cubemap
            bindings[2].binding = 2;
            bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

            // Binding 3: BRDF LUT
            bindings[3].binding = 3;
            bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[3].descriptorCount = 1;
            bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            cameraIBLDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
        }

        // Set 1: Material textures (empty for now - using default PBR values)
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

        // Set 2: Bone matrices SSBO
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
        // Pool for camera + IBL (set 0)
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

        // Pool for textures (set 1) - using defaults for now
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

        // Pool for bone matrices (set 2)
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

        // Map persistently
        vk::Result result = device.getLogicalDevice().mapMemory(boneSSBOMemory, 0, bufferSize, {}, &boneSSBOMapped);
        if (result != vk::Result::eSuccess)
        {
            loggerError("Failed to map bone SSBO memory");
            boneSSBOMapped = nullptr;
        }
        else
        {
            // Initialize with identity matrices
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
        // Allocate camera + IBL descriptor set
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = cameraIBLDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &cameraIBLDescriptorSetLayout;

            cameraIBLDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

            // Write camera UBO
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

            // Write IBL textures
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

        // Allocate bone SSBO descriptor set
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

        // Allocate texture descriptor set (empty/default for now)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = textureDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout;

            textureDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

            // For now, bind default IBL brdfLUT as placeholder for all texture slots
            // This is a workaround - proper textures would be bound when materials are supported
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

        // Three descriptor set layouts: set 0 (camera + IBL), set 1 (textures), set 2 (bones)
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

    bool SkinnedMeshPipeline::loadMesh(std::string_view meshPath)
    {
        unloadMesh();

        auto meshesData = resource::MeshStreamResource::loadAll(meshPath);
        if (meshesData.meshes.empty())
        {
            loggerError("Failed to load mesh: {}", meshPath);
            return false;
        }

        loadedMesh = std::make_unique<SkinnedMeshGPUData>();
        loadedMesh->meshPath = std::string(meshPath);
        loadedMesh->hasSkinning = meshesData.hasSkinning;

        createMeshGPUBuffers(meshesData);

        return true;
    }

    void SkinnedMeshPipeline::unloadMesh()
    {
        if (!loadedMesh) return;

        destroyMeshGPUBuffers();
        loadedMesh.reset();
    }

    void SkinnedMeshPipeline::createMeshGPUBuffers(const resource::MeshesData& meshesData)
    {
        if (!loadedMesh) return;

        bool boundingBoxInitialized = false;

        for (const auto& meshData : meshesData.meshes)
        {
            if (meshData.lodLevels.empty() || meshData.lodLevels[0].vertices.empty())
            {
                loggerWarning("Skipping empty submesh in skinned mesh");
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;

            // Compute bounding boxes from LOD0 vertices
            const auto& lod0 = meshData.lodLevels[0];
            bool subMeshBBInitialized = false;
            for (const auto& vertex : lod0.vertices)
            {
                if (!subMeshBBInitialized)
                {
                    subMesh.boundingBox.min = vertex.position;
                    subMesh.boundingBox.max = vertex.position;
                    subMeshBBInitialized = true;
                }
                else
                {
                    subMesh.boundingBox.expand(vertex.position);
                }

                if (!boundingBoxInitialized)
                {
                    loadedMesh->meshData.boundingBox.min = vertex.position;
                    loadedMesh->meshData.boundingBox.max = vertex.position;
                    boundingBoxInitialized = true;
                }
                else
                {
                    loadedMesh->meshData.boundingBox.expand(vertex.position);
                }
            }

            // Debug: Check bone index distribution in vertex data
            const auto& lod0Verts = meshData.lodLevels[0].vertices;
            std::unordered_map<int32_t, size_t> boneIndexCounts;
            size_t verticesWithWeights = 0;
            for (const auto& v : lod0Verts)
            {
                bool hasWeight = false;
                for (int i = 0; i < 4; ++i)
                {
                    if (v.boneIndices[i] >= 0 && v.boneWeights[i] > 0.0f)
                    {
                        boneIndexCounts[v.boneIndices[i]]++;
                        hasWeight = true;
                    }
                }
                if (hasWeight) verticesWithWeights++;
            }
            loggerInfo("Loaded mesh vertex bone data: {} verts with weights, {} unique bone indices",
                       verticesWithWeights, boneIndexCounts.size());
            if (boneIndexCounts.size() <= 5)
            {
                for (const auto& [idx, count] : boneIndexCounts)
                {
                    loggerInfo("  Bone index {}: {} vertex influences", idx, count);
                }
            }

            // Upload LOD levels
            uint32_t lodCount = static_cast<uint32_t>(std::min(meshData.lodLevels.size(),
                                                               static_cast<size_t>(resource::LOD_LEVEL_COUNT)));

            for (uint32_t lod = 0; lod < lodCount; ++lod)
            {
                const auto& srcLOD = meshData.lodLevels[lod];
                auto& dstLOD = subMesh.lodLevels[lod];

                if (srcLOD.vertices.empty())
                {
                    continue;
                }

                // Create vertex buffer
                vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * srcLOD.vertices.size();
                core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                vertexBufferRequest.size = vertexBufferSize;
                vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
                vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(vertexBufferRequest, dstLOD.vertexBuffer, dstLOD.vertexBufferMemory);

                core::BufferUtilities::copyToBuffer(
                    device.getLogicalDevice(),
                    device.getPhysicalDevice(),
                    device.getGraphicsQueue(),
                    device.getStagingCommandPool(),
                    dstLOD.vertexBuffer,
                    srcLOD.vertices.data(),
                    vertexBufferSize
                );

                // Create index buffer
                if (!srcLOD.indices.empty())
                {
                    vk::DeviceSize indexBufferSize = sizeof(uint32_t) * srcLOD.indices.size();
                    core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                    indexBufferRequest.size = indexBufferSize;
                    indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
                    indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                    core::BufferUtilities::createBuffer(indexBufferRequest, dstLOD.indexBuffer, dstLOD.indexBufferMemory);

                    core::BufferUtilities::copyToBuffer(
                        device.getLogicalDevice(),
                        device.getPhysicalDevice(),
                        device.getGraphicsQueue(),
                        device.getStagingCommandPool(),
                        dstLOD.indexBuffer,
                        srcLOD.indices.data(),
                        indexBufferSize
                    );
                }

                dstLOD.vertexCount = static_cast<uint32_t>(srcLOD.vertices.size());
                dstLOD.indexCount = static_cast<uint32_t>(srcLOD.indices.size());
            }

            // Fill remaining LODs with highest available LOD
            for (uint32_t lod = lodCount; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                const auto& srcLOD = meshData.lodLevels[lodCount - 1];
                auto& dstLOD = subMesh.lodLevels[lod];

                if (srcLOD.vertices.empty())
                {
                    continue;
                }

                // Create vertex buffer
                vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * srcLOD.vertices.size();
                core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                vertexBufferRequest.size = vertexBufferSize;
                vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
                vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(vertexBufferRequest, dstLOD.vertexBuffer, dstLOD.vertexBufferMemory);

                core::BufferUtilities::copyToBuffer(
                    device.getLogicalDevice(),
                    device.getPhysicalDevice(),
                    device.getGraphicsQueue(),
                    device.getStagingCommandPool(),
                    dstLOD.vertexBuffer,
                    srcLOD.vertices.data(),
                    vertexBufferSize
                );

                // Create index buffer
                if (!srcLOD.indices.empty())
                {
                    vk::DeviceSize indexBufferSize = sizeof(uint32_t) * srcLOD.indices.size();
                    core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                    indexBufferRequest.size = indexBufferSize;
                    indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
                    indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                    core::BufferUtilities::createBuffer(indexBufferRequest, dstLOD.indexBuffer, dstLOD.indexBufferMemory);

                    core::BufferUtilities::copyToBuffer(
                        device.getLogicalDevice(),
                        device.getPhysicalDevice(),
                        device.getGraphicsQueue(),
                        device.getStagingCommandPool(),
                        dstLOD.indexBuffer,
                        srcLOD.indices.data(),
                        indexBufferSize
                    );
                }

                dstLOD.vertexCount = static_cast<uint32_t>(srcLOD.vertices.size());
                dstLOD.indexCount = static_cast<uint32_t>(srcLOD.indices.size());
            }

            loadedMesh->meshData.subMeshes.push_back(std::move(subMesh));
        }
    }

    void SkinnedMeshPipeline::destroyMeshGPUBuffers()
    {
        if (!loadedMesh) return;

        auto logicalDevice = device.getLogicalDevice();

        for (auto& subMesh : loadedMesh->meshData.subMeshes)
        {
            for (auto& lod : subMesh.lodLevels)
            {
                if (lod.vertexBuffer)
                {
                    logicalDevice.destroyBuffer(lod.vertexBuffer);
                    logicalDevice.freeMemory(lod.vertexBufferMemory);
                    lod.vertexBuffer = nullptr;
                }
                if (lod.indexBuffer)
                {
                    logicalDevice.destroyBuffer(lod.indexBuffer);
                    logicalDevice.freeMemory(lod.indexBufferMemory);
                    lod.indexBuffer = nullptr;
                }
            }
        }
    }

    const math::AABB* SkinnedMeshPipeline::getMeshBoundingBox() const
    {
        if (!loadedMesh) return nullptr;
        return &loadedMesh->meshData.boundingBox;
    }

    void SkinnedMeshPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                              const glm::vec3& cameraPos, float time) const
    {
        currentView = view;
        currentProjection = projection;
        currentCameraPos = cameraPos;

        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(cameraUBOMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(cameraUBOMemory);
        }
    }

    void SkinnedMeshPipeline::updateBoneMatrices(const std::vector<glm::mat4>& boneMatrices)
    {
        if (!boneSSBOMapped) return;

        BoneMatricesSSBO* ssboData = static_cast<BoneMatricesSSBO*>(boneSSBOMapped);

        size_t boneCount = std::min(boneMatrices.size(), static_cast<size_t>(MAX_BONES));
        for (size_t i = 0; i < boneCount; ++i)
        {
            ssboData->boneMatrices[i] = boneMatrices[i];
        }
        ssboData->activeBoneCount = static_cast<uint32_t>(boneCount);
    }

    void SkinnedMeshPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                   uint32_t imageIndex,
                                                   const SkinnedMeshRenderData& renderData) const
    {
        if (!loadedMesh || loadedMesh->meshData.subMeshes.empty())
        {
            return;
        }

        // Begin render pass
        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D(0, 0);
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

        std::array<vk::ClearValue, 2> clearValues{};
        // Background color matching the preview viewport (bluish-gray)
        clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.36f, 0.38f, 0.48f, 1.0f});
        clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Bind pipeline
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        // Bind descriptor sets
        std::array<vk::DescriptorSet, 3> descriptorSets = {
            cameraIBLDescriptorSet,
            textureDescriptorSet,
            boneDescriptorSet
        };
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
            0, descriptorSets, nullptr);

        // Set push constants
        SkinnedMeshPushConstants pc{};
        pc.model = renderData.modelMatrix;
        pc.albedo = renderData.albedo;
        pc.metallic = renderData.metallic;
        pc.roughness = renderData.roughness;
        pc.ao = renderData.ao;
        pc.emission = renderData.emission;

        commandBuffer.pushConstants(pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(SkinnedMeshPushConstants), &pc);

        // Draw all submeshes at LOD 0
        for (const auto& subMesh : loadedMesh->meshData.subMeshes)
        {
            const auto& lod = subMesh.lodLevels[0]; // Always use LOD 0 for animation preview

            if (!lod.isValid()) continue;

            vk::Buffer vertexBuffers[] = {lod.vertexBuffer};
            vk::DeviceSize offsets[] = {0};

            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
            commandBuffer.bindIndexBuffer(lod.indexBuffer, 0, vk::IndexType::eUint32);
            commandBuffer.drawIndexed(lod.indexCount, 1, 0, 0, 0);
        }

        commandBuffer.endRenderPass();
    }
}
