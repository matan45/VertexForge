#include "DecalPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "resource/Types.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>

namespace render::decal
{
    DecalPipeline::DecalPipeline(core::Device& device, core::SwapChain& swapChain,
                                 core::OffscreenResources& offscreenResources)
        : device(device), swapChain(swapChain), offscreenResources(offscreenResources)
    {
    }

    DecalPipeline::~DecalPipeline()
    {
        cleanup();
    }

    void DecalPipeline::init()
    {
        if (initialized) return;

        createSamplers();
        createCubeGeometry();
        createBuffers();
        createRenderPass();
        createFramebuffers();
        createFallbackTexture();
        createDescriptorResources();
        createPipeline();

        if (!pipeline)
        {
            vfLogError("DecalPipeline: init failed - pipeline is null");
            return;
        }

        initialized = true;
    }

    void DecalPipeline::cleanup()
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (pipeline) { vkDevice.destroyPipeline(pipeline); pipeline = nullptr; }
        if (pipelineLayout) { vkDevice.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }

        if (globalDescriptorPool) { vkDevice.destroyDescriptorPool(globalDescriptorPool); globalDescriptorPool = nullptr; }
        if (globalDescriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(globalDescriptorSetLayout); globalDescriptorSetLayout = nullptr; }
        if (textureDescriptorPool) { vkDevice.destroyDescriptorPool(textureDescriptorPool); textureDescriptorPool = nullptr; }
        if (textureDescriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(textureDescriptorSetLayout); textureDescriptorSetLayout = nullptr; }

        for (auto& fb : decalFramebuffers) { vkDevice.destroyFramebuffer(fb); }
        decalFramebuffers.clear();

        if (decalRenderPass) { vkDevice.destroyRenderPass(decalRenderPass); decalRenderPass = nullptr; }
        if (depthSampler) { vkDevice.destroySampler(depthSampler); depthSampler = nullptr; }
        if (textureSampler) { vkDevice.destroySampler(textureSampler); textureSampler = nullptr; }

        textureCache.clear();
        fallbackTexture.reset();

        core::BufferUtilities::destroyBuffer(vkDevice, decalDataBuffer, decalDataMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, cameraUBOBuffer, cameraUBOMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, cubeVertexBuffer, cubeVertexMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, cubeIndexBuffer, cubeIndexMemory);

