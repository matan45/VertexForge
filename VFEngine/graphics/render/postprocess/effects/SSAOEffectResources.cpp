#include "SSAOEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include <cstring>

namespace render::postprocess
{
    void SSAOEffect::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void SSAOEffect::createRenderPasses()
    {
        auto& dev = device.getLogicalDevice();

        auto createR8RenderPass = [&](vk::RenderPass& rp) {
            vk::AttachmentDescription colorAttachment{};
            colorAttachment.format = vk::Format::eR8Unorm;
            colorAttachment.samples = vk::SampleCountFlagBits::e1;
            colorAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
            colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
            colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
            colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::AttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

            vk::SubpassDescription subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;

            vk::SubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
            dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            vk::RenderPassCreateInfo rpInfo{};
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments = &colorAttachment;
            rpInfo.subpassCount = 1;
            rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = 1;
            rpInfo.pDependencies = &dependency;

            rp = dev.createRenderPass(rpInfo);
        };

        createR8RenderPass(ssaoRenderPass);
        createR8RenderPass(blurRenderPass);
    }

    void SSAOEffect::createImages()
    {
        auto& dev = device.getLogicalDevice();

        auto createR8Image = [&](vk::Image& img, vk::DeviceMemory& mem,
                                  vk::ImageView& view, vk::Framebuffer& fb,
                                  vk::RenderPass rp) {
            core::ImageInfoRequest req(dev, device.getPhysicalDevice());
            req.width = currentExtent.width;
            req.height = currentExtent.height;
            req.format = vk::Format::eR8Unorm;
            req.tiling = vk::ImageTiling::eOptimal;
            req.usage = vk::ImageUsageFlagBits::eColorAttachment
                      | vk::ImageUsageFlagBits::eSampled;
            req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

            core::ImageUtilities::createImage(req, img, mem);

            core::ImageViewInfoRequest viewReq(dev, img);
            viewReq.format = vk::Format::eR8Unorm;
            core::ImageUtilities::createImageView(viewReq, view);

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = rp;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &view;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;

            fb = dev.createFramebuffer(fbInfo);
        };

        createR8Image(ssaoRawImage, ssaoRawMemory, ssaoRawImageView,
                       ssaoRawFramebuffer, ssaoRenderPass);
        createR8Image(ssaoBlurredImage, ssaoBlurredMemory, ssaoBlurredImageView,
                       ssaoBlurredFramebuffer, blurRenderPass);
    }

