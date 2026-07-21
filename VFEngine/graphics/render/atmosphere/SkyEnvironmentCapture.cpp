#include "SkyEnvironmentCapture.hpp"
#include "AtmospherePipeline.hpp"
#include "../ibl/BRDFLUTGenerator.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "print/Log.hpp"

#include <array>
#include <cstring>

namespace render::atmosphere
{
    // Sync2 stage/access shorthands used by the barriers throughout this file (both the anonymous-
    // namespace helpers and the SkyEnvironmentCapture members below reference them unqualified).
    // Only the STAGE enum is `using enum`-ed: `using enum` imports *every* enumerator, and
    // PipelineStageFlagBits2 + AccessFlagBits2 both declare eNone/eNoneKHR, so importing both would
    // be an ill-formed redeclaration (MSVC C2874). The four access flags are aliased individually
    // (their names don't exist in the stage enum, so there is no collision).
    using enum vk::PipelineStageFlagBits2;
    constexpr auto eColorAttachmentWrite = vk::AccessFlagBits2::eColorAttachmentWrite;
    constexpr auto eTransferRead = vk::AccessFlagBits2::eTransferRead;
    constexpr auto eTransferWrite = vk::AccessFlagBits2::eTransferWrite;
    constexpr auto eShaderRead = vk::AccessFlagBits2::eShaderRead;

    namespace
    {
        // Explicit synchronization2 image barrier — the standard ImageUtilities helper does not
        // cover the shaderReadOnly<->transferDst / transferDst<->transferSrc transitions the capture
        // needs, so the per-frame path records barriers directly (like the swapchain helpers).
        void imageBarrier(const vk::CommandBuffer& cmd, vk::Image image,
                          vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
                          vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess,
                          vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                          uint32_t baseMip, uint32_t mipCount, uint32_t layerCount)
        {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = srcStage;
            barrier.srcAccessMask = srcAccess;
            barrier.dstStageMask = dstStage;
            barrier.dstAccessMask = dstAccess;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eColor, baseMip, mipCount, 0, layerCount};

            vk::DependencyInfo depInfo{};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;
            cmd.pipelineBarrier2KHR(depInfo);
        }

