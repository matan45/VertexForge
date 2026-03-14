#include "DecalPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
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

        createSampler();
        createCubeGeometry();
        createBuffers();
        createRenderPass();
        createFramebuffers();
        createDescriptorResources();
        createPipeline();

        if (!pipeline)
        {
            vfLogError("DecalPipeline: init failed - pipeline is null");
            return;
        }

        initialized = true;
        vfLogInfo("DecalPipeline: initialized successfully");
    }

    void DecalPipeline::cleanup()
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (pipeline) { vkDevice.destroyPipeline(pipeline); pipeline = nullptr; }
        if (pipelineLayout) { vkDevice.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }
        if (descriptorPool) { vkDevice.destroyDescriptorPool(descriptorPool); descriptorPool = nullptr; }
        if (descriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(descriptorSetLayout); descriptorSetLayout = nullptr; }

        for (auto& fb : decalFramebuffers) { vkDevice.destroyFramebuffer(fb); }
        decalFramebuffers.clear();

        if (decalRenderPass) { vkDevice.destroyRenderPass(decalRenderPass); decalRenderPass = nullptr; }
        if (depthSampler) { vkDevice.destroySampler(depthSampler); depthSampler = nullptr; }

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
        updateDescriptorSet();
    }

    void DecalPipeline::createRenderPass()
    {
        // Color attachment: load existing scene color, store with blended decals
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

        // External dependency to ensure color attachment is available
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

    void DecalPipeline::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

        depthSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void DecalPipeline::createDescriptorResources()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Bindings: 0=cameraUBO, 1=depthTexture, 2=decalDataSSBO
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

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Pool
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eUniformBuffer, 1};
        poolSizes[1] = {vk::DescriptorType::eCombinedImageSampler, 1};
        poolSizes[2] = {vk::DescriptorType::eStorageBuffer, 1};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);

        // Allocate set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        updateDescriptorSet();
    }

    void DecalPipeline::updateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Camera UBO
        vk::DescriptorBufferInfo cameraBufferInfo{};
        cameraBufferInfo.buffer = cameraUBOBuffer;
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraUBO);

        // Depth texture
        vk::DescriptorImageInfo depthImageInfo{};
        depthImageInfo.sampler = depthSampler;
        depthImageInfo.imageView = offscreenResources.depthImage.depthImageView;
        depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        // Decal data SSBO
        vk::DescriptorBufferInfo decalBufferInfo{};
        decalBufferInfo.buffer = decalDataBuffer;
        decalBufferInfo.offset = 0;
        decalBufferInfo.range = sizeof(DecalGPUData) * maxDecals;

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraBufferInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &depthImageInfo;

        writes[2].dstSet = descriptorSet;
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

        // Push constants: decal world matrix + decal index
        vk::PushConstantRange pushConstant{};
        pushConstant.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(DecalPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        // Vertex input for cube geometry: position only (vec3)
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
        // No culling — decal must be visible from any camera angle
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // No depth test/write since we sample depth in the fragment shader
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        // Alpha blending
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
        // Unit cube vertices [-1, 1] range
        std::vector<glm::vec3> vertices = {
            {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
            {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
        };

        std::vector<uint32_t> indices = {
            // Front
            0, 1, 2, 2, 3, 0,
            // Back
            5, 4, 7, 7, 6, 5,
            // Left
            4, 0, 3, 3, 7, 4,
            // Right
            1, 5, 6, 6, 2, 1,
            // Top
            3, 2, 6, 6, 7, 3,
            // Bottom
            4, 5, 1, 1, 0, 4
        };

        cubeIndexCount = static_cast<uint32_t>(indices.size());

        vk::Device vkDevice = device.getLogicalDevice();
        vk::PhysicalDevice physDevice = device.getPhysicalDevice();

        // Vertex buffer
        vk::DeviceSize vertSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertReq(vkDevice, physDevice, vertSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::BufferUtilities::createBuffer(vertReq, cubeVertexBuffer, cubeVertexMemory);
        core::BufferUtilities::copyToBuffer(vkDevice, physDevice,
            device.getGraphicsQueue(), device.getStagingCommandPool(),
            cubeVertexBuffer, vertices.data(), vertSize);

        // Index buffer
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

        // Decal data SSBO (host visible for easy updates)
        vk::DeviceSize decalSize = sizeof(DecalGPUData) * maxDecals;
        core::BufferInfoRequest decalReq(vkDevice, physDevice, decalSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(decalReq, decalDataBuffer, decalDataMemory);

        // Camera UBO (host visible for easy updates)
        vk::DeviceSize cameraSize = sizeof(CameraUBO);
        core::BufferInfoRequest cameraReq(vkDevice, physDevice, cameraSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(cameraReq, cameraUBOBuffer, cameraUBOMemory);
    }

    void DecalPipeline::updateDecals(const std::vector<services::DecalRenderData>& decals)
    {
        if (!decals.empty())
        {
            static bool loggedOnce = false;
            if (!loggedOnce)
            {
                vfLogInfo("DecalPipeline: received {} decals", decals.size());
                loggedOnce = true;
            }
        }
        currentDecals = decals;

        // Sort by priority (lower = rendered first, higher = on top)
        std::stable_sort(currentDecals.begin(), currentDecals.end(),
            [](const services::DecalRenderData& a, const services::DecalRenderData& b)
            {
                return a.sortPriority < b.sortPriority;
            });

        // Build GPU data
        gpuDecalData.resize(currentDecals.size());
        for (size_t i = 0; i < currentDecals.size(); ++i)
        {
            const auto& decal = currentDecals[i];
            auto& gpu = gpuDecalData[i];

            gpu.inverseDecalMatrix = decal.inverseWorldMatrix;
            gpu.color = decal.color;
            gpu.fadeParams = glm::vec4(decal.angleFadeStart, decal.angleFadeEnd,
                                       decal.edgeFalloff, decal.normalStrength);
            gpu.halfExtents = glm::vec4(decal.halfExtents, decal.modifyNormals ? 1.0f : 0.0f);
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

        static bool loggedRenderOnce = false;
        if (!loggedRenderOnce)
        {
            vfLogInfo("DecalPipeline: rendering {} decals, imageIndex={}", currentDecals.size(), imageIndex);
            loggedRenderOnce = true;
        }

        uploadDecalData();
        uploadCameraUBO();

        // Transition depth to read-only for shader sampling
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
                               pipelineLayout, 0, descriptorSet, {});

        vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, cubeVertexBuffer, offset);
        cmd.bindIndexBuffer(cubeIndexBuffer, 0, vk::IndexType::eUint32);

        uint32_t count = std::min(static_cast<uint32_t>(currentDecals.size()), maxDecals);
        for (uint32_t i = 0; i < count; ++i)
        {
            DecalPushConstants pc{};
            pc.decalWorldMatrix = currentDecals[i].worldMatrix;
            pc.decalIndex = i;

            cmd.pushConstants(pipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(DecalPushConstants), &pc);

            cmd.drawIndexed(cubeIndexCount, 1, 0, 0, 0);
        }

        cmd.endRenderPass();

        // Transition depth back to attachment for subsequent passes
        transitionDepthToAttachment(cmd);
    }
}
