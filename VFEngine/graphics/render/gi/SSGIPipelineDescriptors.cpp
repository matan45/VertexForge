#include "SSGIPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"

namespace render::gi
{
    void SSGIPipeline::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            traceSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            traceSet1Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            temporalSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        {
            std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[2].binding = 2;
            bindings[2].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            temporalSet1Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            denoiseSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            compositeSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }
    }

    void SSGIPipeline::createDescriptorPool()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = imageCount + 12;

        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 3;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = imageCount + 8;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void SSGIPipeline::createDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        {
            std::vector<vk::DescriptorSetLayout> layouts(imageCount, traceSet0Layout);
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = imageCount;
            allocInfo.pSetLayouts = layouts.data();
            traceSet0PerImage = dev.allocateDescriptorSets(allocInfo);
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &traceSet1Layout;
            traceSet1 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &temporalSet0Layout;
            temporalSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        {
            std::array<vk::DescriptorSetLayout, 2> layouts = {temporalSet1Layout, temporalSet1Layout};
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts.data();
            auto sets = dev.allocateDescriptorSets(allocInfo);
            temporalSet1PerHistory[0] = sets[0];
            temporalSet1PerHistory[1] = sets[1];
        }

        {
            std::array<vk::DescriptorSetLayout, 2> layouts = {denoiseSet0Layout, denoiseSet0Layout};
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts.data();
            auto sets = dev.allocateDescriptorSets(allocInfo);
            denoiseHorizSet0PerHistory[0] = sets[0];
            denoiseHorizSet0PerHistory[1] = sets[1];
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &denoiseSet0Layout;
            denoiseSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &compositeSet0Layout;
            compositeSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }
    }

    void SSGIPipeline::updateDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        vk::DescriptorImageInfo depthImageInfo{};
        depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthImageInfo.imageView = depthOnlyImageView;
        depthImageInfo.sampler = sampler;

        vk::DescriptorBufferInfo uboInfo{};
        uboInfo.buffer = paramsBuffer;
        uboInfo.offset = 0;
        uboInfo.range = sizeof(SSGIParamsUBO);

        std::vector<vk::WriteDescriptorSet> writes;

        std::vector<vk::DescriptorImageInfo> sceneColorInfos(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            sceneColorInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            sceneColorInfos[i].imageView = offscreenResources.colorImages[i].colorImageView;
            sceneColorInfos[i].sampler = sampler;

            vk::WriteDescriptorSet w{};
            w.dstSet = traceSet0PerImage[i];
            w.dstBinding = 0;
            w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w.descriptorCount = 1;
            w.pImageInfo = &sceneColorInfos[i];
            writes.push_back(w);
        }

        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = traceSet1;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &depthImageInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = traceSet1;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eUniformBuffer;
            w1.descriptorCount = 1;
            w1.pBufferInfo = &uboInfo;
            writes.push_back(w1);
        }

        vk::DescriptorImageInfo ssgiRawInfo{};
        ssgiRawInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssgiRawInfo.imageView = ssgiRawImageView;
        ssgiRawInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w{};
            w.dstSet = temporalSet0;
            w.dstBinding = 0;
            w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w.descriptorCount = 1;
            w.pImageInfo = &ssgiRawInfo;
            writes.push_back(w);
        }

        std::array<vk::DescriptorImageInfo, 2> historyInfos{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            historyInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            historyInfos[i].imageView = ssgiHistoryImageViews[i];
            historyInfos[i].sampler = sampler;

            vk::WriteDescriptorSet wHist{};
            wHist.dstSet = temporalSet1PerHistory[i];
            wHist.dstBinding = 0;
            wHist.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            wHist.descriptorCount = 1;
            wHist.pImageInfo = &historyInfos[i];
            writes.push_back(wHist);

            vk::WriteDescriptorSet wDepth{};
            wDepth.dstSet = temporalSet1PerHistory[i];
            wDepth.dstBinding = 1;
            wDepth.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            wDepth.descriptorCount = 1;
            wDepth.pImageInfo = &depthImageInfo;
            writes.push_back(wDepth);

            vk::WriteDescriptorSet wUbo{};
            wUbo.dstSet = temporalSet1PerHistory[i];
            wUbo.dstBinding = 2;
            wUbo.descriptorType = vk::DescriptorType::eUniformBuffer;
            wUbo.descriptorCount = 1;
            wUbo.pBufferInfo = &uboInfo;
            writes.push_back(wUbo);
        }

        std::array<vk::DescriptorImageInfo, 2> denoiseHistoryInfos{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            denoiseHistoryInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            denoiseHistoryInfos[i].imageView = ssgiHistoryImageViews[i];
            denoiseHistoryInfos[i].sampler = sampler;

            vk::WriteDescriptorSet w0{};
            w0.dstSet = denoiseHorizSet0PerHistory[i];
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &denoiseHistoryInfos[i];
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = denoiseHorizSet0PerHistory[i];
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        vk::DescriptorImageInfo ssgiDenoiseHorizInfo{};
        ssgiDenoiseHorizInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssgiDenoiseHorizInfo.imageView = ssgiDenoiseHorizImageView;
        ssgiDenoiseHorizInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = denoiseSet0;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &ssgiDenoiseHorizInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = denoiseSet0;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        vk::DescriptorImageInfo ssgiDenoisedInfo{};
        ssgiDenoisedInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssgiDenoisedInfo.imageView = ssgiDenoisedImageView;
        ssgiDenoisedInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = compositeSet0;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &ssgiDenoisedInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = compositeSet0;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        dev.updateDescriptorSets(writes, nullptr);
    }

    void SSGIPipeline::loadShaders()
    {
        traceShader = std::make_shared<core::Shader>(device);
        traceShader->readShader("../../resources/shaders/gi/ssgi_trace.glsl");

        temporalShader = std::make_shared<core::Shader>(device);
        temporalShader->readShader("../../resources/shaders/gi/ssgi_temporal.glsl");

        denoiseShader = std::make_shared<core::Shader>(device);
        denoiseShader->readShader("../../resources/shaders/gi/ssgi_denoise.glsl");

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/gi/ssgi_composite.glsl");
    }

    vk::Pipeline SSGIPipeline::createFullscreenPipeline(vk::PipelineLayout layout,
                                                         vk::Format colorFormat,
                                                         vk::Extent2D extent,
                                                         const std::shared_ptr<core::Shader>& shdr,
                                                         bool additiveBlend)
    {
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.extent = extent;

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

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        if (additiveBlend)
        {
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        }
        else
        {
            colorBlendAttachment.blendEnable = VK_FALSE;
        }
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR
                                            | vk::ColorComponentFlagBits::eG
                                            | vk::ColorComponentFlagBits::eB
                                            | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = shdr->getShaderStages();

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = layout;
        pipelineInfo.subpass = 0;

        // Dynamic rendering: specify color format via pNext
        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        pipelineInfo.pNext = &renderingInfo;

        return device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

}
