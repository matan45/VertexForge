#include "ShadowPassPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::shadow
{
    ShadowPassPipeline::ShadowPassPipeline(core::Device& device)
        : device(device)
    {
    }

    ShadowPassPipeline::~ShadowPassPipeline()
    {
        cleanup();
    }

    void ShadowPassPipeline::init(vk::DescriptorSetLayout perDrawLayout,
                                   vk::DescriptorSetLayout meshletDataLayout,
                                   vk::DescriptorSetLayout vertexDataLayout,
                                   vk::DescriptorSetLayout boneMatrixLayout,
                                   vk::Format atlasDepthFormat)
    {
        if (initialized)
        {
            return;
        }

        cachedPerDrawLayout = perDrawLayout;
        cachedMeshletDataLayout = meshletDataLayout;
        cachedVertexDataLayout = vertexDataLayout;
        cachedBoneMatrixLayout = boneMatrixLayout;
        depthFormat = atlasDepthFormat;

        createCameraDescriptorResources();
        createShadowRenderPass();
        createShadowRenderPassLoad();
        createShadowPipeline();

        initialized = true;
    }

    void ShadowPassPipeline::cleanup()
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        destroyFramebuffer();

        if (shadowShader)
        {
            shadowShader->cleanUp();
            shadowShader.reset();
        }

        if (shadowPipeline)
        {
            vkDevice.destroyPipeline(shadowPipeline);
            shadowPipeline = nullptr;
        }

        if (shadowPipelineLayout)
        {
            vkDevice.destroyPipelineLayout(shadowPipelineLayout);
            shadowPipelineLayout = nullptr;
        }

        if (shadowRenderPass)
        {
            vkDevice.destroyRenderPass(shadowRenderPass);
            shadowRenderPass = nullptr;
        }

        if (shadowRenderPassLoad)
        {
            vkDevice.destroyRenderPass(shadowRenderPassLoad);
            shadowRenderPassLoad = nullptr;
        }

        if (cameraDescriptorPool)
        {
            vkDevice.destroyDescriptorPool(cameraDescriptorPool);
            cameraDescriptorPool = nullptr;
            cameraDescriptorSet = nullptr;
        }

        if (cameraUBOLayout)
        {
            vkDevice.destroyDescriptorSetLayout(cameraUBOLayout);
            cameraUBOLayout = nullptr;
        }

        initialized = false;
    }

    void ShadowPassPipeline::createShadowRenderPass()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 0;
        subpass.pColorAttachments = nullptr;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<vk::SubpassDependency, 2> dependencies{};

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[0].dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                         vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
        dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &depthAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        shadowRenderPass = vkDevice.createRenderPass(renderPassInfo);
    }

    void ShadowPassPipeline::createShadowRenderPassLoad()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;  // Preserve cached tiles
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 0;
        subpass.pColorAttachments = nullptr;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<vk::SubpassDependency, 2> dependencies{};

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[0].dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                         vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
        dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &depthAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        shadowRenderPassLoad = vkDevice.createRenderPass(renderPassInfo);
    }

    void ShadowPassPipeline::createShadowPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shadowShader = std::make_unique<core::Shader>(device);
        shadowShader->readShader("../../resources/shaders/shadow/shadow.glsl");

        const auto& stages = shadowShader->getShaderStages();
        if (stages.size() < 2)
        {
            vfLogError("ShadowPassPipeline: Failed to load shadow shaders: {}",
                        shadowShader->getLastCompilationError());
            return;
        }

        bool hasTask = false, hasMesh = false;
        for (const auto& stage : stages)
        {
            if (stage.stage == vk::ShaderStageFlagBits::eTaskEXT) hasTask = true;
            if (stage.stage == vk::ShaderStageFlagBits::eMeshEXT) hasMesh = true;
        }

        if (!hasTask || !hasMesh)
        {
            vfLogError("ShadowPassPipeline: Missing shader stages (Task={}, Mesh={})", hasTask, hasMesh);
            return;
        }

        std::array<vk::DescriptorSetLayout, 5> setLayouts = {
            cachedPerDrawLayout,
            cachedMeshletDataLayout,
            cachedVertexDataLayout,
            cachedBoneMatrixLayout,
            cameraUBOLayout
        };

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(ShadowPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        shadowPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        std::array<vk::DynamicState, 3> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
            vk::DynamicState::eDepthBias
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = nullptr;
        viewportState.scissorCount = 1;
        viewportState.pScissors = nullptr;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 0.0f;
        rasterizer.depthBiasSlopeFactor = 0.0f;
        rasterizer.depthBiasClamp = 0.0f;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 0;
        colorBlending.pAttachments = nullptr;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = nullptr;
        pipelineInfo.pInputAssemblyState = nullptr;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = shadowPipelineLayout;
        pipelineInfo.renderPass = shadowRenderPass;
        pipelineInfo.subpass = 0;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("ShadowPassPipeline: Failed to create pipeline");
            return;
        }

        shadowPipeline = result.value;
    }

    void ShadowPassPipeline::createFramebuffer(vk::ImageView depthImageView, uint32_t width, uint32_t height)
    {
        if (atlasFramebuffer)
            destroyFramebuffer();

        vk::Device vkDevice = device.getLogicalDevice();

        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = shadowRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &depthImageView;
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        atlasFramebuffer = vkDevice.createFramebuffer(framebufferInfo);

        // Create framebuffer for the eLoad render pass (same image, different render pass)
        framebufferInfo.renderPass = shadowRenderPassLoad;
        atlasFramebufferLoad = vkDevice.createFramebuffer(framebufferInfo);
    }

    void ShadowPassPipeline::destroyFramebuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (atlasFramebuffer)
        {
            vkDevice.destroyFramebuffer(atlasFramebuffer);
            atlasFramebuffer = nullptr;
        }
        if (atlasFramebufferLoad)
        {
            vkDevice.destroyFramebuffer(atlasFramebufferLoad);
            atlasFramebufferLoad = nullptr;
        }
    }

    void ShadowPassPipeline::createCameraDescriptorResources()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create descriptor set layout with a single UBO binding (set 4, binding 0)
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eUniformBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        cameraUBOLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Create descriptor pool
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eUniformBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        cameraDescriptorPool = vkDevice.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = cameraDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &cameraUBOLayout;

        cameraDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void ShadowPassPipeline::updateCameraDescriptor(vk::Buffer cameraBuffer, vk::DeviceSize bufferSize)
    {
        if (!cameraDescriptorSet)
            return;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = bufferSize;

        vk::WriteDescriptorSet write{};
        write.dstSet = cameraDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &bufferInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
    }
}
