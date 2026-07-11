#include "SelectionOutlineComposite.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "stats/FrameDrawStats.hpp"
#include "print/Log.hpp"

namespace render::selection
{
    namespace
    {
        struct OutlinePushConstants
        {
            float texelSizeX;
            float texelSizeY;
            float padding0;
            float padding1;
            float colorR;
            float colorG;
            float colorB;
            float colorA;
        };

        // Editor selection accent #FFA100
        constexpr float kOutlineR = 1.0f;
        constexpr float kOutlineG = 161.0f / 255.0f;
        constexpr float kOutlineB = 0.0f;
    }

    SelectionOutlineComposite::SelectionOutlineComposite(core::Device& device,
                                                         core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    SelectionOutlineComposite::~SelectionOutlineComposite()
    {
        cleanup();
    }

    void SelectionOutlineComposite::init()
    {
        createDescriptorResources();
        loadShader();
        createPipeline();
        initialized = pipeline != nullptr;
    }

    void SelectionOutlineComposite::cleanup()
    {
        if (!initialized && !descriptorSetLayout)
        {
            return;
        }

        auto& dev = device.getLogicalDevice();
        dev.waitIdle();

        if (pipeline)
        {
            dev.destroyPipeline(pipeline);
            pipeline = nullptr;
        }
        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }
        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }
        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        boundMaskView = nullptr;
        initialized = false;
    }

    void SelectionOutlineComposite::setMaskInput(vk::ImageView maskView, vk::Sampler maskSampler)
    {
        if (!initialized || !maskView || maskView == boundMaskView)
        {
            return;
        }

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = maskView;
        imageInfo.sampler = maskSampler;

        vk::WriteDescriptorSet write{};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        device.getLogicalDevice().updateDescriptorSets(write, nullptr);

        boundMaskView = maskView;
    }

    void SelectionOutlineComposite::executeGraphManaged(const vk::CommandBuffer& commandBuffer,
                                                        vk::ImageView sceneColorView,
                                                        vk::Extent2D extent,
                                                        vk::Extent2D maskExtent)
    {
        if (!initialized || !boundMaskView)
        {
            return;
        }

        auto colorAttach = core::colorLoad(sceneColorView);

        core::DynamicRenderingInfo info{};
        info.extent = extent;
        info.colorAttachments = {colorAttach};

        core::beginDynamicRendering(commandBuffer, info);

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        commandBuffer.setScissor(0, scissor);

        OutlinePushConstants pc{};
        pc.texelSizeX = maskExtent.width > 0 ? 1.0f / static_cast<float>(maskExtent.width) : 0.0f;
        pc.texelSizeY = maskExtent.height > 0 ? 1.0f / static_cast<float>(maskExtent.height) : 0.0f;
        pc.colorR = kOutlineR;
        pc.colorG = kOutlineG;
        pc.colorB = kOutlineB;
        pc.colorA = 1.0f;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);
        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(OutlinePushConstants), &pc);
        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count(render::DrawCategory::PostProcess);

        core::endDynamicRendering(commandBuffer);
    }

    void SelectionOutlineComposite::createDescriptorResources()
    {
        auto& dev = device.getLogicalDevice();

        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        descriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        descriptorPool = dev.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSet = dev.allocateDescriptorSets(allocInfo)[0];
    }

    void SelectionOutlineComposite::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/selection/selection_outline.glsl");
        if (shader->getShaderStages().size() < 2)
        {
            vfLogError("SelectionOutlineComposite: failed to load shader: {}",
                       shader->getLastCompilationError());
        }
    }

    void SelectionOutlineComposite::createPipeline()
    {
        if (!shader || shader->getShaderStages().size() < 2)
        {
            return;
        }

        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(OutlinePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        pipelineLayout = dev.createPipelineLayout(layoutInfo);

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        // Dynamic viewport/scissor — no recreate needed on resize.
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<vk::DynamicState, 2> dynamicStates = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

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

        // Non-edge fragments discard, edge fragments write opaque — no blending.
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = shader->getShaderStages();

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.subpass = 0;

        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        pipelineInfo.pNext = &renderingInfo;

        pipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }
}
