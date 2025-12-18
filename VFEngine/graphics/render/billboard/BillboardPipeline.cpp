#include "BillboardPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"
#include <cstring>
#include <filesystem>

namespace render::billboard
{
    BillboardPipeline::BillboardPipeline(core::Device& device, core::SwapChain& swapChain,
                                         core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
    }

    BillboardPipeline::~BillboardPipeline() = default;

    void BillboardPipeline::init()
    {
        loadShader();
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();
        createCameraUBO();
        createDefaultAtlas();
        createDescriptorSet();
        createPipelineLayout();
        createGraphicsPipeline();
        createFramebuffers();
        createQuadBuffers();
        createInstanceBuffer();

        initialized = true;
    }

    void BillboardPipeline::loadShader()
    {
        billboardShader = std::make_shared<core::Shader>(device);
        billboardShader->readShader("../../resources/shaders/billboard/billboard.glsl");
    }

    void BillboardPipeline::recreate()
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

    void BillboardPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        // Framebuffers
        for (auto& framebuffer : framebuffers)
        {
            dev.destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        // Pipeline resources
        if (graphicsPipeline) dev.destroyPipeline(graphicsPipeline);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);

        // Descriptor resources
        if (descriptorPool)
        {
            if (descriptorSet)
                dev.freeDescriptorSets(descriptorPool, descriptorSet);
            dev.destroyDescriptorPool(descriptorPool);
        }
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);

        // Render pass
        if (renderPass) dev.destroyRenderPass(renderPass);

        // Buffers
        if (cameraUBO)
        {
            dev.destroyBuffer(cameraUBO);
            dev.freeMemory(cameraUBOMemory);
        }
        if (quadVertexBuffer)
        {
            dev.destroyBuffer(quadVertexBuffer);
            dev.freeMemory(quadVertexBufferMemory);
        }
        if (quadIndexBuffer)
        {
            dev.destroyBuffer(quadIndexBuffer);
            dev.freeMemory(quadIndexBufferMemory);
        }
        if (instanceBuffer)
        {
            dev.destroyBuffer(instanceBuffer);
            dev.freeMemory(instanceBufferMemory);
        }

        // Atlas texture (loaded from file)
        atlasTexture.reset();

        // Default atlas (procedural fallback)
        if (defaultAtlasSampler) dev.destroySampler(defaultAtlasSampler);
        if (defaultAtlasImageView) dev.destroyImageView(defaultAtlasImageView);
        if (defaultAtlasImage)
        {
            dev.destroyImage(defaultAtlasImage);
            dev.freeMemory(defaultAtlasImageMemory);
        }

        // Shader
        if (billboardShader)
        {
            billboardShader->cleanUp();
            billboardShader.reset();
        }

        initialized = false;
        atlasLoaded = false;
    }

    void BillboardPipeline::createRenderPass()
    {
        // Color attachment - load existing content (preserve mesh rendering)
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;  // Preserve previous rendering
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        // Depth attachment - load existing (read-only depth test)
        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;  // Load existing depth
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

    void BillboardPipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

        // Binding 0: Camera UBO (vertex shader)
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Atlas texture (fragment shader)
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

    void BillboardPipeline::createDescriptorPool()
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

    void BillboardPipeline::createCameraUBO()
    {
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(BillboardCameraUBO);
        core::Utilities::createBuffer(bufferRequest, cameraUBO, cameraUBOMemory);
    }

    void BillboardPipeline::createDefaultAtlas()
    {
        // Create a simple 4x4 grid atlas (256x256) with placeholder colors
        constexpr uint32_t atlasSize = AtlasConfig::ATLAS_SIZE;
        constexpr uint32_t tileSize = AtlasConfig::TILE_SIZE;
        constexpr uint32_t gridSize = AtlasConfig::GRID_SIZE;

        // Create image
        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            atlasSize, atlasSize, 1, 1,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::Utilities::createImage(imageInfo, defaultAtlasImage, defaultAtlasImageMemory);

        // Create image view
        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            defaultAtlasImage,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::Utilities::createImageView(viewInfo, defaultAtlasImageView);

        // Create sampler
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        defaultAtlasSampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Generate placeholder atlas texture data
        std::vector<uint8_t> atlasData(atlasSize * atlasSize * 4);

        // Define colors for each icon type (RGBA)
        // Index 0: Light (yellow), 1: Camera (blue), 2: AudioSource (green), 3: Particle (magenta)
        const uint8_t iconColors[16][4] = {
            {255, 220, 50, 255},   // 0: Light - yellow
            {50, 150, 255, 255},   // 1: Camera - blue
            {50, 255, 100, 255},   // 2: AudioSource - green
            {255, 100, 255, 255},  // 3: Particle - magenta
            {200, 200, 200, 255},  // 4: Custom placeholder
            {180, 180, 180, 255},  // 5
            {160, 160, 160, 255},  // 6
            {140, 140, 140, 255},  // 7
            {120, 120, 120, 255},  // 8
            {100, 100, 100, 255},  // 9
            {80, 80, 80, 255},     // 10
            {60, 60, 60, 255},     // 11
            {40, 40, 40, 255},     // 12
            {20, 20, 20, 255},     // 13
            {10, 10, 10, 255},     // 14
            {5, 5, 5, 255},        // 15
        };

        // Fill each tile with a solid color and a circular shape
        for (uint32_t ty = 0; ty < gridSize; ++ty)
        {
            for (uint32_t tx = 0; tx < gridSize; ++tx)
            {
                uint32_t tileIndex = ty * gridSize + tx;
                const uint8_t* color = iconColors[tileIndex];

                for (uint32_t py = 0; py < tileSize; ++py)
                {
                    for (uint32_t px = 0; px < tileSize; ++px)
                    {
                        uint32_t x = tx * tileSize + px;
                        uint32_t y = ty * tileSize + py;
                        uint32_t idx = (y * atlasSize + x) * 4;

                        // Create a circular icon shape
                        float cx = static_cast<float>(px) - tileSize / 2.0f + 0.5f;
                        float cy = static_cast<float>(py) - tileSize / 2.0f + 0.5f;
                        float dist = std::sqrt(cx * cx + cy * cy);
                        float radius = tileSize / 2.0f - 4.0f;

                        if (dist < radius)
                        {
                            atlasData[idx + 0] = color[0];
                            atlasData[idx + 1] = color[1];
                            atlasData[idx + 2] = color[2];
                            atlasData[idx + 3] = color[3];
                        }
                        else
                        {
                            // Transparent outside the circle
                            atlasData[idx + 0] = 0;
                            atlasData[idx + 1] = 0;
                            atlasData[idx + 2] = 0;
                            atlasData[idx + 3] = 0;
                        }
                    }
                }
            }
        }

        // Create staging buffer and upload
        vk::DeviceSize imageSize = atlasData.size();

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        // RAII cleanup for staging resources
        auto cleanupStaging = [&]() {
            if (stagingBuffer) device.getLogicalDevice().destroyBuffer(stagingBuffer);
            if (stagingMemory) device.getLogicalDevice().freeMemory(stagingMemory);
        };

        try
        {
            // Copy data to staging buffer
            void* data;
            vk::Result mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
            if (mapResult == vk::Result::eSuccess)
            {
                std::memcpy(data, atlasData.data(), imageSize);
                device.getLogicalDevice().unmapMemory(stagingMemory);
            }

            // Create command pool for transfer
            vk::CommandPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
            poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
            auto commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);

            // Transition and copy
            auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());

            // Transition to transfer dst
            core::Utilities::transitionImageLayout(cmd.get(), defaultAtlasImage,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);

            // Copy buffer to image
            vk::BufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0, 0, 0};
            region.imageExtent = vk::Extent3D{atlasSize, atlasSize, 1};

            cmd->copyBufferToImage(stagingBuffer, defaultAtlasImage, vk::ImageLayout::eTransferDstOptimal, region);

            // Transition to shader read
            core::Utilities::transitionImageLayout(cmd.get(), defaultAtlasImage,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd);

            // Cleanup staging
            cleanupStaging();
        }
        catch (...)
        {
            cleanupStaging();
            throw;
        }
    }

    void BillboardPipeline::createDescriptorSet()
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
        uboBufferInfo.range = sizeof(BillboardCameraUBO);

        vk::WriteDescriptorSet uboWrite{};
        uboWrite.dstSet = descriptorSet;
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &uboBufferInfo;

        // Atlas texture binding - use atlasTexture if loaded, otherwise default atlas
        vk::DescriptorImageInfo atlasImageInfo{};
        atlasImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        if (atlasTexture)
        {
            atlasImageInfo.imageView = atlasTexture->getImageView();
            atlasImageInfo.sampler = atlasTexture->getSampler();
        }
        else
        {
            atlasImageInfo.imageView = defaultAtlasImageView;
            atlasImageInfo.sampler = defaultAtlasSampler;
        }

        vk::WriteDescriptorSet atlasWrite{};
        atlasWrite.dstSet = descriptorSet;
        atlasWrite.dstBinding = 1;
        atlasWrite.dstArrayElement = 0;
        atlasWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        atlasWrite.descriptorCount = 1;
        atlasWrite.pImageInfo = &atlasImageInfo;

        std::array<vk::WriteDescriptorSet, 2> descriptorWrites = {uboWrite, atlasWrite};
        device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    void BillboardPipeline::createPipelineLayout()
    {
        // Push constant range for BillboardPushConstants
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(BillboardPushConstants);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    }

    void BillboardPipeline::createGraphicsPipeline()
    {
        // Vertex input - two bindings: quad vertices (binding 0) and instance data (binding 1)
        auto vertexBinding = BillboardVertex::getBindingDescription();
        auto instanceBinding = BillboardInstanceData::getBindingDescription();
        std::array<vk::VertexInputBindingDescription, 2> bindings = {vertexBinding, instanceBinding};

        auto vertexAttribs = BillboardVertex::getAttributeDescriptions();
        auto instanceAttribs = BillboardInstanceData::getAttributeDescriptions();

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
        vertexInputInfo.pVertexBindingDescriptions = bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttribs.size());
        vertexInputInfo.pVertexAttributeDescriptions = allAttribs.data();

        // Input assembly - triangle list for quad
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
        scissor.offset = vk::Offset2D{0, 0};
        scissor.extent = swapChain.getSwapchainExtent();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // Rasterizer - no culling for billboards (both sides visible)
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;  // No culling
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Depth testing - enabled but no write (read-only)
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth buffer
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Alpha blending
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // Create pipeline
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(billboardShader->getShaderStages().size());
        pipelineInfo.pStages = billboardShader->getShaderStages().data();
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

    void BillboardPipeline::createFramebuffers()
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

    void BillboardPipeline::createQuadBuffers()
    {
        // Create a simple quad with vertices at corners
        std::vector<BillboardVertex> vertices = {
            {{-0.5f, -0.5f}, {0.0f, 1.0f}},  // Bottom-left
            {{ 0.5f, -0.5f}, {1.0f, 1.0f}},  // Bottom-right
            {{ 0.5f,  0.5f}, {1.0f, 0.0f}},  // Top-right
            {{-0.5f,  0.5f}, {0.0f, 0.0f}},  // Top-left
        };

        std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

        // Create vertex buffer
        vk::DeviceSize vertexBufferSize = sizeof(BillboardVertex) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferMemory);

        // Create index buffer
        vk::DeviceSize indexBufferSize = sizeof(uint16_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferMemory);

        // Create command pool for transfer
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        auto commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);

        // Upload vertex data
        core::Utilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            commandPool.get(),
            quadVertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        // Upload index data
        core::Utilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            commandPool.get(),
            quadIndexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    void BillboardPipeline::createInstanceBuffer()
    {
        vk::DeviceSize bufferSize = sizeof(BillboardInstanceData) * maxInstances;
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.size = bufferSize;
        bufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        core::Utilities::createBuffer(bufferRequest, instanceBuffer, instanceBufferMemory);
    }

    bool BillboardPipeline::loadAtlas(const std::string& atlasPath)
    {
        if (!std::filesystem::exists(atlasPath))
        {
            loggerWarning("Billboard atlas file not found: {}, using default atlas", atlasPath);
            return atlasLoaded;
        }

        // Wait for GPU to finish using current resources
        device.getLogicalDevice().waitIdle();

        // Reset existing atlas texture if any
        atlasTexture.reset();

        // Create new texture and load from file
        atlasTexture = std::make_unique<core::Texture>(device);
        atlasTexture->loadTextureFromFile(atlasPath, vk::Format::eR8G8B8A8Unorm, false);

        // Update descriptor set with new atlas
        if (descriptorSet)
        {
            vk::DescriptorImageInfo atlasImageInfo{};
            atlasImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            atlasImageInfo.imageView = atlasTexture->getImageView();
            atlasImageInfo.sampler = atlasTexture->getSampler();

            vk::WriteDescriptorSet atlasWrite{};
            atlasWrite.dstSet = descriptorSet;
            atlasWrite.dstBinding = 1;
            atlasWrite.dstArrayElement = 0;
            atlasWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            atlasWrite.descriptorCount = 1;
            atlasWrite.pImageInfo = &atlasImageInfo;

            device.getLogicalDevice().updateDescriptorSets(atlasWrite, nullptr);
        }

        atlasLoaded = true;
        const auto& imgData = atlasTexture->getImageData();
        loggerInfo("Billboard atlas loaded successfully: {} ({}x{})", atlasPath, imgData.width, imgData.height);
        return true;
    }

    void BillboardPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                            const glm::vec3& cameraPos) const
    {
        BillboardCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(cameraUBOMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            std::memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(cameraUBOMemory);
        }
    }

    void BillboardPipeline::setBillboardList(const std::vector<BillboardRenderData>& billboards)
    {
        currentBillboards = billboards;
        updateInstanceBuffer();
    }

    void BillboardPipeline::updateInstanceBuffer()
    {
        if (currentBillboards.empty())
        {
            currentInstanceCount = 0;
            return;
        }

        currentInstanceCount = static_cast<uint32_t>(std::min(currentBillboards.size(),
                                                               static_cast<size_t>(maxInstances)));

        std::vector<BillboardInstanceData> instanceData(currentInstanceCount);
        for (uint32_t i = 0; i < currentInstanceCount; ++i)
        {
            const auto& src = currentBillboards[i];
            instanceData[i].worldPosition = src.worldPosition;
            instanceData[i].atlasIndex = static_cast<float>(src.atlasIndex);
            instanceData[i].size = src.size;
            instanceData[i].sizeMode = src.sizeMode;
            instanceData[i].entityId = src.entityId;
            instanceData[i].colorTint = src.colorTint;
        }

        void* data;
        vk::DeviceSize bufferSize = sizeof(BillboardInstanceData) * currentInstanceCount;
        vk::Result result = device.getLogicalDevice().mapMemory(instanceBufferMemory, 0, bufferSize, {}, &data);
        if (result == vk::Result::eSuccess)
        {
            std::memcpy(data, instanceData.data(), bufferSize);
            device.getLogicalDevice().unmapMemory(instanceBufferMemory);
        }
    }

    void BillboardPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                 uint32_t imageIndex) const
    {
        if (!initialized || currentInstanceCount == 0)
        {
            return;
        }

        // Begin render pass
        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

        // No clear values - we're loading existing content
        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Bind pipeline
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        // Bind descriptor set
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, descriptorSet, nullptr);

        // Push constants
        BillboardPushConstants pushConstants{};
        pushConstants.viewportSize = glm::vec2(
            static_cast<float>(swapChain.getSwapchainExtent().width),
            static_cast<float>(swapChain.getSwapchainExtent().height)
        );
        pushConstants.atlasGridSize = static_cast<float>(AtlasConfig::GRID_SIZE);

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                     0, sizeof(BillboardPushConstants), &pushConstants);

        // Bind vertex buffers
        vk::Buffer vertexBuffers[] = {quadVertexBuffer, instanceBuffer};
        vk::DeviceSize offsets[] = {0, 0};
        commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);

        // Bind index buffer
        commandBuffer.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        // Draw instanced
        commandBuffer.drawIndexed(6, currentInstanceCount, 0, 0, 0);

        commandBuffer.endRenderPass();
    }
}
