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

        for (auto& fb : decalFramebuffers) vkDevice.destroyFramebuffer(fb);
        decalFramebuffers.clear();

        if (decalRenderPass) { vkDevice.destroyRenderPass(decalRenderPass); decalRenderPass = nullptr; }
        if (depthSampler) { vkDevice.destroySampler(depthSampler); depthSampler = nullptr; }
        if (textureSampler) { vkDevice.destroySampler(textureSampler); textureSampler = nullptr; }

        decalDescriptorCache.clear();
        textureCache.clear();
        fallbackWhiteTexture.reset();
        fallbackNormalTexture.reset();

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
        for (auto& fb : decalFramebuffers) vkDevice.destroyFramebuffer(fb);
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
        vk::SamplerCreateInfo depthSamplerInfo{};
        depthSamplerInfo.magFilter = vk::Filter::eNearest;
        depthSamplerInfo.minFilter = vk::Filter::eNearest;
        depthSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        depthSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        depthSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        depthSampler = device.getLogicalDevice().createSampler(depthSamplerInfo);

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
        {
            fallbackWhiteTexture = std::make_unique<core::Texture>(device);
            resource::TextureData texData;
            texData.width = 1; texData.height = 1; texData.numbersOfChannels = 4; texData.mipLevels = 1;
            resource::MipLevelData mip0;
            mip0.width = 1; mip0.height = 1; mip0.data = {255, 255, 255, 255};
            texData.mipData.push_back(std::move(mip0));
            if (!fallbackWhiteTexture->loadTextureFromData(texData, vk::Format::eR8G8B8A8Srgb, false))
            {
                vfLogError("Failed to create fallback white texture for decals");
            }
        }
        {
            fallbackNormalTexture = std::make_unique<core::Texture>(device);
            resource::TextureData texData;
            texData.width = 1; texData.height = 1; texData.numbersOfChannels = 4; texData.mipLevels = 1;
            resource::MipLevelData mip0;
            mip0.width = 1; mip0.height = 1; mip0.data = {128, 128, 255, 255};
            texData.mipData.push_back(std::move(mip0));
            if (!fallbackNormalTexture->loadTextureFromData(texData, vk::Format::eR8G8B8A8Unorm, false))
            {
                vfLogError("Failed to create fallback normal texture for decals");
            }
        }
    }

    void DecalPipeline::createCubeGeometry()
    {
        std::vector<glm::vec3> vertices = {
            {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
            {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
        };

        std::vector<uint32_t> indices = {
            0, 1, 2, 2, 3, 0,  5, 4, 7, 7, 6, 5,
            4, 0, 3, 3, 7, 4,  1, 5, 6, 6, 2, 1,
            3, 2, 6, 6, 7, 3,  4, 5, 1, 1, 0, 4
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

        core::BufferInfoRequest decalReq(vkDevice, physDevice, sizeof(DecalGPUData) * maxDecals,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(decalReq, decalDataBuffer, decalDataMemory);

        core::BufferInfoRequest cameraReq(vkDevice, physDevice, sizeof(CameraUBO),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(cameraReq, cameraUBOBuffer, cameraUBOMemory);
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

        vk::VertexInputBindingDescription vertexBinding{0, sizeof(glm::vec3), vk::VertexInputRate::eVertex};
        vk::VertexInputAttributeDescription vertexAttrib{0, 0, vk::Format::eR32G32B32Sfloat, 0};

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &vertexAttrib;

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};

        vk::PipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        blendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        blendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
        dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicStateInfo.pDynamicStates = dynamicStates.data();

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
        pipelineInfo.pDynamicState = &dynamicStateInfo;
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
}