        initialized = false;
    }

    void DecalPipeline::recreate()
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();

        for (auto& fb : decalFramebuffers) { vkDevice.destroyFramebuffer(fb); }
        decalFramebuffers.clear();

        if (pipeline) { vkDevice.destroyPipeline(pipeline); pipeline = nullptr; }
        if (pipelineLayout) { vkDevice.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }

        createFramebuffers();
        createPipeline();
        updateGlobalDescriptorSet();
    }

    void DecalPipeline::createRenderPass()
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

        vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        decalRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void DecalPipeline::createFramebuffers()
    {
        auto extent = swapChain.getSwapchainExtent();
        uint32_t imageCount = swapChain.getImageCount();
        decalFramebuffers.resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            vk::ImageView attachments[] = { offscreenResources.colorImages[i].colorImageView };

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = decalRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = attachments;
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;

            decalFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void DecalPipeline::createSamplers()
    {
        // Depth sampler (nearest, clamp)
        vk::SamplerCreateInfo depthSamplerInfo{};
        depthSamplerInfo.magFilter = vk::Filter::eNearest;
        depthSamplerInfo.minFilter = vk::Filter::eNearest;
        depthSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        depthSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        depthSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        depthSampler = device.getLogicalDevice().createSampler(depthSamplerInfo);

        // Texture sampler (linear, clamp for decals)
        vk::SamplerCreateInfo texSamplerInfo{};
        texSamplerInfo.magFilter = vk::Filter::eLinear;
        texSamplerInfo.minFilter = vk::Filter::eLinear;
        texSamplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        texSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        texSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        texSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        texSamplerInfo.maxLod = 12.0f;
        texSamplerInfo.anisotropyEnable = VK_TRUE;
        texSamplerInfo.maxAnisotropy = 4.0f;
        textureSampler = device.getLogicalDevice().createSampler(texSamplerInfo);
    }

    void DecalPipeline::createFallbackTexture()
    {
        // Create a 1x1 white texture as fallback for decals without albedo texture
        fallbackTexture = std::make_unique<core::Texture>(device);

        resource::TextureData texData;
        texData.width = 1;
        texData.height = 1;
        texData.numbersOfChannels = 4;
        texData.mipLevels = 1;

        resource::MipLevelData mip0;
        mip0.width = 1;
        mip0.height = 1;
        mip0.data = {255, 255, 255, 255};
        texData.mipData.push_back(std::move(mip0));

        fallbackTexture->loadTextureFromData(texData, vk::Format::eR8G8B8A8Srgb, false);
    }

    void DecalPipeline::createDescriptorResources()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Set 0: camera UBO, depth texture, decal data SSBO
        {
            std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[2].binding = 2;
            bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            globalDescriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

            std::array<vk::DescriptorPoolSize, 3> poolSizes{};
            poolSizes[0] = {vk::DescriptorType::eUniformBuffer, 1};
            poolSizes[1] = {vk::DescriptorType::eCombinedImageSampler, 1};
            poolSizes[2] = {vk::DescriptorType::eStorageBuffer, 1};

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            globalDescriptorPool = vkDevice.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = globalDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &globalDescriptorSetLayout;
            globalDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

            updateGlobalDescriptorSet();
        }

        // Set 1: per-decal albedo texture (one sampler binding)
        {
            vk::DescriptorSetLayoutBinding texBinding{};
            texBinding.binding = 0;
            texBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            texBinding.descriptorCount = 1;
            texBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &texBinding;
            textureDescriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

            vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, MAX_CACHED_TEXTURES + 1};
            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = MAX_CACHED_TEXTURES + 1; // +1 for fallback
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            textureDescriptorPool = vkDevice.createDescriptorPool(poolInfo);

            // Allocate fallback descriptor set
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = textureDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout;
            fallbackDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

            // Write fallback texture
            vk::DescriptorImageInfo imgInfo{};
            imgInfo.sampler = textureSampler;
            imgInfo.imageView = fallbackTexture->getImageView();
            imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::WriteDescriptorSet write{};
            write.dstSet = fallbackDescriptorSet;
            write.dstBinding = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &imgInfo;
            vkDevice.updateDescriptorSets(write, {});
        }
    }

    void DecalPipeline::updateGlobalDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo cameraBufferInfo{};
        cameraBufferInfo.buffer = cameraUBOBuffer;
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraUBO);

        vk::DescriptorImageInfo depthImageInfo{};
        depthImageInfo.sampler = depthSampler;
        depthImageInfo.imageView = offscreenResources.depthImage.depthImageView;
        depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        vk::DescriptorBufferInfo decalBufferInfo{};
        decalBufferInfo.buffer = decalDataBuffer;
        decalBufferInfo.offset = 0;
        decalBufferInfo.range = sizeof(DecalGPUData) * maxDecals;

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0].dstSet = globalDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraBufferInfo;

        writes[1].dstSet = globalDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &depthImageInfo;

        writes[2].dstSet = globalDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &decalBufferInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void DecalPipeline::createPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::Shader shader(device);
        shader.readShader("../../resources/shaders/decal/decal.glsl");
        const auto& stages = shader.getShaderStages();
        if (stages.size() < 2)
        {
            vfLogError("DecalPipeline: Failed to load decal shader");
            return;
        }

        vk::PushConstantRange pushConstant{};
        pushConstant.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(DecalPushConstants);

        std::array<vk::DescriptorSetLayout, 2> layouts = {globalDescriptorSetLayout, textureDescriptorSetLayout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        vk::VertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(glm::vec3);
        vertexBinding.inputRate = vk::VertexInputRate::eVertex;

        vk::VertexInputAttributeDescription vertexAttrib{};
        vertexAttrib.binding = 0;
        vertexAttrib.location = 0;
        vertexAttrib.format = vk::Format::eR32G32B32Sfloat;
        vertexAttrib.offset = 0;

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &vertexAttrib;

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        auto extent = swapChain.getSwapchainExtent();
        vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(extent.width),
                              static_cast<float>(extent.height), 0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, extent};
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        blendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        blendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                          vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB |
                                          vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = decalRenderPass;
        pipelineInfo.subpass = 0;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("DecalPipeline: Failed to create pipeline");
            return;
        }
        pipeline = result.value;

        shader.cleanUp();
    }

    void DecalPipeline::createCubeGeometry()
    {
        std::vector<glm::vec3> vertices = {
            {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
            {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
        };

        std::vector<uint32_t> indices = {
            0, 1, 2, 2, 3, 0,
            5, 4, 7, 7, 6, 5,
            4, 0, 3, 3, 7, 4,
            1, 5, 6, 6, 2, 1,
            3, 2, 6, 6, 7, 3,
            4, 5, 1, 1, 0, 4
        };

        cubeIndexCount = static_cast<uint32_t>(indices.size());

        vk::Device vkDevice = device.getLogicalDevice();
        vk::PhysicalDevice physDevice = device.getPhysicalDevice();

        vk::DeviceSize vertSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertReq(vkDevice, physDevice, vertSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::BufferUtilities::createBuffer(vertReq, cubeVertexBuffer, cubeVertexMemory);
        core::BufferUtilities::copyToBuffer(vkDevice, physDevice,
            device.getGraphicsQueue(), device.getStagingCommandPool(),
            cubeVertexBuffer, vertices.data(), vertSize);

        vk::DeviceSize idxSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest idxReq(vkDevice, physDevice, idxSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::BufferUtilities::createBuffer(idxReq, cubeIndexBuffer, cubeIndexMemory);
        core::BufferUtilities::copyToBuffer(vkDevice, physDevice,
            device.getGraphicsQueue(), device.getStagingCommandPool(),
            cubeIndexBuffer, indices.data(), idxSize);
    }

    void DecalPipeline::createBuffers()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vk::PhysicalDevice physDevice = device.getPhysicalDevice();

        vk::DeviceSize decalSize = sizeof(DecalGPUData) * maxDecals;
        core::BufferInfoRequest decalReq(vkDevice, physDevice, decalSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(decalReq, decalDataBuffer, decalDataMemory);

        vk::DeviceSize cameraSize = sizeof(CameraUBO);
        core::BufferInfoRequest cameraReq(vkDevice, physDevice, cameraSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(cameraReq, cameraUBOBuffer, cameraUBOMemory);
    }

    vk::DescriptorSet DecalPipeline::getOrLoadTexture(const std::string& path)
    {
        if (path.empty()) return fallbackDescriptorSet;

        auto it = textureCache.find(path);
        if (it != textureCache.end()) return it->second.descriptorSet;

        // Load new texture
        try
        {
            auto tex = std::make_unique<core::Texture>(device);
            tex->loadTextureFromFile(path, vk::Format::eR8G8B8A8Srgb, false);

            // Allocate descriptor set
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = textureDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout;
            vk::DescriptorSet descSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

            // Write descriptor
            vk::DescriptorImageInfo imgInfo{};
            imgInfo.sampler = textureSampler;
            imgInfo.imageView = tex->getImageView();
            imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::WriteDescriptorSet write{};
            write.dstSet = descSet;
            write.dstBinding = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &imgInfo;
            device.getLogicalDevice().updateDescriptorSets(write, {});

            textureCache[path] = {std::move(tex), descSet};
            return descSet;
        }
        catch (const std::exception& e)
        {
            vfLogError("DecalPipeline: Failed to load texture '{}': {}", path, e.what());
            return fallbackDescriptorSet;
        }
    }

    void DecalPipeline::updateDecals(const std::vector<services::DecalRenderData>& decals)
    {
        currentDecals = decals;

        std::stable_sort(currentDecals.begin(), currentDecals.end(),
            [](const services::DecalRenderData& a, const services::DecalRenderData& b)
            {
                return a.sortPriority < b.sortPriority;
            });

        gpuDecalData.resize(currentDecals.size());
        for (size_t i = 0; i < currentDecals.size(); ++i)
        {
            const auto& decal = currentDecals[i];
            auto& gpu = gpuDecalData[i];

            gpu.inverseDecalMatrix = decal.inverseWorldMatrix;
            gpu.color = decal.color;
            gpu.fadeParams = glm::vec4(decal.angleFadeStart, decal.angleFadeEnd,
                                       decal.edgeFalloff, decal.normalStrength);
            gpu.halfExtents = glm::vec4(decal.halfExtents,
                                         decal.albedoTexture.empty() ? 0.0f : 1.0f);
        }
    }

    void DecalPipeline::setCameraData(const glm::mat4& view, const glm::mat4& projection,
                                       float nearPlane, float farPlane)
    {
        currentViewProjection = projection * view;
        currentInverseViewProjection = glm::inverse(currentViewProjection);
        currentNearPlane = nearPlane;
        currentFarPlane = farPlane;
    }

    void DecalPipeline::uploadDecalData()
    {
        if (gpuDecalData.empty()) return;

        uint32_t count = std::min(static_cast<uint32_t>(gpuDecalData.size()), maxDecals);
        vk::Device vkDevice = device.getLogicalDevice();
        void* mapped = vkDevice.mapMemory(decalDataMemory, 0, sizeof(DecalGPUData) * count);
        memcpy(mapped, gpuDecalData.data(), sizeof(DecalGPUData) * count);
        vkDevice.unmapMemory(decalDataMemory);
    }

    void DecalPipeline::uploadCameraUBO()
    {
        auto extent = swapChain.getSwapchainExtent();

        CameraUBO ubo{};
        ubo.viewProjection = currentViewProjection;
        ubo.inverseViewProjection = currentInverseViewProjection;
        ubo.cameraParams = glm::vec4(currentNearPlane, currentFarPlane,
                                      static_cast<float>(extent.width),
                                      static_cast<float>(extent.height));

        vk::Device vkDevice = device.getLogicalDevice();
        void* mapped = vkDevice.mapMemory(cameraUBOMemory, 0, sizeof(CameraUBO));
        memcpy(mapped, &ubo, sizeof(CameraUBO));
        vkDevice.unmapMemory(cameraUBOMemory);
    }

    void DecalPipeline::transitionDepthToReadOnly(const vk::CommandBuffer& cmd)
    {
        vk::ImageMemoryBarrier depthBarrier{};
        depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthBarrier.newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = offscreenResources.depthImage.depthImage;
        depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth |
                                                    vk::ImageAspectFlagBits::eStencil;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        depthBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, depthBarrier);
    }

    void DecalPipeline::transitionDepthToAttachment(const vk::CommandBuffer& cmd)
    {
        vk::ImageMemoryBarrier depthBarrier{};
        depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthBarrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = offscreenResources.depthImage.depthImage;
        depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth |
                                                    vk::ImageAspectFlagBits::eStencil;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        depthBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                     vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {}, {}, {}, depthBarrier);
    }

    void DecalPipeline::render(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        if (!initialized || currentDecals.empty() || !pipeline) return;

        uploadDecalData();
        uploadCameraUBO();

        transitionDepthToReadOnly(cmd);

        auto extent = swapChain.getSwapchainExtent();

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = decalRenderPass;
        rpBegin.framebuffer = decalFramebuffers[imageIndex];
        rpBegin.renderArea.extent = extent;

        cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(extent.width),
                              static_cast<float>(extent.height), 0.0f, 1.0f};
        cmd.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        cmd.setScissor(0, scissor);

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               pipelineLayout, 0, globalDescriptorSet, {});

        vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, cubeVertexBuffer, offset);
        cmd.bindIndexBuffer(cubeIndexBuffer, 0, vk::IndexType::eUint32);

        uint32_t count = std::min(static_cast<uint32_t>(currentDecals.size()), maxDecals);
        for (uint32_t i = 0; i < count; ++i)
        {
            // Bind per-decal albedo texture (set 1)
            vk::DescriptorSet texDescSet = getOrLoadTexture(currentDecals[i].albedoTexture);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   pipelineLayout, 1, texDescSet, {});

            DecalPushConstants pc{};
            pc.decalWorldMatrix = currentDecals[i].worldMatrix;
            pc.decalIndex = i;

            cmd.pushConstants(pipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(DecalPushConstants), &pc);

            cmd.drawIndexed(cubeIndexCount, 1, 0, 0, 0);
        }

        cmd.endRenderPass();

        transitionDepthToAttachment(cmd);
    }
}