    void SSAOEffect::createDepthImageView()
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = offscreenResources.depthImage.depthImage;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = swapChain.getSwapchainDepthStencilFormat();
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        depthOnlyImageView = device.getLogicalDevice().createImageView(viewInfo);
    }

    void SSAOEffect::createParamsBuffer()
    {
        auto& dev = device.getLogicalDevice();

        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(SSAOParamsUBO);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                          | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferMemory);
        paramsBufferMapped = dev.mapMemory(paramsBufferMemory, 0, sizeof(SSAOParamsUBO));
    }

    void SSAOEffect::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        // SSAO pass: binding 0 = depth sampler, binding 1 = UBO
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

            ssaoDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Blur pass: binding 0 = ssao raw sampler, binding 1 = depth sampler
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

            blurDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Composite pass: binding 0 = blurred SSAO sampler
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            compositeDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }
    }

    void SSAOEffect::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};

        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 4; // depth (ssao), ssao raw + depth (blur), blurred (composite)

        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 3;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void SSAOEffect::createDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 3> layouts = {
            ssaoDescriptorSetLayout, blurDescriptorSetLayout, compositeDescriptorSetLayout
        };

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        auto sets = dev.allocateDescriptorSets(allocInfo);
        ssaoDescriptorSet = sets[0];
        blurDescriptorSet = sets[1];
        compositeDescriptorSet = sets[2];

        {
            vk::DescriptorImageInfo depthImageInfo{};
            depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            depthImageInfo.imageView = depthOnlyImageView;
            depthImageInfo.sampler = sampler;

            vk::DescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = paramsBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(SSAOParamsUBO);

            std::array<vk::WriteDescriptorSet, 2> writes{};

            writes[0].dstSet = ssaoDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &depthImageInfo;

            writes[1].dstSet = ssaoDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &bufferInfo;

            dev.updateDescriptorSets(writes, nullptr);
        }

        {
            vk::DescriptorImageInfo ssaoRawInfo{};
            ssaoRawInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            ssaoRawInfo.imageView = ssaoRawImageView;
            ssaoRawInfo.sampler = sampler;

            vk::DescriptorImageInfo depthImageInfo{};
            depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            depthImageInfo.imageView = depthOnlyImageView;
            depthImageInfo.sampler = sampler;

            std::array<vk::WriteDescriptorSet, 2> writes{};

            writes[0].dstSet = blurDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &ssaoRawInfo;

            writes[1].dstSet = blurDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &depthImageInfo;

            dev.updateDescriptorSets(writes, nullptr);
        }

        {
            vk::DescriptorImageInfo blurredInfo{};
            blurredInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            blurredInfo.imageView = ssaoBlurredImageView;
            blurredInfo.sampler = sampler;

            vk::WriteDescriptorSet write{};
            write.dstSet = compositeDescriptorSet;
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &blurredInfo;

            dev.updateDescriptorSets(write, nullptr);
        }
    }

    void SSAOEffect::loadShaders()
    {
        ssaoShader = std::make_shared<core::Shader>(device);
        ssaoShader->readShader("../../resources/shaders/postprocess/ssao.glsl");

        blurShader = std::make_shared<core::Shader>(device);
        blurShader->readShader("../../resources/shaders/postprocess/ssao_blur.glsl");

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/postprocess/ssao_composite.glsl");
    }

    void SSAOEffect::createSSAOPipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            pipeline.getInputDescriptorSetLayout(), ssaoDescriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        ssaoPipelineLayout = dev.createPipelineLayout(layoutInfo);

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{};
        viewport.width = static_cast<float>(currentExtent.width);
        viewport.height = static_cast<float>(currentExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.extent = currentExtent;

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
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = ssaoShader->getShaderStages();

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
        pipelineInfo.layout = ssaoPipelineLayout;
        pipelineInfo.renderPass = ssaoRenderPass;
        pipelineInfo.subpass = 0;

        ssaoPipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void SSAOEffect::createBlurPipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushConstant{};
        pushConstant.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(float) * 2;

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &blurDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;

        blurPipelineLayout = dev.createPipelineLayout(layoutInfo);

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{};
        viewport.width = static_cast<float>(currentExtent.width);
        viewport.height = static_cast<float>(currentExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.extent = currentExtent;

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
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = blurShader->getShaderStages();

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
        pipelineInfo.layout = blurPipelineLayout;
        pipelineInfo.renderPass = blurRenderPass;
        pipelineInfo.subpass = 0;

        blurPipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void SSAOEffect::createCompositePipeline(vk::RenderPass externalRenderPass)
    {
        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            pipeline.getInputDescriptorSetLayout(), compositeDescriptorSetLayout
        };

        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = externalRenderPass;
        config.extent = currentExtent;
        config.shaderStages = compositeShader->getShaderStages();
        config.descriptorSetLayouts = {setLayouts.begin(), setLayouts.end()};
        config.pushConstantSize = sizeof(float);
        config.pushConstantStages = vk::ShaderStageFlagBits::eFragment;
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;
        config.cullMode = vk::CullModeFlagBits::eNone;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        compositePipeline = result.pipeline;
        compositePipelineLayout = result.pipelineLayout;
    }

    void SSAOEffect::cleanupImages()
    {
        auto& dev = device.getLogicalDevice();

        auto destroyImage = [&](vk::Framebuffer& fb, vk::ImageView& iv,
                                 vk::Image& img, vk::DeviceMemory& mem) {
            if (fb) { dev.destroyFramebuffer(fb); fb = nullptr; }
            if (iv) { dev.destroyImageView(iv); iv = nullptr; }
            if (img) { dev.destroyImage(img); img = nullptr; }
            if (mem) { dev.freeMemory(mem); mem = nullptr; }
        };

        destroyImage(ssaoRawFramebuffer, ssaoRawImageView, ssaoRawImage, ssaoRawMemory);
        destroyImage(ssaoBlurredFramebuffer, ssaoBlurredImageView, ssaoBlurredImage, ssaoBlurredMemory);
    }

    void SSAOEffect::cleanupPipelines()
    {
        auto& dev = device.getLogicalDevice();

        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& pl) {
            if (p) { dev.destroyPipeline(p); p = nullptr; }
            if (pl) { dev.destroyPipelineLayout(pl); pl = nullptr; }
        };

        destroyPipeline(ssaoPipeline, ssaoPipelineLayout);
        destroyPipeline(blurPipeline, blurPipelineLayout);
        destroyPipeline(compositePipeline, compositePipelineLayout);
    }

    void SSAOEffect::updateParamsBuffer()
    {
        const auto& camInfo = pipeline.getCameraData();

        SSAOParamsUBO data{};
        data.projection = camInfo.projectionMatrix;
        data.inverseProjection = glm::inverse(camInfo.projectionMatrix);
        data.params = glm::vec4(currentRadius, currentBias, currentIntensity, currentPower);
        data.noiseScale = glm::vec2(
            static_cast<float>(currentExtent.width) / 4.0f,
            static_cast<float>(currentExtent.height) / 4.0f
        );
        data.kernelSize = currentKernelSize;
        data.nearPlane = camInfo.nearPlane;
        data.farPlane = camInfo.farPlane;

        std::memcpy(paramsBufferMapped, &data, sizeof(SSAOParamsUBO));
    }
}
