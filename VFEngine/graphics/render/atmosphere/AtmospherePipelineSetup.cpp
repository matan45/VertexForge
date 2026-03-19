#include "AtmospherePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/MemoryUtilities.hpp"

namespace render::atmosphere
{
    void AtmospherePipeline::create2DImage(uint32_t width, uint32_t height,
                                            vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.extent = vk::Extent3D{width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        image = dev.createImage(imageInfo);
        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        memory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(image, memory, 0);

        core::ImageViewInfoRequest viewReq(dev, image);
        viewReq.format = vk::Format::eR16G16B16A16Sfloat;
        viewReq.imageType = vk::ImageViewType::e2D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    void AtmospherePipeline::create3DImage(uint32_t width, uint32_t height, uint32_t depth,
                                            vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e3D;
        imageInfo.extent = vk::Extent3D{width, height, depth};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        image = dev.createImage(imageInfo);
        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        memory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(image, memory, 0);

        core::ImageViewInfoRequest viewReq(dev, image);
        viewReq.format = vk::Format::eR16G16B16A16Sfloat;
        viewReq.imageType = vk::ImageViewType::e3D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    namespace
    {
        struct ComputePipelineKit
        {
            vk::DescriptorSetLayout layout;
            vk::DescriptorPool pool;
            vk::DescriptorSet set;
            vk::PipelineLayout pipelineLayout;
            vk::Pipeline pipeline;
        };

        ComputePipelineKit createComputeKit(
            core::Device& device,
            const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
            const std::vector<vk::DescriptorPoolSize>& poolSizes,
            core::Shader& shader)
        {
            auto& dev = device.getLogicalDevice();
            ComputePipelineKit kit{};

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            kit.layout = dev.createDescriptorSetLayout(layoutInfo);

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            kit.pool = dev.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = kit.pool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &kit.layout;
            kit.set = dev.allocateDescriptorSets(allocInfo)[0];

            vk::PipelineLayoutCreateInfo plInfo{};
            plInfo.setLayoutCount = 1;
            plInfo.pSetLayouts = &kit.layout;
            kit.pipelineLayout = dev.createPipelineLayout(plInfo);

            const auto& stages = shader.getShaderStages();
            vk::ComputePipelineCreateInfo cpInfo{};
            cpInfo.stage = stages[0];
            cpInfo.layout = kit.pipelineLayout;
            kit.pipeline = dev.createComputePipeline(nullptr, cpInfo).value;

            return kit;
        }
    }

    void AtmospherePipeline::createTransmittanceLUT()
    {
        create2DImage(256, 64, transmittanceImage, transmittanceMemory, transmittanceView);

        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}
        };
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        transmittanceShader = std::make_shared<core::Shader>(device);
        transmittanceShader->readShader("../../resources/shaders/atmosphere/transmittance_lut.glsl");

        auto kit = createComputeKit(device, bindings, poolSizes, *transmittanceShader);
        transmittanceDSLayout = kit.layout;
        transmittanceDSPool = kit.pool;
        transmittanceDS = kit.set;
        transmittancePipelineLayout = kit.pipelineLayout;
        transmittancePipeline = kit.pipeline;

        auto& dev = device.getLogicalDevice();
        vk::DescriptorImageInfo imgInfo{nullptr, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0] = {transmittanceDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &imgInfo};
        writes[1] = {transmittanceDS, 1, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void AtmospherePipeline::createMultiScatterLUT()
    {
        create2DImage(32, 32, multiScatterImage, multiScatterMemory, multiScatterView);

        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}
        };
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eCombinedImageSampler, 1},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        multiScatterShader = std::make_shared<core::Shader>(device);
        multiScatterShader->readShader("../../resources/shaders/atmosphere/multiscatter_lut.glsl");

        auto kit = createComputeKit(device, bindings, poolSizes, *multiScatterShader);
        multiScatterDSLayout = kit.layout;
        multiScatterDSPool = kit.pool;
        multiScatterDS = kit.set;
        multiScatterPipelineLayout = kit.pipelineLayout;
        multiScatterPipeline = kit.pipeline;

        auto& dev = device.getLogicalDevice();
        vk::DescriptorImageInfo msImgInfo{nullptr, multiScatterView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0] = {multiScatterDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &msImgInfo};
        writes[1] = {multiScatterDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {multiScatterDS, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void AtmospherePipeline::createSkyViewLUT()
    {
        create2DImage(192, 108, skyViewImage, skyViewMemory, skyViewView);

        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {3, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}
        };
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eCombinedImageSampler, 2},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        skyViewShader = std::make_shared<core::Shader>(device);
        skyViewShader->readShader("../../resources/shaders/atmosphere/skyview_lut.glsl");

        auto kit = createComputeKit(device, bindings, poolSizes, *skyViewShader);
        skyViewDSLayout = kit.layout;
        skyViewDSPool = kit.pool;
        skyViewDS = kit.set;
        skyViewPipelineLayout = kit.pipelineLayout;
        skyViewPipeline = kit.pipeline;

        auto& dev = device.getLogicalDevice();
        vk::DescriptorImageInfo svImgInfo{nullptr, skyViewView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo msInfo{lutSampler, multiScatterView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 4> writes{};
        writes[0] = {skyViewDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &svImgInfo};
        writes[1] = {skyViewDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {skyViewDS, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &msInfo};
        writes[3] = {skyViewDS, 3, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void AtmospherePipeline::createAerialPerspectiveLUT()
    {
        create3DImage(32, 32, 32, aerialImage, aerialMemory, aerialView);

        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {3, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}
        };
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eCombinedImageSampler, 2},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        aerialShader = std::make_shared<core::Shader>(device);
        aerialShader->readShader("../../resources/shaders/atmosphere/aerial_perspective_lut.glsl");

        auto kit = createComputeKit(device, bindings, poolSizes, *aerialShader);
        aerialDSLayout = kit.layout;
        aerialDSPool = kit.pool;
        aerialDS = kit.set;
        aerialPipelineLayout = kit.pipelineLayout;
        aerialPipeline = kit.pipeline;

        auto& dev = device.getLogicalDevice();
        vk::DescriptorImageInfo apImgInfo{nullptr, aerialView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo msInfo{lutSampler, multiScatterView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 4> writes{};
        writes[0] = {aerialDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &apImgInfo};
        writes[1] = {aerialDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {aerialDS, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &msInfo};
        writes[3] = {aerialDS, 3, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void AtmospherePipeline::createDepthOnlyView()
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

    void AtmospherePipeline::createSkyRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        vk::SubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1; rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1; rpInfo.pDependencies = &dep;

        skyRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void AtmospherePipeline::createSkyFramebuffers()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        skyFramebuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = skyRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &offscreenResources.colorImages[i].colorImageView;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;
            skyFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void AtmospherePipeline::createCompositeRenderPass()
    {
        compositeRenderPass = skyRenderPass;
    }

    void AtmospherePipeline::createCompositeFramebuffers()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        compositeFramebuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = compositeRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &offscreenResources.colorImages[i].colorImageView;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;
            compositeFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void AtmospherePipeline::createComposite()
    {
        auto& dev = device.getLogicalDevice();

        createCompositeRenderPass();
        createCompositeFramebuffers();
        createDepthOnlyView();

        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        bindings[2] = {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment};

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        compositeDSLayout = dev.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 2};
        poolSizes[1] = {vk::DescriptorType::eUniformBuffer, 1};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        compositeDSPool = dev.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = compositeDSPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &compositeDSLayout;
        compositeDS = dev.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo depthInfo{lutSampler, depthOnlyImageView, vk::ImageLayout::eDepthStencilReadOnlyOptimal};
        vk::DescriptorImageInfo aerialInfo{lutSampler, aerialView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{compositeParamsBuffer, 0, sizeof(AtmosphereCompositeParams)};

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0] = {compositeDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &depthInfo};
        writes[1] = {compositeDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &aerialInfo};
        writes[2] = {compositeDS, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);

        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &compositeDSLayout;
        compositePipelineLayout = dev.createPipelineLayout(plInfo);

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/atmosphere/atmosphere_composite.glsl");

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(currentExtent.width), static_cast<float>(currentExtent.height), 0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, currentExtent};
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
        vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
        depthStencilState.depthTestEnable = VK_FALSE;
        depthStencilState.depthWriteEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState compositeBlend{};
        compositeBlend.blendEnable = VK_TRUE;
        compositeBlend.srcColorBlendFactor = vk::BlendFactor::eOne;
        compositeBlend.dstColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        compositeBlend.colorBlendOp = vk::BlendOp::eAdd;
        compositeBlend.srcAlphaBlendFactor = vk::BlendFactor::eZero;
        compositeBlend.dstAlphaBlendFactor = vk::BlendFactor::eOne;
        compositeBlend.alphaBlendOp = vk::BlendOp::eAdd;
        compositeBlend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                         vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        vk::PipelineColorBlendStateCreateInfo blending{};
        blending.attachmentCount = 1; blending.pAttachments = &compositeBlend;

        const auto& stages = compositeShader->getShaderStages();
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencilState;
        pipelineInfo.pColorBlendState = &blending;
        pipelineInfo.layout = compositePipelineLayout;
        pipelineInfo.renderPass = compositeRenderPass;
        compositePipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }
}
