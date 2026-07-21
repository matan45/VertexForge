#include "HdrEnvironmentCapture.hpp"
#include "BRDFLUTGenerator.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "asset/AssetRef.hpp"
#include "print/Log.hpp"

#include <array>
#include <chrono>
#include <cstring>

namespace render::ibl
{
    using render::atmosphere::CapturePhase;
    using render::atmosphere::WorkItem;

    // Sync2 stage/access shorthands (mirrors SkyEnvironmentCapture.cpp). Only the STAGE enum is
    // `using enum`-ed; the access flags are aliased individually to avoid the eNone/eNoneKHR
    // redeclaration collision that `using enum`-ing both stage + access flags triggers (MSVC C2874).
    using enum vk::PipelineStageFlagBits2;
    constexpr auto eColorAttachmentWrite = vk::AccessFlagBits2::eColorAttachmentWrite;
    constexpr auto eTransferRead = vk::AccessFlagBits2::eTransferRead;
    constexpr auto eTransferWrite = vk::AccessFlagBits2::eTransferWrite;
    constexpr auto eShaderRead = vk::AccessFlagBits2::eShaderRead;

    namespace
    {
        // FNV-1a over the resolved path — stable, so a given HDR is captured once and a new path
        // restarts the cycle (matching AmbientCaptureScheduler's epoch contract).
        uint64_t fnv1a(const std::string& s)
        {
            uint64_t h = 1469598103934665603ull;
            for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
            return h;
        }

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
                            const glm::mat4& viewProj, const float* roughness)
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
            if (roughness)
            {
                // Prefilter layout has ONE combined range {vertex|fragment, 0, 68}; VUID-01796 needs
                // the push to name ALL stages of an overlapping range, so viewProj (vertex) and
                // roughness (fragment) push together as one blob.
                struct PushBlob { glm::mat4 viewProj; float roughness; } blob{viewProj, *roughness};
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
            s.maxLod = VK_LOD_CLAMP_NONE; // prefilter consumers sample LOD 0..MAX_REFLECTION_LOD(=4)
            s.anisotropyEnable = VK_FALSE;
            s.maxAnisotropy = 1.0f;
            s.borderColor = vk::BorderColor::eFloatOpaqueBlack;
            s.unnormalizedCoordinates = VK_FALSE;
            return dev.createSampler(s);
        }
    }

    HdrEnvironmentCapture::HdrEnvironmentCapture(core::Device& device)
        : device{device}
    {
    }

    HdrEnvironmentCapture::~HdrEnvironmentCapture()
    {
        cleanup();
    }

    void HdrEnvironmentCapture::init(const vk::CommandPool& pool)
    {
        if (initialized)
            return;

        commandPool = pool;

        envShader = std::make_shared<core::Shader>(device);
        envShader->readShader("../../resources/shaders/ibl/equirect_to_cubemap_pushconst.glsl");
        irradianceShader = std::make_shared<core::Shader>(device);
        irradianceShader->readShader("../../resources/shaders/ibl/cubemap_irradiance_convolution.glsl");
        prefilterShader = std::make_shared<core::Shader>(device);
        prefilterShader->readShader("../../resources/shaders/ibl/sky_prefilter.glsl");

        core::BufferInfoRequest vbReq(device.getLogicalDevice(), device.getPhysicalDevice());
        vbReq.size = sizeof(ibl::cubeVertices[0]) * ibl::cubeVertices.size();
        vbReq.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        vbReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(vbReq, cubeVertexBuffer, cubeVertexAllocation, device.getMemoryManager());
        if (cubeVertexAllocation.mappedPtr)
            std::memcpy(cubeVertexAllocation.mappedPtr, ibl::cubeVertices.data(), vbReq.size);

        // Static BRDF LUT (view-independent), owned here so the mesh IBL descriptor is fully served.
        brdfGenerator = std::make_unique<BRDFLUTGenerator>(device);
        brdfGenerator->generate(commandPool);
        brdfLUT = brdfGenerator->getImageData();

        auto initCmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool);
        createImages(initCmd.get());
        core::Utilities::endSingleTimeCommands(device, initCmd);

        createDescriptors();
        createPipelines();

        // Scheduler baked at full fidelity: env(6) + irradiance(6) + prefilter(6 x 10) = 72 items.
        scheduler.plan.prefilterMips = PREFILTER_MIPS;

        initialized = true;
    }

    void HdrEnvironmentCapture::createImages(const vk::CommandBuffer& initCmd)
    {
        const vk::Device dev = device.getLogicalDevice();
        auto& mem = device.getMemoryManager();

        auto makeCube = [&](ImageData& out, uint32_t size, uint32_t mips, bool sampled)
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

        auto makeHelper = [&](OffScreenHelper& out, uint32_t size)
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

        makeCube(envStaging, ENV_SIZE, 1, true);
        makeCube(stagingIrradiance, IRR_SIZE, 1, false);
        makeCube(stagingPrefilter, PREFILTER_SIZE, PREFILTER_MIPS, false);
        makeCube(envLive, ENV_SIZE, 1, true);
        makeCube(irradianceLive, IRR_SIZE, 1, true);
        makeCube(prefilterLive, PREFILTER_SIZE, PREFILTER_MIPS, true);
        makeHelper(helperHi, ENV_SIZE); // env(1024) + prefilter(<=512) render target
        makeHelper(helperLo, IRR_SIZE);

        using vk::ImageLayout;
        const auto color = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::transitionImageLayout(initCmd, envStaging.image, ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal, color, 6, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, stagingIrradiance.image, ImageLayout::eUndefined,
            ImageLayout::eTransferDstOptimal, color, 6, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, stagingPrefilter.image, ImageLayout::eUndefined,
            ImageLayout::eTransferDstOptimal, color, 6, PREFILTER_MIPS);
        core::ImageUtilities::transitionImageLayout(initCmd, envLive.image, ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal, color, 6, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, irradianceLive.image, ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal, color, 6, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, prefilterLive.image, ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal, color, 6, PREFILTER_MIPS);
        core::ImageUtilities::transitionImageLayout(initCmd, helperHi.image, ImageLayout::eUndefined,
            ImageLayout::eColorAttachmentOptimal, color, 1, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, helperLo.image, ImageLayout::eUndefined,
            ImageLayout::eColorAttachmentOptimal, color, 1, 1);

        envStagingLayout = ImageLayout::eShaderReadOnlyOptimal;
    }

    void HdrEnvironmentCapture::createDescriptors()
    {
        const vk::Device dev = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 2}; // srcDS (HDR) + envDS (cube)
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = dev.createDescriptorPool(poolInfo);

        // Source set: b0 equirectangular HDR (sampler2D). Written on the first upload (promoteSource).
        vk::DescriptorSetLayoutBinding srcBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo srcLayoutInfo{};
        srcLayoutInfo.bindingCount = 1;
        srcLayoutInfo.pBindings = &srcBinding;
        srcDSLayout = dev.createDescriptorSetLayout(srcLayoutInfo);

        // Env-sample set (irradiance + prefilter): b0 env cubemap (the staging cube).
        vk::DescriptorSetLayoutBinding envBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo envLayoutInfo{};
        envLayoutInfo.bindingCount = 1;
        envLayoutInfo.pBindings = &envBinding;
        envDSLayout = dev.createDescriptorSetLayout(envLayoutInfo);

        vk::DescriptorSetAllocateInfo srcAlloc{descriptorPool, 1, &srcDSLayout};
        srcDS = dev.allocateDescriptorSets(srcAlloc)[0];
        vk::DescriptorSetAllocateInfo envAlloc{descriptorPool, 1, &envDSLayout};
        envDS = dev.allocateDescriptorSets(envAlloc)[0];

        // envDS binds the staging env cube (stable view). srcDS is written when a source uploads.
        vk::DescriptorImageInfo envInfo{envStaging.sampler, envStaging.imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::WriteDescriptorSet envWrite{envDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &envInfo};
        dev.updateDescriptorSets(envWrite, nullptr);
    }

    void HdrEnvironmentCapture::createPipelines()
    {
        const vk::Device dev = device.getLogicalDevice();

        vk::PushConstantRange vpRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)};
        vk::PipelineLayoutCreateInfo envLayoutInfo{};
        envLayoutInfo.setLayoutCount = 1;
        envLayoutInfo.pSetLayouts = &srcDSLayout;
        envLayoutInfo.pushConstantRangeCount = 1;
        envLayoutInfo.pPushConstantRanges = &vpRange;
        envPipelineLayout = dev.createPipelineLayout(envLayoutInfo);

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

        envPipeline = build(envShader, envPipelineLayout);
        irradiancePipeline = build(irradianceShader, irradiancePipelineLayout);
        prefilterPipeline = build(prefilterShader, prefilterPipelineLayout);
    }

    void HdrEnvironmentCapture::recordEnvFace(const vk::CommandBuffer& cmd, uint32_t face)
    {
        const glm::mat4 viewProj = ibl::CameraViewMatrix::captureProjection * ibl::CameraViewMatrix::captureViews[face];
        renderCubeFace(cmd, envPipeline, envPipelineLayout, srcDS, cubeVertexBuffer,
                       static_cast<uint32_t>(ibl::cubeVertices.size()), helperHi.view, ENV_SIZE, viewProj, nullptr);

        imageBarrier(cmd, helperHi.image, eColorAttachmentOutput, eColorAttachmentWrite,
                     eTransfer, eTransferRead, vk::ImageLayout::eColorAttachmentOptimal,
                     vk::ImageLayout::eTransferSrcOptimal, 0, 1, 1);
        copyToLayer(cmd, helperHi.image, envStaging.image, 0, face, ENV_SIZE);
        imageBarrier(cmd, helperHi.image, eTransfer, eTransferRead, eColorAttachmentOutput,
                     eColorAttachmentWrite, vk::ImageLayout::eTransferSrcOptimal,
                     vk::ImageLayout::eColorAttachmentOptimal, 0, 1, 1);
    }

    void HdrEnvironmentCapture::recordIrradianceFace(const vk::CommandBuffer& cmd, uint32_t face)
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

    void HdrEnvironmentCapture::recordPrefilterFace(const vk::CommandBuffer& cmd, uint32_t face, uint32_t mip)
    {
        const uint32_t size = PREFILTER_SIZE >> mip;
        const float roughness = static_cast<float>(mip) / static_cast<float>(PREFILTER_MIPS - 1); // m/9
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

    void HdrEnvironmentCapture::recordItem(const vk::CommandBuffer& cmd, const WorkItem& item)
    {
        switch (item.phase)
        {
        case CapturePhase::Env:
            if (envStagingLayout != vk::ImageLayout::eTransferDstOptimal)
            {
                imageBarrier(cmd, envStaging.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                             envStagingLayout, vk::ImageLayout::eTransferDstOptimal, 0, 1, 6);
                envStagingLayout = vk::ImageLayout::eTransferDstOptimal;
            }
            recordEnvFace(cmd, item.face);
            if (item.face == 5u) // env phase complete -> make the staging cube sampleable
            {
                imageBarrier(cmd, envStaging.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                             vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 0, 1, 6);
                envStagingLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
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

    void HdrEnvironmentCapture::publish(const vk::CommandBuffer& cmd)
    {
        using vk::ImageLayout;

        // Staging finished being written -> readable as copy source.
        imageBarrier(cmd, envStaging.image, eFragmentShader, eShaderRead, eTransfer, eTransferRead,
                     ImageLayout::eShaderReadOnlyOptimal, ImageLayout::eTransferSrcOptimal, 0, 1, 6);
        imageBarrier(cmd, stagingIrradiance.image, eTransfer, eTransferWrite, eTransfer, eTransferRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eTransferSrcOptimal, 0, 1, 6);
        imageBarrier(cmd, stagingPrefilter.image, eTransfer, eTransferWrite, eTransfer, eTransferRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eTransferSrcOptimal, 0, PREFILTER_MIPS, 6);

        // Live maps: wait for prior scene fragment reads (WAR — skybox samples env, mesh samples
        // irradiance/prefilter) then take them as copy targets.
        imageBarrier(cmd, envLive.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                     ImageLayout::eShaderReadOnlyOptimal, ImageLayout::eTransferDstOptimal, 0, 1, 6);
        imageBarrier(cmd, irradianceLive.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                     ImageLayout::eShaderReadOnlyOptimal, ImageLayout::eTransferDstOptimal, 0, 1, 6);
        imageBarrier(cmd, prefilterLive.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                     ImageLayout::eShaderReadOnlyOptimal, ImageLayout::eTransferDstOptimal, 0, PREFILTER_MIPS, 6);

        vk::ImageCopy envRegion{};
        envRegion.srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 6};
        envRegion.dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 6};
        envRegion.extent = vk::Extent3D{ENV_SIZE, ENV_SIZE, 1};
        cmd.copyImage(envStaging.image, ImageLayout::eTransferSrcOptimal, envLive.image,
                      ImageLayout::eTransferDstOptimal, envRegion);

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
        imageBarrier(cmd, envLive.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eShaderReadOnlyOptimal, 0, 1, 6);
        imageBarrier(cmd, irradianceLive.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eShaderReadOnlyOptimal, 0, 1, 6);
        imageBarrier(cmd, prefilterLive.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eShaderReadOnlyOptimal, 0, PREFILTER_MIPS, 6);

        // envStaging returns to sampleable (irradiance/prefilter sample it next cycle; the env phase
        // will re-transition it to transferDst). Irradiance/prefilter staging return to writable.
        imageBarrier(cmd, envStaging.image, eTransfer, eTransferRead, eFragmentShader, eShaderRead,
                     ImageLayout::eTransferSrcOptimal, ImageLayout::eShaderReadOnlyOptimal, 0, 1, 6);
        imageBarrier(cmd, stagingIrradiance.image, eTransfer, eTransferRead, eTransfer, eTransferWrite,
                     ImageLayout::eTransferSrcOptimal, ImageLayout::eTransferDstOptimal, 0, 1, 6);
        imageBarrier(cmd, stagingPrefilter.image, eTransfer, eTransferRead, eTransfer, eTransferWrite,
                     ImageLayout::eTransferSrcOptimal, ImageLayout::eTransferDstOptimal, 0, PREFILTER_MIPS, 6);
    }

    void HdrEnvironmentCapture::recordCapture(const vk::CommandBuffer& cmd, uint32_t budget, uint64_t epoch)
    {
        if (!initialized || !hasSrc)
            return;

        if (scheduler.cycleComplete() && scheduler.published)
        {
            if (epoch != scheduler.epoch)
                scheduler.restart(epoch); // new HDR resident -> start a fresh cycle
            else
                return; // unchanged -> nothing to do (captured once)
        }

        // Never abort mid-cycle (forward progress guaranteed).
        scheduler.advance(budget, [&](const WorkItem& item) { recordItem(cmd, item); });

        if (scheduler.shouldPublish())
        {
            publish(cmd);
            scheduler.markPublished();
        }
    }

    void HdrEnvironmentCapture::captureBlocking(uint64_t epoch)
    {
        (void)epoch; // the resident source's path hash (sourceEpoch) is authoritative here.
        if (!initialized)
            return;

        ensureSourceResident();
        if (!hasSrc)
            return;

        scheduler.restart(sourceEpoch);
        auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool);
        scheduler.advance(scheduler.totalItems(), [&](const WorkItem& item) { recordItem(cmd.get(), item); });
        publish(cmd.get());
        scheduler.markPublished();
        core::Utilities::endSingleTimeCommands(device, cmd);
    }

    void HdrEnvironmentCapture::setSource(const std::string& hdrPath)
    {
        const uint64_t epoch = fnv1a(hdrPath);
        if (hasSrc && epoch == sourceEpoch)
            return; // already the active source
        if (pendingFuture.valid() && epoch == pendingEpoch)
            return; // already loading this source

        pendingEpoch = epoch;
        pendingFuture = resource::ResourceManager::loadHDRAsync(asset::AssetRef::fromPath(hdrPath));
    }

    void HdrEnvironmentCapture::pollSource()
    {
        if (!pendingFuture.valid())
            return;
        if (pendingFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        // Only swap between cycles so the source descriptor is never re-pointed while the env phase
        // is (or could be) recording — the old live maps stay bound until the current cycle finishes.
        if (hasSrc && !(scheduler.cycleComplete() && scheduler.published))
            return;

        auto hdr = pendingFuture.get();
        pendingFuture = {};
        promoteSource(hdr);
    }

    void HdrEnvironmentCapture::ensureSourceResident()
    {
        if (!pendingFuture.valid())
            return;
        auto hdr = pendingFuture.get(); // blocks (first bind / mode toggle only)
        pendingFuture = {};
        promoteSource(hdr);
    }

    void HdrEnvironmentCapture::promoteSource(const std::shared_ptr<resource::HDRData>& hdr)
    {
        if (!hdr || hdr->mipData.empty())
        {
            vfLogError("HdrEnvironmentCapture: HDR decode produced no data; keeping previous environment");
            return;
        }

        auto newTex = std::make_shared<core::Texture>(device);
        newTex->loadHDRFromData(*hdr, false);

        if (hdrTexture)
        {
            if (deletionQueue)
                hdrTexture->extractResources(*deletionQueue);
            else
                retiredSources.push_back(hdrTexture); // freed at cleanup (Texture dtor waits idle)
        }
        hdrTexture = newTex;

        vk::DescriptorImageInfo info{hdrTexture->getSampler(), hdrTexture->getImageView(),
            vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::WriteDescriptorSet write{srcDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &info};
        device.getLogicalDevice().updateDescriptorSets(write, nullptr);

        hasSrc = true;
        sourceEpoch = pendingEpoch;
    }

    void HdrEnvironmentCapture::cleanup()
    {
        if (!initialized && !envShader)
            return;

        const vk::Device dev = device.getLogicalDevice();
        dev.waitIdle();

        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& l)
        {
            if (p) { dev.destroyPipeline(p); p = nullptr; }
            if (l) { dev.destroyPipelineLayout(l); l = nullptr; }
        };
        destroyPipeline(envPipeline, envPipelineLayout);
        destroyPipeline(irradiancePipeline, irradiancePipelineLayout);
        destroyPipeline(prefilterPipeline, prefilterPipelineLayout);

        if (srcDSLayout) { dev.destroyDescriptorSetLayout(srcDSLayout); srcDSLayout = nullptr; }
        if (envDSLayout) { dev.destroyDescriptorSetLayout(envDSLayout); envDSLayout = nullptr; }
        if (descriptorPool) { dev.destroyDescriptorPool(descriptorPool); descriptorPool = nullptr; }

        auto destroyCube = [&](ImageData& d)
        {
            if (d.sampler) { dev.destroySampler(d.sampler); d.sampler = nullptr; }
            if (d.imageView) { dev.destroyImageView(d.imageView); d.imageView = nullptr; }
            if (d.image) { dev.destroyImage(d.image); d.image = nullptr; }
            if (d.imageAllocation) { device.getMemoryManager().free(d.imageAllocation); d.imageAllocation = {}; }
        };
        destroyCube(envStaging);
        destroyCube(stagingIrradiance);
        destroyCube(stagingPrefilter);
        destroyCube(envLive);
        destroyCube(irradianceLive);
        destroyCube(prefilterLive);

        auto destroyHelper = [&](OffScreenHelper& h)
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

        if (envShader) { envShader->cleanUp(); envShader.reset(); }
        if (irradianceShader) { irradianceShader->cleanUp(); irradianceShader.reset(); }
        if (prefilterShader) { prefilterShader->cleanUp(); prefilterShader.reset(); }

        // The device is idle; drop source textures (dtor destroys their Vulkan handles).
        hdrTexture.reset();
        retiredSources.clear();
        hasSrc = false;
        pendingEpoch = 0;
        sourceEpoch = 0;

        initialized = false;
    }
}
