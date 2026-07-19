#include "ReflectionProbeManager.hpp"
#include "../RenderTextureViewPort.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstring>

namespace render::probe
{
    using render::atmosphere::CapturePhase;
    using render::atmosphere::WorkItem;

    // Sync2 shorthands. Only the STAGE enum is `using enum`-ed — `using enum`-ing the access flags
    // as well collides on eNone/eNoneKHR and fails to compile on MSVC (C2874). Mirrors
    // SkyEnvironmentCapture.cpp / HdrEnvironmentCapture.cpp.
    using enum vk::PipelineStageFlagBits2;
    constexpr auto eColorAttachmentWrite = vk::AccessFlagBits2::eColorAttachmentWrite;
    constexpr auto eTransferRead = vk::AccessFlagBits2::eTransferRead;
    constexpr auto eTransferWrite = vk::AccessFlagBits2::eTransferWrite;
    constexpr auto eShaderRead = vk::AccessFlagBits2::eShaderRead;

    namespace
    {
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
            // maxLod is the trap here: vk::SamplerCreateInfo{} defaults it to 0.0, which clamps every
            // LOD to mip 0 and makes every reflection mirror-sharp regardless of roughness — with no
            // validation error and no other symptom. Same settings as the sky/HDR capture samplers.
            vk::SamplerCreateInfo s{};
            s.magFilter = vk::Filter::eLinear;
            s.minFilter = vk::Filter::eLinear;
            s.mipmapMode = vk::SamplerMipmapMode::eLinear; // trilinear across prefilter mips
            s.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            s.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            s.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            s.minLod = 0.0f;
            s.maxLod = VK_LOD_CLAMP_NONE; // consumers sample LOD 0..MAX_REFLECTION_LOD (= 4)
            s.anisotropyEnable = VK_FALSE;
            s.maxAnisotropy = 1.0f;
            s.borderColor = vk::BorderColor::eFloatOpaqueBlack;
            s.unnormalizedCoordinates = VK_FALSE;
            return dev.createSampler(s);
        }
    }

    ReflectionProbeManager::ReflectionProbeManager(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}, bufferManager{device}
    {
    }

    ReflectionProbeManager::~ReflectionProbeManager()
    {
        cleanup();
    }

    void ReflectionProbeManager::init(const vk::CommandPool& pool)
    {
        if (initialized)
            return;

        commandPool = pool;

        // Reused verbatim: sky_prefilter.glsl takes viewProj + roughness as push constants and
        // hardcodes `resolution = 128.0`, which is exactly the probe cube size. If PROBE_CUBE_SIZE
        // ever becomes configurable, that constant has to become a push constant too.
        prefilterShader = std::make_shared<core::Shader>(device);
        prefilterShader->readShader("../../resources/shaders/ibl/sky_prefilter.glsl");

        core::BufferInfoRequest vbReq(device.getLogicalDevice(), device.getPhysicalDevice());
        vbReq.size = sizeof(ibl::cubeVertices[0]) * ibl::cubeVertices.size();
        vbReq.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        vbReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(vbReq, cubeVertexBuffer, cubeVertexAllocation, device.getMemoryManager());
        if (cubeVertexAllocation.mappedPtr)
            std::memcpy(cubeVertexAllocation.mappedPtr, ibl::cubeVertices.data(), vbReq.size);

        auto initCmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool);
        createImages(initCmd.get());
        core::Utilities::endSingleTimeCommands(device, initCmd);

        createDescriptors();
        createPipelines();

        bufferManager.init();

        // Specular-only: no irradiance phase. 6 env + 6x5 prefilter = 36 items per probe.
        scheduler.plan.irrFaces = 0;
        scheduler.plan.prefilterMips = PREFILTER_MIPS;

        initialized = true;
    }

    void ReflectionProbeManager::createImages(const vk::CommandBuffer& initCmd)
    {
        const vk::Device dev = device.getLogicalDevice();
        auto& mem = device.getMemoryManager();

        auto makeCube = [&](ibl::ImageData& out, uint32_t mips, bool sampled)
        {
            core::ImageInfoRequest req(dev, device.getPhysicalDevice());
            req.format = HDR_FORMAT;
            req.layers = 6;
            req.mipLevels = mips;
            req.width = CUBE_SIZE;
            req.height = CUBE_SIZE;
            req.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc |
                (sampled ? vk::ImageUsageFlagBits::eSampled : vk::ImageUsageFlags{});
            // eCubeCompatible + 6 layers + 5 mips means each probe cube is byte-identical to one
            // slice of a future cube array — migrating later stays a descriptor change, not a rebake.
            req.imageFlags = vk::ImageCreateFlagBits::eCubeCompatible;
            core::ImageUtilities::createImage(req, out.image, out.imageAllocation, mem);

            if (sampled)
            {
                core::ImageViewInfoRequest viewReq(dev, out.image);
                viewReq.format = HDR_FORMAT;
                viewReq.layerCount = 6;
                // levelCount MUST be the full mip count. Leaving it at 1 is the other silent way to
                // get mirror-sharp reflections at every roughness.
                viewReq.mipLevels = mips;
                viewReq.imageType = vk::ImageViewType::eCube;
                core::ImageUtilities::createImageView(viewReq, out.imageView);
                out.sampler = makeSampler(dev);
            }
        };

        makeCube(envScratch, 1, true);
        makeCube(stagingPrefilter, PREFILTER_MIPS, false);

        // The 8 live cubes are allocated up front: they are the descriptor targets for set 0
        // binding 4, and allocating them lazily would mean rewriting the descriptor set mid-bake.
        // 8 x ~1.0 MiB is a fixed ~8 MiB once any probe exists in the project.
        for (auto& cube : probeCubes)
            makeCube(cube, PREFILTER_MIPS, true);

        {
            core::ImageInfoRequest req(dev, device.getPhysicalDevice());
            req.format = HDR_FORMAT;
            req.width = CUBE_SIZE;
            req.height = CUBE_SIZE;
            req.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
            core::ImageUtilities::createImage(req, helperColor.image, helperColor.allocation, mem);

            core::ImageViewInfoRequest viewReq(dev, helperColor.image);
            viewReq.format = HDR_FORMAT;
            core::ImageUtilities::createImageView(viewReq, helperColor.view);
        }

        using vk::ImageLayout;
        const auto color = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::transitionImageLayout(initCmd, envScratch.image, ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal, color, 6, 1);
        core::ImageUtilities::transitionImageLayout(initCmd, stagingPrefilter.image, ImageLayout::eUndefined,
            ImageLayout::eTransferDstOptimal, color, 6, PREFILTER_MIPS);
        for (auto& cube : probeCubes)
        {
            core::ImageUtilities::transitionImageLayout(initCmd, cube.image, ImageLayout::eUndefined,
                ImageLayout::eShaderReadOnlyOptimal, color, 6, PREFILTER_MIPS);
        }
        core::ImageUtilities::transitionImageLayout(initCmd, helperColor.image, ImageLayout::eUndefined,
            ImageLayout::eColorAttachmentOptimal, color, 1, 1);

        envScratchLayout = ImageLayout::eShaderReadOnlyOptimal;
    }

    void ReflectionProbeManager::createDescriptors()
    {
        const vk::Device dev = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 1};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = dev.createDescriptorPool(poolInfo);

        vk::DescriptorSetLayoutBinding envBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo envLayoutInfo{};
        envLayoutInfo.bindingCount = 1;
        envLayoutInfo.pBindings = &envBinding;
        envDSLayout = dev.createDescriptorSetLayout(envLayoutInfo);

        vk::DescriptorSetAllocateInfo envAlloc{descriptorPool, 1, &envDSLayout};
        envDS = dev.allocateDescriptorSets(envAlloc)[0];

        // Stable view: the env scratch cube is allocated once and never recreated, so this write
        // happens exactly once for the manager's lifetime.
        vk::DescriptorImageInfo envInfo{envScratch.sampler, envScratch.imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::WriteDescriptorSet envWrite{envDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &envInfo};
        dev.updateDescriptorSets(envWrite, nullptr);
    }

    void ReflectionProbeManager::createPipelines()
    {
        const vk::Device dev = device.getLogicalDevice();

        // ONE combined range covering both stages. VUID-01796 requires a vkCmdPushConstants call to
        // name every stage of any range it overlaps, so viewProj (vertex) and roughness (fragment)
        // must be pushed together as a single blob — splitting them is invalid.
        vk::PushConstantRange prefilterRange{
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
            sizeof(glm::mat4) + sizeof(float)};
        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &envDSLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &prefilterRange;
        prefilterPipelineLayout = dev.createPipelineLayout(layoutInfo);

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

        vk::GraphicsPipelineCreateInfo info{};
        info.stageCount = static_cast<uint32_t>(prefilterShader->getShaderStages().size());
        info.pStages = prefilterShader->getShaderStages().data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisampling;
        info.pColorBlendState = &colorBlending;
        info.pDynamicState = &dynamicState;
        info.layout = prefilterPipelineLayout;
        info.pNext = &renderingInfo;
        prefilterPipeline = dev.createGraphicsPipeline(nullptr, info).value;
    }

    void ReflectionProbeManager::ensureCaptureViewport()
    {
        if (captureViewport) return;

        captureViewport = std::make_unique<RenderTextureViewPort>(device, swapChain);
        // Raw HDR, no tonemap: a probe stores scene radiance, not display-referred colour. Tonemapping
        // here would bake the tonemap curve into every reflection and double-apply it on screen.
        captureViewport->setTonemap(false);
        captureViewport->init(CUBE_SIZE, CUBE_SIZE);
    }

    bool ReflectionProbeManager::selectNextDirtyProbe()
    {
        const auto& probes = bufferManager.getResolvedProbes();

        // Keep baking the current probe until its cycle finishes.
        if (activeSlot >= 0)
        {
            for (const auto& p : probes)
            {
                if (p.entity == activeEntity && p.slot == static_cast<uint32_t>(activeSlot))
                    return true;
            }
            // The probe vanished (deleted / deactivated / re-sorted) mid-bake — drop it and re-pick.
            activeSlot = -1;
            activeEntity = entt::null;
        }

        for (const auto& p : probes)
        {
            if (!p.dirty) continue;
            activeSlot = static_cast<int32_t>(p.slot);
            activeEntity = p.entity;
            scheduler.restart(scheduler.epoch + 1); // any restart value works; probes bake on demand
            bufferManager.setSlotReady(p.slot, false);
            return true;
        }

        return false;
    }

    void ReflectionProbeManager::tickSceneCapture(RenderPassHandler* passHandler)
    {
        if (!initialized || !passHandler) return;

        bufferManager.updateFromScene();
        bufferManager.uploadToGPU();

        if (!selectNextDirtyProbe()) return;
        if (scheduler.cycleComplete()) return; // waiting on the graph half to prefilter + publish

        const WorkItem item = scheduler.itemAt(scheduler.cursor);
        if (item.phase != CapturePhase::Env) return; // prefilter items belong to the graph hook

        const auto& probes = bufferManager.getResolvedProbes();
        const ResolvedProbe* active = nullptr;
        for (const auto& p : probes)
        {
            if (p.entity == activeEntity) { active = &p; break; }
        }
        if (!active) return;

        ensureCaptureViewport();
        captureSceneFace(passHandler, *active, item.face);

        // One face per frame: each face is its own scene render + submit, so a burst of six in one
        // frame would be a visible hitch. 8 probes converge in ~48 frames (<1s at 60fps).
        scheduler.advance(1, [](const WorkItem&) {});
    }

    void ReflectionProbeManager::captureSceneFace(RenderPassHandler* passHandler,
                                                  const ResolvedProbe& probe, uint32_t face)
    {
        // CUBE FACE ORIENTATION — the one thing here that cannot be settled by reading.
        //
        // Scene cameras negate proj[1][1] for the Vulkan Y-flip (SpotShadowCalculator.cpp:58), while
        // ibl::CameraViewMatrix::captureProjection does NOT, because captureViews already bakes the
        // compensating up = -Y into each face and the IBL shaders convert internally. We reuse
        // captureViews, so we deliberately use the UNFLIPPED projection to stay consistent with every
        // other cubemap this engine generates.
        //
        // If a test interior comes out vertically mirrored, the fix is exactly one of two one-line
        // changes, both HERE: negate proj[1][1] below, or switch copyToLayer to a Y-flipping
        // vkCmdBlitImage. Do not "fix" it downstream in the shader.
        const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, probe.nearPlane, probe.farPlane);
        const glm::mat4 view = ibl::CameraViewMatrix::captureViews[face] *
                               glm::translate(glm::mat4(1.0f), -probe.capturePos);

        captureViewport->setRenderShadows(probe.captureShadows);
        captureViewport->render(passHandler, view, proj, probe.capturePos, probe.nearPlane, probe.farPlane);

        const vk::Image srcImage = captureViewport->getLastRenderedImage();
        if (!srcImage) return;

        // Copy the rendered face into the env scratch cube. This runs as its own single-time submit
        // on the same queue as the RTT render, so submission order plus the barriers below give the
        // required dependency; endSingleTimeCommands then waits for it. That CPU wait is why this is
        // one face per frame and only while a probe is actually baking.
        auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool);

        if (envScratchLayout != vk::ImageLayout::eTransferDstOptimal)
        {
            imageBarrier(cmd.get(), envScratch.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                         envScratchLayout, vk::ImageLayout::eTransferDstOptimal, 0, 1, 6);
            envScratchLayout = vk::ImageLayout::eTransferDstOptimal;
        }

        // RenderTextureViewPort leaves its colour image in eShaderReadOnlyOptimal.
        imageBarrier(cmd.get(), srcImage, eColorAttachmentOutput, eColorAttachmentWrite,
                     eTransfer, eTransferRead, vk::ImageLayout::eShaderReadOnlyOptimal,
                     vk::ImageLayout::eTransferSrcOptimal, 0, 1, 1);

        copyToLayer(cmd.get(), srcImage, envScratch.image, 0, face, CUBE_SIZE);

        // Hand the RTT image back exactly as we found it — it is reused for the next face and by any
        // other render-texture consumer.
        imageBarrier(cmd.get(), srcImage, eTransfer, eTransferRead, eFragmentShader, eShaderRead,
                     vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 0, 1, 1);

        if (face == 5u)
        {
            // All six faces written -> make the scratch cube sampleable for the prefilter pass.
            imageBarrier(cmd.get(), envScratch.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                         vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 0, 1, 6);
            envScratchLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        core::Utilities::endSingleTimeCommands(device, cmd);
    }

    void ReflectionProbeManager::recordPrefilterFace(const vk::CommandBuffer& cmd, uint32_t face, uint32_t mip)
    {
        const uint32_t size = CUBE_SIZE >> mip;
        // Must match every consumer: roughness = m/(mips-1) = m/4, sampled as
        // textureLod(cube, R, roughness * MAX_REFLECTION_LOD) with MAX_REFLECTION_LOD == 4.0.
        const float roughness = static_cast<float>(mip) / static_cast<float>(PREFILTER_MIPS - 1);
        const glm::mat4 viewProj = ibl::CameraViewMatrix::captureProjection *
                                   ibl::CameraViewMatrix::captureViews[face];

        vk::Viewport vp{};
        vp.width = static_cast<float>(size);
        vp.height = static_cast<float>(size);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vk::Rect2D scissor{vk::Offset2D{0, 0}, vk::Extent2D{size, size}};

        core::DynamicRenderingInfo info{};
        info.extent = vk::Extent2D{size, size};
        info.colorAttachments = {
            core::colorClear(helperColor.view, vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}})
        };

        core::beginDynamicRendering(cmd, info);
        cmd.setViewport(0, 1, &vp);
        cmd.setScissor(0, 1, &scissor);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, prefilterPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, prefilterPipelineLayout, 0, envDS, {});

        struct PushBlob { glm::mat4 viewProj; float roughness; } blob{viewProj, roughness};
        cmd.pushConstants(prefilterPipelineLayout,
                          vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(glm::mat4) + sizeof(float), &blob);

        vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, cubeVertexBuffer, offset);
        cmd.draw(static_cast<uint32_t>(ibl::cubeVertices.size()), 1, 0, 0);
        core::endDynamicRendering(cmd);

        imageBarrier(cmd, helperColor.image, eColorAttachmentOutput, eColorAttachmentWrite,
                     eTransfer, eTransferRead, vk::ImageLayout::eColorAttachmentOptimal,
                     vk::ImageLayout::eTransferSrcOptimal, 0, 1, 1);
        copyToLayer(cmd, helperColor.image, stagingPrefilter.image, mip, face, size);
        imageBarrier(cmd, helperColor.image, eTransfer, eTransferRead, eColorAttachmentOutput,
                     eColorAttachmentWrite, vk::ImageLayout::eTransferSrcOptimal,
                     vk::ImageLayout::eColorAttachmentOptimal, 0, 1, 1);
    }

    void ReflectionProbeManager::publishToSlot(const vk::CommandBuffer& cmd, uint32_t slot)
    {
        using vk::ImageLayout;
        ibl::ImageData& live = probeCubes[slot];

        imageBarrier(cmd, stagingPrefilter.image, eTransfer, eTransferWrite, eTransfer, eTransferRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eTransferSrcOptimal, 0, PREFILTER_MIPS, 6);
        // WAR: the scene fragment stage may still be sampling this slot from a previous frame.
        imageBarrier(cmd, live.image, eFragmentShader, eShaderRead, eTransfer, eTransferWrite,
                     ImageLayout::eShaderReadOnlyOptimal, ImageLayout::eTransferDstOptimal, 0, PREFILTER_MIPS, 6);

        std::array<vk::ImageCopy, PREFILTER_MIPS> regions{};
        for (uint32_t mip = 0; mip < PREFILTER_MIPS; ++mip)
        {
            const uint32_t dim = CUBE_SIZE >> mip;
            regions[mip].srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, mip, 0, 6};
            regions[mip].dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, mip, 0, 6};
            regions[mip].extent = vk::Extent3D{dim, dim, 1};
        }
        cmd.copyImage(stagingPrefilter.image, ImageLayout::eTransferSrcOptimal, live.image,
                      ImageLayout::eTransferDstOptimal, regions);

        imageBarrier(cmd, live.image, eTransfer, eTransferWrite, eFragmentShader, eShaderRead,
                     ImageLayout::eTransferDstOptimal, ImageLayout::eShaderReadOnlyOptimal, 0, PREFILTER_MIPS, 6);
        imageBarrier(cmd, stagingPrefilter.image, eTransfer, eTransferRead, eTransfer, eTransferWrite,
                     ImageLayout::eTransferSrcOptimal, ImageLayout::eTransferDstOptimal, 0, PREFILTER_MIPS, 6);
    }

    void ReflectionProbeManager::recordGraphWork(const vk::CommandBuffer& cmd, uint32_t budget)
    {
        if (!initialized || activeSlot < 0) return;
        if (scheduler.cycleComplete()) return;

        // Only prefilter items belong here; the env faces are produced by tickSceneCapture.
        if (scheduler.itemAt(scheduler.cursor).phase != CapturePhase::Prefilter) return;

        scheduler.advance(budget, [&](const WorkItem& item)
        {
            if (item.phase == CapturePhase::Prefilter)
                recordPrefilterFace(cmd, item.face, item.mip);
        });

        if (scheduler.shouldPublish())
        {
            const auto slot = static_cast<uint32_t>(activeSlot);
            publishToSlot(cmd, slot);
            scheduler.markPublished();

            bufferManager.setSlotReady(slot, true);
            anySlotReady = true;

            // Clear the component's dirty flag so this probe is not re-picked next frame.
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.valid(activeEntity) && registry.all_of<components::ReflectionProbeComponent>(activeEntity))
            {
                registry.get<components::ReflectionProbeComponent>(activeEntity).dirty = false;
            }

            activeSlot = -1;
            activeEntity = entt::null;
        }
    }

    void ReflectionProbeManager::cleanup()
    {
        if (!initialized) return;

        const vk::Device dev = device.getLogicalDevice();
        auto& mem = device.getMemoryManager();

        if (captureViewport)
        {
            captureViewport->cleanUp();
            captureViewport.reset();
        }

        if (prefilterPipeline) dev.destroyPipeline(prefilterPipeline);
        if (prefilterPipelineLayout) dev.destroyPipelineLayout(prefilterPipelineLayout);
        if (envDSLayout) dev.destroyDescriptorSetLayout(envDSLayout);
        if (descriptorPool) dev.destroyDescriptorPool(descriptorPool);
        prefilterPipeline = nullptr;
        prefilterPipelineLayout = nullptr;
        envDSLayout = nullptr;
        descriptorPool = nullptr;
        envDS = nullptr;

        auto destroyImage = [&](ibl::ImageData& img)
        {
            if (img.sampler) dev.destroySampler(img.sampler);
            if (img.imageView) dev.destroyImageView(img.imageView);
            if (img.image)
            {
                dev.destroyImage(img.image);
                mem.free(img.imageAllocation);
            }
            img = {};
        };

        destroyImage(envScratch);
        destroyImage(stagingPrefilter);
        for (auto& cube : probeCubes)
            destroyImage(cube);

        if (helperColor.view) dev.destroyImageView(helperColor.view);
        if (helperColor.image)
        {
            dev.destroyImage(helperColor.image);
            mem.free(helperColor.allocation);
        }
        helperColor = {};

        core::BufferUtilities::destroyBuffer(dev, cubeVertexBuffer, cubeVertexAllocation, mem);

        if (prefilterShader)
        {
            prefilterShader->cleanUp();
            prefilterShader.reset();
        }

        bufferManager.cleanup();

        activeSlot = -1;
        activeEntity = entt::null;
        anySlotReady = false;
        initialized = false;
    }
}
