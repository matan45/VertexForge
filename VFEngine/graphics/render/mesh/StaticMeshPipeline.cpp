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
        // Create mesh GPU cache for mesh buffer management
        meshCache = std::make_unique<MeshGPUCache>(device);

        // Create material texture cache for texture GPU resources
        textureCache = std::make_unique<MaterialTextureCache>(device);
        textureCache->init(device.getStagingCommandPool());

        // Create material shader cache for per-material pipeline compilation
        materialShaderCache = std::make_unique<MaterialShaderCache>(device);

        // Create material cache manager and link related caches
        materialCacheManager = std::make_unique<MaterialCacheManager>();
        materialCacheManager->setShaderCache(materialShaderCache.get());
        materialCacheManager->setTextureCache(textureCache.get());

        // Register callback to invalidate material cache when materials change
        material::MaterialManager::instance().registerChangeCallback(
            [this](const std::string& materialPath) {
                if (materialCacheManager) {
                    materialCacheManager->invalidate(materialPath);
                }
            });
    }

    StaticMeshPipeline::~StaticMeshPipeline() = default;

    void StaticMeshPipeline::init(const ibl::ImageData& irradianceMap,
                                  const ibl::ImageData& prefilterMap,
                                  const ibl::ImageData& brdfLUT)
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout(); // Set 1 layout
        createDescriptorPool();
        createTextureDescriptorPool(); // Set 1 pool
        createCameraUBO();
        createDescriptorSet(irradianceMap, prefilterMap, brdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(renderPass, pipelineLayout, swapChain.getSwapchainExtent());
        initializeDefaultTextureDescriptors(); // Initialize set 1 with defaults
        createFramebuffers();
    }

    void StaticMeshPipeline::initWithDefaults()
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout(); // Set 1 layout
        createDescriptorPool();
        createTextureDescriptorPool(); // Set 1 pool
        createCameraUBO();

        // Create default IBL textures using the factory
        defaultIBLFactory = std::make_unique<ibl::DefaultIBLTextureFactory>(device);
        defaultIBLFactory->createDefaultTextures(device.getStagingCommandPool());

        createDescriptorSet(defaultIBLFactory->getIrradiance(),
                            defaultIBLFactory->getPrefilter(),
                            defaultIBLFactory->getBrdfLUT());
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(renderPass, pipelineLayout, swapChain.getSwapchainExtent());
        initializeDefaultTextureDescriptors(); // Initialize set 1 with defaults
        createFramebuffers();
        usingDefaultTextures = true;
    }

    void StaticMeshPipeline::loadShaders()
    {
        meshShader = std::make_shared<core::Shader>(device);
        meshShader->readShader("../../resources/shaders/mesh/mesh.glsl");
    }

    void StaticMeshPipeline::recreate()
    {
        // Cleanup framebuffers and render pass for recreation
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
        // Color attachment - load existing content (preserve skybox)
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad; // Preserve skybox
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        // Depth attachment
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

        // Binding 0: Camera UBO (vertex + fragment + task + mesh for mesh shader pipeline)
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Irradiance cubemap (fragment only)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].pImmutableSamplers = nullptr;

        // Binding 2: Prefilter cubemap (fragment only)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[2].pImmutableSamplers = nullptr;

        // Binding 3: BRDF LUT (fragment only)
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

        // Camera UBO binding
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

        // Irradiance map binding
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

        // Prefilter map binding
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

        // BRDF LUT binding
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
        // Set 1, Binding 0: Array of 16 material textures per material
        // See material::TextureSlot for slot assignments (albedo, normal, ORM, metallic, roughness, ao, emission, etc.)
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
        // Legacy pool for backward compatibility - per-material pool is managed by MaterialTextureCache
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

        // Two descriptor set layouts: set 0 (camera + IBL), set 1 (material textures)
        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            descriptorSetLayout, // Set 0: Camera + IBL
            textureDescriptorSetLayout // Set 1: Material textures
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

        // Input assembly
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport and scissor
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

        // Rasterizer - back-face culling enabled for meshes
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Depth testing - enabled for meshes
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending - no blending
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

        // Create opaque pipeline
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
        // Store for AABB wireframe rendering and animation
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
        // Clean up pipeline/descriptor resources but preserve loaded meshes and command pool
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

        // Reset MaterialTextureCache descriptor resources before destroying layout
        // (they share the same layout, so must be freed first)
        if (textureCache)
        {
            textureCache->resetDescriptorResources();
        }

        // Clean up texture descriptor set (set 1)
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

        // Clean up default textures if we created them
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

                // Frustum culling
                if (frustum && frustum->isInitialized() &&
                    !frustum->intersectsAABB(subMesh.boundingBox, meshData.modelMatrix))
                    continue;

                // Get material path for sorting
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

        // Sort by material path to group same-material submeshes together
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

        // Get or create per-material descriptor set if material has textures
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

        // Bind material descriptor set if different from current
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

        // Setup push constants
        MeshPushConstants pushConstants{};
        pushConstants.model = meshData.modelMatrix;
        pushConstants.metallic = pbrValues.metallic;
        pushConstants.roughness = pbrValues.roughness;
        pushConstants.ao = pbrValues.ao;
        pushConstants.blendMode = static_cast<float>(pbrValues.blendMode);

        // Pack texture indices - check both pbrValues paths AND explicit meshData.textureIndices
        // meshData.textureIndices is used for preview rendering where textures are set explicitly
        auto getTexIdx = [&](material::TextureSlot slot, const std::string& pbrPath) -> uint8_t {
            if (!pbrPath.empty()) {
                return static_cast<uint8_t>(material::toIndex(slot));
            }
            // Also check explicit texture index from meshData (for preview)
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

        // Highlight selected submesh
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

        // Get LOD and draw
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

        // Begin render pass
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

        // Bind descriptor sets
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        if (textureDescriptorsInitialized && textureDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, textureDescriptorSet, nullptr);
        }

        // Collect and sort submeshes
        std::vector<SortedSubmesh> opaqueSubmeshes;
        std::vector<SortedSubmesh> maskedSubmeshes;
        collectSortedSubmeshes(meshDrawList, frustum, materialCache, opaqueSubmeshes, maskedSubmeshes);

        // Initialize render state
        RenderState state;
        state.currentMaterialDescriptorSet = textureDescriptorSet;

        // Render opaque pass
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        state.currentPipeline = graphicsPipeline;

        for (const auto& item : opaqueSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Opaque, materialCache, state);
        }

        // Render masked pass
        for (const auto& item : maskedSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Masked, materialCache, state);
        }

        // Render debug items
        if (debugRenderer && debugRenderer->hasItemsToRender())
        {
            debugRenderer->render(commandBuffer, meshDrawList, debugView, debugProjection,
                                  [this](const std::string& meshId) { return getMesh(meshId); });
        }

        commandBuffer.endRenderPass();
    }

    void StaticMeshPipeline::prepareTexturesForFrame(const std::vector<MeshRenderData>& meshDrawList) const
    {
        // Check if any meshes have materials. If not (e.g., material preview),
        // skip texture preparation to preserve externally-bound textures
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
            // No materials in draw list - don't reset texture bindings
            // This allows external texture management (e.g., MaterialPreviewController)
            return;
        }

        // Check if cache needs to be invalidated (material was saved externally)
        materialCacheManager->checkAndClearInvalidation();

        auto loadTexturesFromMaterial = [this](const std::shared_ptr<material::MaterialData>& matData)
        {
            if (!matData) return;

            ExtractedPBRValues pbr = MaterialPBRExtractor::extractPBRFromMaterial(*matData);

            // Load all texture types that may be used
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

        // Load all unique materials and textures into cache
        // Per-material descriptor sets are created on-demand in recordCommandBuffer
        for (const auto& meshData : meshDrawList)
        {
            if (!meshData.defaultMaterialPath.empty())
            {
                auto matData = materialCacheManager->getMaterial(meshData.defaultMaterialPath);
                loadTexturesFromMaterial(matData);
            }

            // Also process per-submesh materials
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

        // Clear values for depth only - color uses loadOp::eLoad to preserve skybox
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
