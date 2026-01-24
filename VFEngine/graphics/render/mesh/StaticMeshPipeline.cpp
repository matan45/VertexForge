#include "StaticMeshPipeline.hpp"
#include "MeshGPUCache.hpp"
#include "MaterialCacheManager.hpp"
#include "../DebugRenderer.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialShaderCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../ibl/DefaultIBLTextureFactory.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include "resource/ResourceManager.hpp"
#include "material/MaterialManager.hpp"
#include "material/MaterialTypes.hpp"
#include "math/Frustum.hpp"
#include "print/Logger.hpp"
#include <algorithm>

namespace render::mesh
{
    StaticMeshPipeline::StaticMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                           core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
        meshCache = std::make_unique<MeshGPUCache>(device);

        textureCache = std::make_unique<MaterialTextureCache>(device);
        textureCache->init(device.getStagingCommandPool());

        materialShaderCache = std::make_unique<MaterialShaderCache>(device);

        materialCacheManager = std::make_unique<MaterialCacheManager>();
        materialCacheManager->setShaderCache(materialShaderCache.get());
        materialCacheManager->setTextureCache(textureCache.get());
    }

    StaticMeshPipeline::~StaticMeshPipeline()
    {
        if (materialChangeCallbackId) {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
        }
    }

    void StaticMeshPipeline::registerMaterialChangeCallback()
    {
        if (!materialChangeCallbackId) {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath) {
                    if (materialCacheManager) {
                        materialCacheManager->invalidate(materialPath);
                    }
                });
        }
    }

    void StaticMeshPipeline::init(const ibl::ImageData& irradianceMap,
                                  const ibl::ImageData& prefilterMap,
                                  const ibl::ImageData& brdfLUT)
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout();
        createDescriptorPool();
        createTextureDescriptorPool();
        createCameraUBO();
        createDescriptorSet(irradianceMap, prefilterMap, brdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(renderPass, pipelineLayout, swapChain.getSwapchainExtent());
        initializeDefaultTextureDescriptors();
        createFramebuffers();
        registerMaterialChangeCallback();
    }

    void StaticMeshPipeline::initWithDefaults()
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout();
        createDescriptorPool();
        createTextureDescriptorPool();
        createCameraUBO();

        defaultIBLFactory = std::make_unique<ibl::DefaultIBLTextureFactory>(device);
        defaultIBLFactory->createDefaultTextures(device.getStagingCommandPool());

        createDescriptorSet(defaultIBLFactory->getIrradiance(),
                            defaultIBLFactory->getPrefilter(),
                            defaultIBLFactory->getBrdfLUT());
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(renderPass, pipelineLayout, swapChain.getSwapchainExtent());
        initializeDefaultTextureDescriptors();
        createFramebuffers();
        usingDefaultTextures = true;
        registerMaterialChangeCallback();
    }

    void StaticMeshPipeline::loadShaders()
    {
        meshShader = std::make_shared<core::Shader>(device);
        meshShader->readShader("../../resources/shaders/mesh/mesh.glsl");
    }

    void StaticMeshPipeline::recreate()
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

    void StaticMeshPipeline::createRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
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

    void StaticMeshPipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(4);

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;
        bindings[0].pImmutableSamplers = nullptr;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].pImmutableSamplers = nullptr;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[2].pImmutableSamplers = nullptr;

        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[3].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void StaticMeshPipeline::createDescriptorPool()
    {
        std::vector<vk::DescriptorPoolSize> poolSizes(2);
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 3; // irradiance, prefilter, brdfLUT

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void StaticMeshPipeline::createCameraUBO()
    {
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(CameraUBO);
        core::BufferUtilities::createBuffer(bufferRequest, cameraUBO, cameraUBOMemory);
    }

    void StaticMeshPipeline::createDescriptorSet(const ibl::ImageData& irradianceMap,
                                                 const ibl::ImageData& prefilterMap,
                                                 const ibl::ImageData& brdfLUT)
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = cameraUBO;
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(CameraUBO);

        vk::WriteDescriptorSet uboWrite{};
        uboWrite.dstSet = descriptorSet;
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &uboBufferInfo;

        vk::DescriptorImageInfo irradianceImageInfo{};
        irradianceImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        irradianceImageInfo.imageView = irradianceMap.imageView;
        irradianceImageInfo.sampler = irradianceMap.sampler;

        vk::WriteDescriptorSet irradianceWrite{};
        irradianceWrite.dstSet = descriptorSet;
        irradianceWrite.dstBinding = 1;
        irradianceWrite.dstArrayElement = 0;
        irradianceWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        irradianceWrite.descriptorCount = 1;
        irradianceWrite.pImageInfo = &irradianceImageInfo;

        vk::DescriptorImageInfo prefilterImageInfo{};
        prefilterImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        prefilterImageInfo.imageView = prefilterMap.imageView;
        prefilterImageInfo.sampler = prefilterMap.sampler;

        vk::WriteDescriptorSet prefilterWrite{};
        prefilterWrite.dstSet = descriptorSet;
        prefilterWrite.dstBinding = 2;
        prefilterWrite.dstArrayElement = 0;
        prefilterWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        prefilterWrite.descriptorCount = 1;
        prefilterWrite.pImageInfo = &prefilterImageInfo;

        vk::DescriptorImageInfo brdfImageInfo{};
        brdfImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        brdfImageInfo.imageView = brdfLUT.imageView;
        brdfImageInfo.sampler = brdfLUT.sampler;

        vk::WriteDescriptorSet brdfWrite{};
        brdfWrite.dstSet = descriptorSet;
        brdfWrite.dstBinding = 3;
        brdfWrite.dstArrayElement = 0;
        brdfWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        brdfWrite.descriptorCount = 1;
        brdfWrite.pImageInfo = &brdfImageInfo;

        std::array<vk::WriteDescriptorSet, 4> descriptorWrites = {
            uboWrite, irradianceWrite, prefilterWrite, brdfWrite
        };
        device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    void StaticMeshPipeline::createTextureDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding textureBinding{};
        textureBinding.binding = 0;
        textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        textureBinding.descriptorCount = material::MAX_MATERIAL_TEXTURES; // 6 textures per material
        textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        textureBinding.pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &textureBinding;

        textureDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void StaticMeshPipeline::createTextureDescriptorPool()
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = material::MAX_MATERIAL_TEXTURES;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        textureDescriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void StaticMeshPipeline::initializeDefaultTextureDescriptors()
    {
        textureCache->initDescriptorResources(textureDescriptorSetLayout);
        
        if (!textureCache->hasDefaultTexture())
        {
            loggerWarning("Default texture not available, skipping default descriptor set creation");
            textureDescriptorsInitialized = false;
            return;
        }
        
        if (!textureDescriptorSet)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = textureDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout;
            textureDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
            
            std::array<vk::DescriptorImageInfo, material::MAX_MATERIAL_TEXTURES> imageInfos;
            for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i)
            {
                imageInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                imageInfos[i].imageView = textureCache->getDefaultView();
                imageInfos[i].sampler = textureCache->getDefaultSampler();
            }

            vk::WriteDescriptorSet writeSet{};
            writeSet.dstSet = textureDescriptorSet;
            writeSet.dstBinding = 0;
            writeSet.dstArrayElement = 0;
            writeSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writeSet.descriptorCount = material::MAX_MATERIAL_TEXTURES;
            writeSet.pImageInfo = imageInfos.data();

            device.getLogicalDevice().updateDescriptorSets(writeSet, nullptr);
        }
        textureDescriptorsInitialized = true;
    }

    void StaticMeshPipeline::updatePreviewTextureDescriptors(
        const std::array<vk::ImageView, material::MAX_MATERIAL_TEXTURES>& imageViews,
        const std::array<vk::Sampler, material::MAX_MATERIAL_TEXTURES>& samplers)
    {
        if (!textureDescriptorSet) return;

        std::array<vk::DescriptorImageInfo, material::MAX_MATERIAL_TEXTURES> imageInfos;
        for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i)
        {
            imageInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageInfos[i].imageView = imageViews[i];
            imageInfos[i].sampler = samplers[i];
        }

        vk::WriteDescriptorSet writeSet{};
        writeSet.dstSet = textureDescriptorSet;
        writeSet.dstBinding = 0;
        writeSet.dstArrayElement = 0;
        writeSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writeSet.descriptorCount = material::MAX_MATERIAL_TEXTURES;
        writeSet.pImageInfo = imageInfos.data();

        device.getLogicalDevice().updateDescriptorSets(writeSet, nullptr);
    }

    void StaticMeshPipeline::createPipelineLayout()
    {
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(MeshPushConstants);

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            descriptorSetLayout,
            textureDescriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    }

    void StaticMeshPipeline::createGraphicsPipeline()
    {
        auto bindingDescription = MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = MeshVertexInput::getAttributeDescriptions();

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
        pipelineInfo.stageCount = static_cast<uint32_t>(meshShader->getShaderStages().size());
        pipelineInfo.pStages = meshShader->getShaderStages().data();
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

    void StaticMeshPipeline::createFramebuffers()
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

    void StaticMeshPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos, float time) const
    {
        currentView = view;
        currentProjection = projection;
        currentCameraPos = cameraPos;
        currentTime = time;

        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        math::extractFrustumPlanes(projection * view, ubo.frustumPlanes);

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(cameraUBOMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(cameraUBOMemory);
        }
    }

    void StaticMeshPipeline::cleanUpForReinit()
    {
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        if (cameraUBO)
        {
            device.getLogicalDevice().destroyBuffer(cameraUBO);
            device.getLogicalDevice().freeMemory(cameraUBOMemory);
            cameraUBO = nullptr;
            cameraUBOMemory = nullptr;
        }

        if (renderPass)
            device.getLogicalDevice().destroyRenderPass(renderPass);
        if (graphicsPipeline)
            device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        if (pipelineLayout)
            device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        if (descriptorPool)
        {
            if (descriptorSet)
                device.getLogicalDevice().freeDescriptorSets(descriptorPool, descriptorSet);
            device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        }
        if (descriptorSetLayout)
            device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);

        if (textureCache)
        {
            textureCache->resetDescriptorResources();
        }

        if (textureDescriptorPool)
        {
            if (textureDescriptorSet)
                device.getLogicalDevice().freeDescriptorSets(textureDescriptorPool, textureDescriptorSet);
            device.getLogicalDevice().destroyDescriptorPool(textureDescriptorPool);
            textureDescriptorPool = nullptr;
            textureDescriptorSet = nullptr;
        }
        if (textureDescriptorSetLayout)
        {
            device.getLogicalDevice().destroyDescriptorSetLayout(textureDescriptorSetLayout);
            textureDescriptorSetLayout = nullptr;
        }
        textureDescriptorsInitialized = false;

        renderPass = nullptr;
        graphicsPipeline = nullptr;
        pipelineLayout = nullptr;
        descriptorSet = nullptr;
        descriptorPool = nullptr;
        descriptorSetLayout = nullptr;

        if (usingDefaultTextures && defaultIBLFactory)
        {
            defaultIBLFactory->cleanup();
            defaultIBLFactory.reset();
            usingDefaultTextures = false;
        }
    }

    void StaticMeshPipeline::cleanUp()
    {
        if (materialCacheManager)
        {
            materialCacheManager->clear();
        }
        
        if (materialShaderCache)
        {
            materialShaderCache->cleanUp();
        }
        
        if (textureCache)
        {
            textureCache->cleanUp();
        }
        
        unloadAllMeshes();
        
        cleanUpForReinit();
        
        textureCache.reset();
        meshCache.reset();
        materialCacheManager.reset();
    }

    void StaticMeshPipeline::cleanUpShader()
    {
        meshShader->cleanUp();
    }

    void StaticMeshPipeline::injectMaterialForPreview(const std::string& materialPath,
                                                      std::shared_ptr<material::MaterialData> materialData)
    {
        if (materialCacheManager)
        {
            materialCacheManager->injectForPreview(materialPath, materialData);
        }
    }

    std::string StaticMeshPipeline::getLastShaderCompilationError() const
    {
        if (materialShaderCache)
        {
            return materialShaderCache->getLastCompilationError();
        }
        return "";
    }

    std::string StaticMeshPipeline::loadMesh(std::string_view meshPath)
    {
        return meshCache->loadMesh(meshPath);
    }

    std::string StaticMeshPipeline::uploadMesh(const std::string& meshId, const resource::MeshesData& meshesData)
    {
        return meshCache->uploadMesh(meshId, meshesData);
    }

    void StaticMeshPipeline::unloadMesh(const std::string& meshId)
    {
        meshCache->unloadMesh(meshId);
    }

    void StaticMeshPipeline::unloadAllMeshes()
    {
        meshCache->unloadAllMeshes();
    }

    const MeshGPUData* StaticMeshPipeline::getMesh(const std::string& meshId) const
    {
        return meshCache->getMesh(meshId);
    }

    bool StaticMeshPipeline::isMeshLoaded(const std::string& meshId) const
    {
        return meshCache->isMeshLoaded(meshId);
    }

    const math::AABB* StaticMeshPipeline::getMeshBoundingBox(const std::string& meshId) const
    {
        return meshCache->getMeshBoundingBox(meshId);
    }

    material::BlendMode StaticMeshPipeline::getMaterialBlendMode(const std::string& materialPath) const
    {
        if (materialPath.empty())
        {
            return material::BlendMode::Opaque;
        }

        auto materialData = materialCacheManager->getMaterial(materialPath);
        if (materialData)
        {
            return materialData->blendMode;
        }

        return material::BlendMode::Opaque;
    }

    std::vector<std::string> StaticMeshPipeline::getLoadedMeshIds() const
    {
        return meshCache->getLoadedMeshIds();
    }

    void StaticMeshPipeline::collectSortedSubmeshes(
        const std::vector<MeshRenderData>& meshDrawList,
        const math::Frustum* frustum,
        const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
        std::vector<SortedSubmesh>& opaqueSubmeshes,
        std::vector<SortedSubmesh>& maskedSubmeshes) const
    {
        opaqueSubmeshes.reserve(256);
        maskedSubmeshes.reserve(64);

        for (const auto& meshData : meshDrawList)
        {
            const MeshGPUData* gpuData = getMesh(meshData.meshPath);
            if (!gpuData || gpuData->subMeshes.empty()) continue;

            for (size_t subMeshIndex = 0; subMeshIndex < gpuData->subMeshes.size(); ++subMeshIndex)
            {
                const auto& subMesh = gpuData->subMeshes[subMeshIndex];

                if (frustum && frustum->isInitialized() &&
                    !frustum->intersectsAABB(subMesh.boundingBox, meshData.modelMatrix))
                    continue;

                ExtractedPBRValues pbrValues = MaterialPBRExtractor::getPBRForSubmesh(
                    meshData, subMesh.name, materialCache, currentTime);

                if (pbrValues.blendMode == material::BlendMode::Opaque)
                {
                    opaqueSubmeshes.push_back({&meshData, &subMesh, subMeshIndex, pbrValues.materialPath});
                }
                else if (pbrValues.blendMode == material::BlendMode::Masked)
                {
                    maskedSubmeshes.push_back({&meshData, &subMesh, subMeshIndex, pbrValues.materialPath});
                }
            }
        }

        auto materialSortComparator = [](const SortedSubmesh& a, const SortedSubmesh& b) {
            return a.materialPath < b.materialPath;
        };
        std::sort(opaqueSubmeshes.begin(), opaqueSubmeshes.end(), materialSortComparator);
        std::sort(maskedSubmeshes.begin(), maskedSubmeshes.end(), materialSortComparator);
    }

    void StaticMeshPipeline::renderSubmesh(
        const vk::CommandBuffer& commandBuffer,
        const MeshRenderData& meshData,
        const SubMeshGPUData& subMesh,
        size_t subMeshIndex,
        material::BlendMode targetBlendMode,
        const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
        RenderState& state) const
    {
        ExtractedPBRValues pbrValues = MaterialPBRExtractor::getPBRForSubmesh(
            meshData, subMesh.name, materialCache, currentTime);
        
        if (pbrValues.blendMode != targetBlendMode) return;
        
        vk::Pipeline targetPipeline = graphicsPipeline;

        if (materialShaderCache && !pbrValues.materialPath.empty())
        {
            auto matIt = materialCache.find(pbrValues.materialPath);
            if (matIt != materialCache.end() && matIt->second)
            {
                const material::MaterialData& matData = *matIt->second;
                if (!matData.cachedVertexShader.empty() && !matData.cachedFragmentShader.empty())
                {
                    const MaterialPipelineData* matPipeline =
                        materialShaderCache->getOrCreatePipeline(pbrValues.materialPath, matData);
                    if (matPipeline && matPipeline->valid)
                    {
                        targetPipeline = (pbrValues.blendMode == material::BlendMode::Masked)
                            ? matPipeline->maskedPipeline
                            : matPipeline->opaquePipeline;
                    }
                }
            }
        }

        if (state.currentPipeline != targetPipeline)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, targetPipeline);
            state.currentPipeline = targetPipeline;
        }

        vk::DescriptorSet materialDescSet = nullptr;
        bool hasAnyTexture = !pbrValues.albedoTexturePath.empty() ||
            !pbrValues.normalTexturePath.empty() ||
            !pbrValues.ormTexturePath.empty() ||
            !pbrValues.metallicTexturePath.empty() ||
            !pbrValues.roughnessTexturePath.empty() ||
            !pbrValues.aoTexturePath.empty() ||
            !pbrValues.emissionTexturePath.empty();

        if (hasAnyTexture && !pbrValues.materialPath.empty())
        {
            MaterialTexturePaths texPaths;
            texPaths.albedo = pbrValues.albedoTexturePath;
            texPaths.normal = pbrValues.normalTexturePath;
            texPaths.orm = pbrValues.ormTexturePath;
            texPaths.metallic = pbrValues.metallicTexturePath;
            texPaths.roughness = pbrValues.roughnessTexturePath;
            texPaths.ao = pbrValues.aoTexturePath;
            texPaths.emission = pbrValues.emissionTexturePath;

            materialDescSet = textureCache->getOrCreateMaterialDescriptorSet(
                pbrValues.materialPath, texPaths);
        }

        if (materialDescSet && materialDescSet != state.currentMaterialDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, materialDescSet, nullptr);
            state.currentMaterialDescriptorSet = materialDescSet;
        }
        else if (!materialDescSet && state.currentMaterialDescriptorSet != textureDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, textureDescriptorSet, nullptr);
            state.currentMaterialDescriptorSet = textureDescriptorSet;
        }

        MeshPushConstants pushConstants{};
        pushConstants.model = meshData.modelMatrix;
        pushConstants.metallic = pbrValues.metallic;
        pushConstants.roughness = pbrValues.roughness;
        pushConstants.ao = pbrValues.ao;
        pushConstants.blendMode = static_cast<float>(pbrValues.blendMode);

        auto getTexIdx = [&](material::TextureSlot slot, const std::string& pbrPath) -> uint8_t {
            if (!pbrPath.empty()) {
                return static_cast<uint8_t>(material::toIndex(slot));
            }
            float explicitIdx = meshData.textureIndices[material::toIndex(slot)];
            if (explicitIdx >= 0.0f) {
                return static_cast<uint8_t>(explicitIdx);
            }
            return TEXTURE_INDEX_NONE;
        };

        pushConstants.textureIndicesPacked[0] = packTextureIndices(
            getTexIdx(material::TextureSlot::Albedo, pbrValues.albedoTexturePath),
            getTexIdx(material::TextureSlot::Normal, pbrValues.normalTexturePath),
            getTexIdx(material::TextureSlot::ORM, pbrValues.ormTexturePath),
            getTexIdx(material::TextureSlot::Metallic, pbrValues.metallicTexturePath)
        );
        pushConstants.textureIndicesPacked[1] = packTextureIndices(
            getTexIdx(material::TextureSlot::Roughness, pbrValues.roughnessTexturePath),
            getTexIdx(material::TextureSlot::AO, pbrValues.aoTexturePath),
            getTexIdx(material::TextureSlot::Emission, pbrValues.emissionTexturePath),
            getTexIdx(material::TextureSlot::Height, pbrValues.heightTexturePath)
        );
        pushConstants.textureIndicesPacked[2] = packTextureIndices(TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE);
        pushConstants.textureIndicesPacked[3] = packTextureIndices(TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE);

        pushConstants.iblDiffuse = pbrValues.iblDiffuse;
        pushConstants.iblSpecular = pbrValues.iblSpecular;

        if (meshData.highlightedSubMesh >= 0 &&
            static_cast<size_t>(meshData.highlightedSubMesh) == subMeshIndex)
        {
            pushConstants.albedo = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            pushConstants.emission = 0.8f;
        }
        else
        {
            pushConstants.albedo = pbrValues.albedo;
            pushConstants.emission = pbrValues.emission;
        }

        commandBuffer.pushConstants(pipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(MeshPushConstants), &pushConstants);

        uint32_t lodLevel = (meshData.forceLODLevel >= 0 && meshData.forceLODLevel < static_cast<int>(resource::LOD_LEVEL_COUNT))
            ? static_cast<uint32_t>(meshData.forceLODLevel) : 0;
        const auto& lodBuffers = subMesh.getLOD(lodLevel);

        if (!lodBuffers.isValid()) return;

        vk::Buffer vertexBuffers[] = {lodBuffers.vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        if (lodBuffers.indexCount > 0)
        {
            commandBuffer.bindIndexBuffer(lodBuffers.indexBuffer, 0, vk::IndexType::eUint32);
            commandBuffer.drawIndexed(lodBuffers.indexCount, 1, 0, 0, 0);
        }
        else
        {
            commandBuffer.draw(lodBuffers.vertexCount, 1, 0, 0);
        }
    }

    void StaticMeshPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                 uint32_t imageIndex,
                                                 const std::vector<MeshRenderData>& meshDrawList,
                                                 const math::Frustum* frustum,
                                                 render::DebugRenderer* debugRenderer,
                                                 const glm::mat4& debugView,
                                                 const glm::mat4& debugProjection) const
    {
        bool hasDebugItems = debugRenderer && debugRenderer->hasItemsToRender();
        if (meshDrawList.empty() && !hasDebugItems)
        {
            return;
        }

        if (!meshDrawList.empty())
        {
            prepareTexturesForFrame(meshDrawList);
        }

        auto cacheLock = materialCacheManager->acquireSharedLock();
        const auto& materialCache = materialCacheManager->getCache();

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        if (textureDescriptorsInitialized && textureDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, textureDescriptorSet, nullptr);
        }

        std::vector<SortedSubmesh> opaqueSubmeshes;
        std::vector<SortedSubmesh> maskedSubmeshes;
        collectSortedSubmeshes(meshDrawList, frustum, materialCache, opaqueSubmeshes, maskedSubmeshes);

        RenderState state;
        state.currentMaterialDescriptorSet = textureDescriptorSet;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        state.currentPipeline = graphicsPipeline;

        for (const auto& item : opaqueSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Opaque, materialCache, state);
        }

        for (const auto& item : maskedSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Masked, materialCache, state);
        }

        if (debugRenderer && debugRenderer->hasItemsToRender())
        {
            debugRenderer->render(commandBuffer, meshDrawList, debugView, debugProjection,
                                  [this](const std::string& meshId) { return getMesh(meshId); });
        }

        commandBuffer.endRenderPass();
    }

    void StaticMeshPipeline::renderMeshList(const vk::CommandBuffer& commandBuffer,
                                            uint32_t imageIndex,
                                            const std::vector<MeshRenderData>& meshDrawList,
                                            const math::Frustum* frustum) const
    {
        if (meshDrawList.empty())
        {
            return;
        }

        prepareTexturesForFrame(meshDrawList);

        auto cacheLock = materialCacheManager->acquireSharedLock();
        const auto& materialCache = materialCacheManager->getCache();

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        if (textureDescriptorsInitialized && textureDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, textureDescriptorSet, nullptr);
        }

        std::vector<SortedSubmesh> opaqueSubmeshes;
        std::vector<SortedSubmesh> maskedSubmeshes;
        collectSortedSubmeshes(meshDrawList, frustum, materialCache, opaqueSubmeshes, maskedSubmeshes);

        RenderState state;
        state.currentMaterialDescriptorSet = textureDescriptorSet;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        state.currentPipeline = graphicsPipeline;

        for (const auto& item : opaqueSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Opaque, materialCache, state);
        }

        for (const auto& item : maskedSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Masked, materialCache, state);
        }
    }

    void StaticMeshPipeline::prepareTexturesForFrame(const std::vector<MeshRenderData>& meshDrawList) const
    {
        bool hasMaterials = false;
        for (const auto& meshData : meshDrawList)
        {
            if (!meshData.defaultMaterialPath.empty() || !meshData.submeshMaterials.empty())
            {
                hasMaterials = true;
                break;
            }
        }

        if (!hasMaterials)
        {
            return;
        }

        materialCacheManager->checkAndClearInvalidation();

        auto loadTexturesFromMaterial = [this](const std::shared_ptr<material::MaterialData>& matData)
        {
            if (!matData) return;

            ExtractedPBRValues pbr = MaterialPBRExtractor::extractPBRFromMaterial(*matData);

            if (!pbr.albedoTexturePath.empty())
                textureCache->loadTexture(pbr.albedoTexturePath);
            if (!pbr.normalTexturePath.empty())
                textureCache->loadTexture(pbr.normalTexturePath);
            if (!pbr.ormTexturePath.empty())
                textureCache->loadTexture(pbr.ormTexturePath);
            if (!pbr.metallicTexturePath.empty())
                textureCache->loadTexture(pbr.metallicTexturePath);
            if (!pbr.roughnessTexturePath.empty())
                textureCache->loadTexture(pbr.roughnessTexturePath);
            if (!pbr.aoTexturePath.empty())
                textureCache->loadTexture(pbr.aoTexturePath);
            if (!pbr.emissionTexturePath.empty())
                textureCache->loadTexture(pbr.emissionTexturePath);
            if (!pbr.heightTexturePath.empty())
                textureCache->loadTexture(pbr.heightTexturePath);
        };

        for (const auto& meshData : meshDrawList)
        {
            if (!meshData.defaultMaterialPath.empty())
            {
                auto matData = materialCacheManager->getMaterial(meshData.defaultMaterialPath);
                loadTexturesFromMaterial(matData);
            }

            for (const auto& [submeshName, matInfo] : meshData.submeshMaterials)
            {
                if (!matInfo.materialPath.empty())
                {
                    auto matData = materialCacheManager->getMaterial(matInfo.materialPath);
                    loadTexturesFromMaterial(matData);
                }
            }
        }
    }

    void StaticMeshPipeline::beginRenderPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    }

    void StaticMeshPipeline::endRenderPass(const vk::CommandBuffer& commandBuffer) const
    {
        commandBuffer.endRenderPass();
    }
}