        void renderCubeFace(const vk::CommandBuffer& cmd, vk::Pipeline pipeline,
                            vk::PipelineLayout layout, vk::DescriptorSet ds, vk::Buffer vbo,
                            uint32_t vertexCount, vk::ImageView targetView, uint32_t size,
                            const glm::mat4& viewProj, const float* extraFloat)
        {
            vk::Viewport vp{};
            vp.x = 0.0f;
            vp.y = 0.0f;
            vp.width = static_cast<float>(size);
            vp.height = static_cast<float>(size);
            vp.minDepth = 0.0f;
            vp.maxDepth = 1.0f;
            vk::Rect2D scissor{vk::Offset2D{0, 0}, vk::Extent2D{size, size}};

            core::DynamicRenderingInfo info{};
            info.extent = vk::Extent2D{size, size};
            info.colorAttachments = {
                core::colorClear(targetView, vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}})
            };

            core::beginDynamicRendering(cmd, info);
            cmd.setViewport(0, 1, &vp);
            cmd.setScissor(0, 1, &scissor);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, ds, {});
            if (extraFloat)
            {
                // Passes that need a fragment-stage scalar alongside viewProj — the prefilter
                // (roughness) and the sky capture (ambientIntensity) — declare ONE combined range
                // {vertex|fragment, 0, 68}. VUID-01796 requires the push to name ALL stages of any
                // overlapping range, so the two must go as a single blob, not split by stage.
                struct PushBlob { glm::mat4 viewProj; float value; } blob{viewProj, *extraFloat};
                cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                  0, sizeof(glm::mat4) + sizeof(float), &blob);
            }
            else
            {
                cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &viewProj);
            }
            vk::DeviceSize offset = 0;
            cmd.bindVertexBuffers(0, vbo, offset);
            cmd.draw(vertexCount, 1, 0, 0);
            core::endDynamicRendering(cmd);
        }

        void copyToLayer(const vk::CommandBuffer& cmd, vk::Image src, vk::Image dst,
                         uint32_t dstMip, uint32_t dstLayer, uint32_t size)
        {
            vk::ImageCopy region{};
            region.srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            region.srcOffset = vk::Offset3D{0, 0, 0};
            region.dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, dstMip, dstLayer, 1};
            region.dstOffset = vk::Offset3D{0, 0, 0};
            region.extent = vk::Extent3D{size, size, 1};
            cmd.copyImage(src, vk::ImageLayout::eTransferSrcOptimal, dst,
                          vk::ImageLayout::eTransferDstOptimal, region);
        }

        vk::Sampler makeSampler(const vk::Device& dev)
        {
            vk::SamplerCreateInfo s{};
            s.magFilter = vk::Filter::eLinear;
            s.minFilter = vk::Filter::eLinear;
            s.mipmapMode = vk::SamplerMipmapMode::eLinear; // trilinear across prefilter mips
            s.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            s.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            s.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            s.minLod = 0.0f;
            s.maxLod = VK_LOD_CLAMP_NONE; // sample LOD 0..4 correctly (prefilter has 5 mips)
            s.anisotropyEnable = VK_FALSE;
            s.maxAnisotropy = 1.0f;
            s.borderColor = vk::BorderColor::eFloatOpaqueBlack;
            s.unnormalizedCoordinates = VK_FALSE;
            return dev.createSampler(s);
        }
    }

    SkyEnvironmentCapture::SkyEnvironmentCapture(core::Device& device)
        : device{device}
    {
    }

    SkyEnvironmentCapture::~SkyEnvironmentCapture()
    {
        cleanup();
    }

    void SkyEnvironmentCapture::init(const vk::CommandPool& pool, AtmospherePipeline& atmosphere)
    {
        if (initialized)
            return;

        commandPool = pool;
        skyViewView = atmosphere.getSkyViewView();
        transmittanceView = atmosphere.getTransmittanceView();
        atmosphereParamsBuffer = atmosphere.getParamsBuffer();
        lutSampler = atmosphere.getLUTSampler();

        skyShader = std::make_shared<core::Shader>(device);
        skyShader->readShader("../../resources/shaders/atmosphere/sky_env_capture.glsl");
        irradianceShader = std::make_shared<core::Shader>(device);
        irradianceShader->readShader("../../resources/shaders/ibl/cubemap_irradiance_convolution.glsl");
        prefilterShader = std::make_shared<core::Shader>(device);
        prefilterShader->readShader("../../resources/shaders/ibl/sky_prefilter.glsl");

        // Cube geometry (shared by all three capture pipelines).
        core::BufferInfoRequest vbReq(device.getLogicalDevice(), device.getPhysicalDevice());
        vbReq.size = sizeof(ibl::cubeVertices[0]) * ibl::cubeVertices.size();
        vbReq.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        vbReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(vbReq, cubeVertexBuffer, cubeVertexAllocation, device.getMemoryManager());
        if (cubeVertexAllocation.mappedPtr)
            std::memcpy(cubeVertexAllocation.mappedPtr, ibl::cubeVertices.data(), vbReq.size);

        // BRDF LUT: owned here (view-independent, static) so dynamic ambient works with no scene HDR IBL.
        brdfGenerator = std::make_unique<ibl::BRDFLUTGenerator>(device);
        brdfGenerator->generate(commandPool);
        brdfLUT = brdfGenerator->getImageData();

        auto initCmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool);
        createImages(initCmd.get());
        core::Utilities::endSingleTimeCommands(device, initCmd);

        createDescriptors();
        createPipelines();

        initialized = true;
    }

    void SkyEnvironmentCapture::createImages(const vk::CommandBuffer& initCmd)
    {
        const vk::Device dev = device.getLogicalDevice();
        auto& mem = device.getMemoryManager();

        auto makeCube = [&](ibl::ImageData& out, uint32_t size, uint32_t mips, bool sampled)
        {
            core::ImageInfoRequest req(dev, device.getPhysicalDevice());
            req.format = HDR_FORMAT;
            req.layers = 6;
            req.mipLevels = mips;
            req.width = size;
            req.height = size;
            req.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc |
                (sampled ? vk::ImageUsageFlagBits::eSampled : vk::ImageUsageFlags{});
            req.imageFlags = vk::ImageCreateFlagBits::eCubeCompatible;
            core::ImageUtilities::createImage(req, out.image, out.imageAllocation, mem);

            if (sampled)
            {
                core::ImageViewInfoRequest viewReq(dev, out.image);
                viewReq.format = HDR_FORMAT;
                viewReq.layerCount = 6;
                viewReq.mipLevels = mips;
                viewReq.imageType = vk::ImageViewType::eCube;
                core::ImageUtilities::createImageView(viewReq, out.imageView);
                out.sampler = makeSampler(dev);
            }
        };

        auto makeHelper = [&](ibl::OffScreenHelper& out, uint32_t size)
        {
            core::ImageInfoRequest req(dev, device.getPhysicalDevice());
            req.format = HDR_FORMAT;
            req.width = size;
            req.height = size;
            req.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
            core::ImageUtilities::createImage(req, out.image, out.allocation, mem);

            core::ImageViewInfoRequest viewReq(dev, out.image);
            viewReq.format = HDR_FORMAT;
            core::ImageUtilities::createImageView(viewReq, out.view);
        };

        makeCube(envCube, ENV_SIZE, 1, true);
        makeCube(stagingIrradiance, IRR_SIZE, 1, false);
        makeCube(stagingPrefilter, PREFILTER_SIZE, PREFILTER_MIPS, false);
        makeCube(irradianceLive, IRR_SIZE, 1, true);
        makeCube(prefilterLive, PREFILTER_SIZE, PREFILTER_MIPS, true);
        makeHelper(helperHi, ENV_SIZE);
        makeHelper(helperLo, IRR_SIZE);

        using vk::ImageLayout;
        const auto color = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::transitionImageLayout(initCmd, envCube.image, ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal, color, 6, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, stagingIrradiance.image, ImageLayout::eUndefined,
            ImageLayout::eTransferDstOptimal, color, 6, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, stagingPrefilter.image, ImageLayout::eUndefined,
            ImageLayout::eTransferDstOptimal, color, 6, PREFILTER_MIPS);
        core::ImageUtilities::transitionImageLayout(initCmd, irradianceLive.image, ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal, color, 6, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, prefilterLive.image, ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal, color, 6, PREFILTER_MIPS);
        core::ImageUtilities::transitionImageLayout(initCmd, helperHi.image, ImageLayout::eUndefined,
            ImageLayout::eColorAttachmentOptimal, color, 1, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, helperLo.image, ImageLayout::eUndefined,
            ImageLayout::eColorAttachmentOptimal, color, 1, 1);

        envLayout = ImageLayout::eShaderReadOnlyOptimal;
    }

    void SkyEnvironmentCapture::createDescriptors()
    {
        const vk::Device dev = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 3};
        poolSizes[1] = {vk::DescriptorType::eUniformBuffer, 1};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = dev.createDescriptorPool(poolInfo);

        // Sky capture set: b0 sky-view LUT, b1 transmittance LUT, b2 atmosphere params UBO.
        std::array<vk::DescriptorSetLayoutBinding, 3> skyBindings{};
        skyBindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        skyBindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        skyBindings[2] = {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo skyLayoutInfo{};
        skyLayoutInfo.bindingCount = static_cast<uint32_t>(skyBindings.size());
        skyLayoutInfo.pBindings = skyBindings.data();
        skyDSLayout = dev.createDescriptorSetLayout(skyLayoutInfo);

        // Env-sample set (irradiance + prefilter): b0 env cubemap.
        vk::DescriptorSetLayoutBinding envBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo envLayoutInfo{};
        envLayoutInfo.bindingCount = 1;
        envLayoutInfo.pBindings = &envBinding;
        envDSLayout = dev.createDescriptorSetLayout(envLayoutInfo);

        vk::DescriptorSetAllocateInfo skyAlloc{descriptorPool, 1, &skyDSLayout};
        skyDS = dev.allocateDescriptorSets(skyAlloc)[0];
        vk::DescriptorSetAllocateInfo envAlloc{descriptorPool, 1, &envDSLayout};
        envDS = dev.allocateDescriptorSets(envAlloc)[0];

        // Sky-view / transmittance LUTs are sampled from eGeneral (matches AtmospherePipeline).
        vk::DescriptorImageInfo skyViewInfo{lutSampler, skyViewView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo paramsInfo{atmosphereParamsBuffer, 0, VK_WHOLE_SIZE};
        vk::DescriptorImageInfo envInfo{envCube.sampler, envCube.imageView, vk::ImageLayout::eShaderReadOnlyOptimal};

        std::array<vk::WriteDescriptorSet, 4> writes{};
        writes[0] = {skyDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &skyViewInfo};
        writes[1] = {skyDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {skyDS, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &paramsInfo};
        writes[3] = {envDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &envInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void SkyEnvironmentCapture::createPipelines()
    {
        const vk::Device dev = device.getLogicalDevice();

        // Pipeline layouts + push-constant ranges.
        vk::PushConstantRange vpRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)};

        // The sky capture carries ambientIntensity into the fragment stage alongside viewProj, so it
        // needs the same combined vertex|fragment range shape as the prefilter (see VUID-01796 note
        // in renderCubeFace) — NOT the vertex-only vpRange the irradiance pass uses.
        vk::PushConstantRange skyRange{
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
            sizeof(glm::mat4) + sizeof(float)};
        vk::PipelineLayoutCreateInfo skyLayoutInfo{};
        skyLayoutInfo.setLayoutCount = 1;
        skyLayoutInfo.pSetLayouts = &skyDSLayout;
        skyLayoutInfo.pushConstantRangeCount = 1;
        skyLayoutInfo.pPushConstantRanges = &skyRange;
        skyPipelineLayout = dev.createPipelineLayout(skyLayoutInfo);

        vk::PipelineLayoutCreateInfo irrLayoutInfo{};
        irrLayoutInfo.setLayoutCount = 1;
        irrLayoutInfo.pSetLayouts = &envDSLayout;
        irrLayoutInfo.pushConstantRangeCount = 1;
        irrLayoutInfo.pPushConstantRanges = &vpRange;
        irradiancePipelineLayout = dev.createPipelineLayout(irrLayoutInfo);

        vk::PushConstantRange prefilterRange{
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
            sizeof(glm::mat4) + sizeof(float)};
        vk::PipelineLayoutCreateInfo prefilterLayoutInfo{};
        prefilterLayoutInfo.setLayoutCount = 1;
        prefilterLayoutInfo.pSetLayouts = &envDSLayout;
        prefilterLayoutInfo.pushConstantRangeCount = 1;
        prefilterLayoutInfo.pPushConstantRanges = &prefilterRange;
        prefilterPipelineLayout = dev.createPipelineLayout(prefilterLayoutInfo);

        // Shared fixed-function state.
        vk::VertexInputBindingDescription binding{0, sizeof(glm::vec3), vk::VertexInputRate::eVertex};
        vk::VertexInputAttributeDescription attribute{0, 0, vk::Format::eR32G32B32Sfloat, 0};
        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &attribute;

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eClockwise;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        blendAttachment.blendEnable = VK_FALSE;
        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        vk::Format colorFormat = HDR_FORMAT;
        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        auto build = [&](const std::shared_ptr<core::Shader>& shader, vk::PipelineLayout layout)
        {
            vk::GraphicsPipelineCreateInfo info{};
            info.stageCount = static_cast<uint32_t>(shader->getShaderStages().size());
            info.pStages = shader->getShaderStages().data();
            info.pVertexInputState = &vertexInput;
            info.pInputAssemblyState = &inputAssembly;
            info.pViewportState = &viewportState;
            info.pRasterizationState = &rasterizer;
            info.pMultisampleState = &multisampling;
            info.pColorBlendState = &colorBlending;
            info.pDynamicState = &dynamicState;
            info.layout = layout;
            info.pNext = &renderingInfo;
            return dev.createGraphicsPipeline(nullptr, info).value;
        };

        skyPipeline = build(skyShader, skyPipelineLayout);
        irradiancePipeline = build(irradianceShader, irradiancePipelineLayout);
        prefilterPipeline = build(prefilterShader, prefilterPipelineLayout);
    }

    void SkyEnvironmentCapture::recordEnvFace(const vk::CommandBuffer& cmd, uint32_t face)
    {
        const glm::mat4 viewProj = ibl::CameraViewMatrix::captureProjection * ibl::CameraViewMatrix::captureViews[face];
        renderCubeFace(cmd, skyPipeline, skyPipelineLayout, skyDS, cubeVertexBuffer,
                       static_cast<uint32_t>(ibl::cubeVertices.size()), helperHi.view, ENV_SIZE, viewProj,
                       &ambientIntensity);

        imageBarrier(cmd, helperHi.image, eColorAttachmentOutput, eColorAttachmentWrite,
                     eTransfer, eTransferRead, vk::ImageLayout::eColorAttachmentOptimal,
                     vk::ImageLayout::eTransferSrcOptimal, 0, 1, 1);
        copyToLayer(cmd, helperHi.image, envCube.image, 0, face, ENV_SIZE);
        imageBarrier(cmd, helperHi.image, eTransfer, eTransferRead, eColorAttachmentOutput,
                     eColorAttachmentWrite, vk::ImageLayout::eTransferSrcOptimal,
                     vk::ImageLayout::eColorAttachmentOptimal, 0, 1, 1);
    }

    void SkyEnvironmentCapture::recordIrradianceFace(const vk::CommandBuffer& cmd, uint32_t face)
    {
        const glm::mat4 viewProj = ibl::CameraViewMatrix::captureProjection * ibl::CameraViewMatrix::captureViews[face];
        renderCubeFace(cmd, irradiancePipeline, irradiancePipelineLayout, envDS, cubeVertexBuffer,
                       static_cast<uint32_t>(ibl::cubeVertices.size()), helperLo.view, IRR_SIZE, viewProj, nullptr);

        imageBarrier(cmd, helperLo.image, eColorAttachmentOutput, eColorAttachmentWrite,
                     eTransfer, eTransferRead, vk::ImageLayout::eColorAttachmentOptimal,
                     vk::ImageLayout::eTransferSrcOptimal, 0, 1, 1);
        copyToLayer(cmd, helperLo.image, stagingIrradiance.image, 0, face, IRR_SIZE);
        imageBarrier(cmd, helperLo.image, eTransfer, eTransferRead, eColorAttachmentOutput,
                     eColorAttachmentWrite, vk::ImageLayout::eTransferSrcOptimal,
                     vk::ImageLayout::eColorAttachmentOptimal, 0, 1, 1);
    }

    void SkyEnvironmentCapture::recordPrefilterFace(const vk::CommandBuffer& cmd, uint32_t face, uint32_t mip)
    {
        const uint32_t size = PREFILTER_SIZE >> mip;
        const float roughness = static_cast<float>(mip) / static_cast<float>(PREFILTER_MIPS - 1);
        const glm::mat4 viewProj = ibl::CameraViewMatrix::captureProjection * ibl::CameraViewMatrix::captureViews[face];
        renderCubeFace(cmd, prefilterPipeline, prefilterPipelineLayout, envDS, cubeVertexBuffer,
                       static_cast<uint32_t>(ibl::cubeVertices.size()), helperHi.view, size, viewProj, &roughness);

        imageBarrier(cmd, helperHi.image, eColorAttachmentOutput, eColorAttachmentWrite,
                     eTransfer, eTransferRead, vk::ImageLayout::eColorAttachmentOptimal,
                     vk::ImageLayout::eTransferSrcOptimal, 0, 1, 1);
        copyToLayer(cmd, helperHi.image, stagingPrefilter.image, mip, face, size);
        imageBarrier(cmd, helperHi.image, eTransfer, eTransferRead, eColorAttachmentOutput,
                     eColorAttachmentWrite, vk::ImageLayout::eTransferSrcOptimal,
                     vk::ImageLayout::eColorAttachmentOptimal, 0, 1, 1);
    }

    void SkyEnvironmentCapture::recordItem(const vk::CommandBuffer& cmd, const WorkItem& item)
    {
        switch (item.phase)
        {
        case CapturePhase::Env:
            if (envLayout != vk::ImageLayout::eTransferDstOptimal)
            {
                imageBarrier(cmd, envCube.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                             envLayout, vk::ImageLayout::eTransferDstOptimal, 0, 1, 6);
                envLayout = vk::ImageLayout::eTransferDstOptimal;
            }
            recordEnvFace(cmd, item.face);
            if (item.face == 5u) // env phase (faces 0..5) complete -> make the cube sampleable
            {
                imageBarrier(cmd, envCube.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                             vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 0, 1, 6);
                envLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            }
            break;
        case CapturePhase::Irradiance:
            recordIrradianceFace(cmd, item.face);
            break;
        case CapturePhase::Prefilter:
            recordPrefilterFace(cmd, item.face, item.mip);
            break;
        }
    }

    void SkyEnvironmentCapture::publish(const vk::CommandBuffer& cmd)
    {
        using vk::ImageLayout;

        // Staging finished being written (copy dst) -> make readable as copy source.
        imageBarrier(cmd, stagingIrradiance.image, eTransfer, eTransferWrite, eTransfer, eTransferRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eTransferSrcOptimal, 0, 1, 6);
        imageBarrier(cmd, stagingPrefilter.image, eTransfer, eTransferWrite, eTransfer, eTransferRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eTransferSrcOptimal, 0, PREFILTER_MIPS, 6);

        // Live maps: wait for prior scene fragment reads (WAR) then take them as copy targets.
        imageBarrier(cmd, irradianceLive.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                     ImageLayout::eShaderReadOnlyOptimal, ImageLayout::eTransferDstOptimal, 0, 1, 6);
        imageBarrier(cmd, prefilterLive.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                     ImageLayout::eShaderReadOnlyOptimal, ImageLayout::eTransferDstOptimal, 0, PREFILTER_MIPS, 6);

        vk::ImageCopy irrRegion{};
        irrRegion.srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 6};
        irrRegion.dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 6};
        irrRegion.extent = vk::Extent3D{IRR_SIZE, IRR_SIZE, 1};
        cmd.copyImage(stagingIrradiance.image, ImageLayout::eTransferSrcOptimal, irradianceLive.image,
                      ImageLayout::eTransferDstOptimal, irrRegion);

        std::array<vk::ImageCopy, PREFILTER_MIPS> preRegions{};
        for (uint32_t mip = 0; mip < PREFILTER_MIPS; ++mip)
        {
            const uint32_t dim = PREFILTER_SIZE >> mip;
            preRegions[mip].srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, mip, 0, 6};
            preRegions[mip].dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, mip, 0, 6};
            preRegions[mip].extent = vk::Extent3D{dim, dim, 1};
        }
        cmd.copyImage(stagingPrefilter.image, ImageLayout::eTransferSrcOptimal, prefilterLive.image,
                      ImageLayout::eTransferDstOptimal, preRegions);

        // Live maps back to sampleable for the scene pass; staging back to writable for the next cycle.
        imageBarrier(cmd, irradianceLive.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eShaderReadOnlyOptimal, 0, 1, 6);
        imageBarrier(cmd, prefilterLive.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eShaderReadOnlyOptimal, 0, PREFILTER_MIPS, 6);
        imageBarrier(cmd, stagingIrradiance.image, eTransfer, eTransferRead, eTransfer, eTransferWrite,
                     ImageLayout::eTransferSrcOptimal, ImageLayout::eTransferDstOptimal, 0, 1, 6);
        imageBarrier(cmd, stagingPrefilter.image, eTransfer, eTransferRead, eTransfer, eTransferWrite,
                     ImageLayout::eTransferSrcOptimal, ImageLayout::eTransferDstOptimal, 0, PREFILTER_MIPS, 6);
    }

    void SkyEnvironmentCapture::recordCapture(const vk::CommandBuffer& cmd, uint32_t budget, uint64_t epoch)
    {
        if (!initialized)
            return;

        if (scheduler.cycleComplete() && scheduler.published)
        {
            if (epoch != scheduler.epoch)
                scheduler.restart(epoch); // sky changed -> start a fresh cycle
            else
                return; // sky unchanged -> nothing to do
        }

        // Never abort mid-cycle (guarantees forward progress even if the sky keeps changing).
        scheduler.advance(budget, [&](const WorkItem& item) { recordItem(cmd, item); });

        if (scheduler.shouldPublish())
        {
            publish(cmd);
            scheduler.markPublished();
        }
    }

    void SkyEnvironmentCapture::captureBlocking(uint64_t epoch, AtmospherePipeline& atmosphere)
    {
        if (!initialized)
            return;

        ambientIntensity = atmosphere.getSettings().ambientIntensity;

        scheduler.restart(epoch);
        auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool);

        // Compute the sky LUTs into this same submit before sampling them. This mirrors the
        // per-frame ordering exactly — the frame graph records the Atmosphere pass (dispatchCompute)
        // immediately before the SkyAmbientCapture pass — and dispatchCompute's trailing
        // compute->fragment barrier is what makes the sampled reads below safe.
        atmosphere.dispatchCompute(cmd.get());

        scheduler.advance(scheduler.totalItems(), [&](const WorkItem& item) { recordItem(cmd.get(), item); });
        publish(cmd.get());
        scheduler.markPublished();
        core::Utilities::endSingleTimeCommands(device, cmd);
    }

    void SkyEnvironmentCapture::cleanup()
    {
        if (!initialized && !skyShader)
            return;

        const vk::Device dev = device.getLogicalDevice();
        dev.waitIdle();

        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& l)
        {
            if (p) { dev.destroyPipeline(p); p = nullptr; }
            if (l) { dev.destroyPipelineLayout(l); l = nullptr; }
        };
        destroyPipeline(skyPipeline, skyPipelineLayout);
        destroyPipeline(irradiancePipeline, irradiancePipelineLayout);
        destroyPipeline(prefilterPipeline, prefilterPipelineLayout);

        if (skyDSLayout) { dev.destroyDescriptorSetLayout(skyDSLayout); skyDSLayout = nullptr; }
        if (envDSLayout) { dev.destroyDescriptorSetLayout(envDSLayout); envDSLayout = nullptr; }
        if (descriptorPool) { dev.destroyDescriptorPool(descriptorPool); descriptorPool = nullptr; }

        auto destroyCube = [&](ibl::ImageData& d)
        {
            if (d.sampler) { dev.destroySampler(d.sampler); d.sampler = nullptr; }
            if (d.imageView) { dev.destroyImageView(d.imageView); d.imageView = nullptr; }
            if (d.image) { dev.destroyImage(d.image); d.image = nullptr; }
            if (d.imageAllocation) { device.getMemoryManager().free(d.imageAllocation); d.imageAllocation = {}; }
        };
        destroyCube(envCube);
        destroyCube(stagingIrradiance);
        destroyCube(stagingPrefilter);
        destroyCube(irradianceLive);
        destroyCube(prefilterLive);

        auto destroyHelper = [&](ibl::OffScreenHelper& h)
        {
            if (h.view) { dev.destroyImageView(h.view); h.view = nullptr; }
            if (h.image) { dev.destroyImage(h.image); h.image = nullptr; }
            if (h.allocation) { device.getMemoryManager().free(h.allocation); h.allocation = {}; }
        };
        destroyHelper(helperHi);
        destroyHelper(helperLo);

        if (cubeVertexBuffer)
        {
            core::BufferUtilities::destroyBuffer(dev, cubeVertexBuffer, cubeVertexAllocation, device.getMemoryManager());
            cubeVertexBuffer = nullptr;
        }

        if (brdfGenerator)
        {
            brdfGenerator->cleanUp();
            brdfGenerator->cleanUpShader();
            brdfGenerator.reset();
        }
        brdfLUT = {};

        if (skyShader) { skyShader->cleanUp(); skyShader.reset(); }
        if (irradianceShader) { irradianceShader->cleanUp(); irradianceShader.reset(); }
        if (prefilterShader) { prefilterShader->cleanUp(); prefilterShader.reset(); }

        initialized = false;
    }
}
