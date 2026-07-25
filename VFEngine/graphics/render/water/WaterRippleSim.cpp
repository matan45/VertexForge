#include "WaterRippleSim.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Utilities.hpp"
#include "print/Log.hpp"

#include <algorithm>

// Windows defines MemoryBarrier as a macro, which conflicts with vk::MemoryBarrier.
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::water
{
    namespace
    {
        constexpr uint32_t RIPPLE_RES = ::water::RIPPLE_RESOLUTION;
        constexpr uint32_t RIPPLE_GROUP = 16;
        constexpr auto RIPPLE_FORMAT = vk::Format::eR16G16B16A16Sfloat;
        constexpr vk::DeviceSize IMPULSE_BYTES =
            static_cast<vk::DeviceSize>(::water::MAX_WATER_IMPULSES) * sizeof(GPUWaterImpulse);

        // The stages that sample the ripple OUTPUT. The vertex one is the reason these barriers are
        // hand-rolled instead of going through ImageUtilities::transitionImageLayout, whose
        // eGeneral -> eShaderReadOnlyOptimal case names eFragmentShader only. Naming a later stage
        // does not make an earlier one wait, so the vertex fetch of the water draw could race the
        // compute write.
        constexpr auto RIPPLE_READ_STAGES = vk::PipelineStageFlagBits::eVertexShader |
                                            vk::PipelineStageFlagBits::eFragmentShader;

        vk::ImageSubresourceRange colorRange()
        {
            return vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        }
    }

    WaterRippleSim::WaterRippleSim(core::Device& device)
        : device(device)
    {
    }

    WaterRippleSim::~WaterRippleSim()
    {
        cleanup();
    }

    void WaterRippleSim::init()
    {
        if (initialized)
            return;

        createImages();
        createSampler();
        createImpulseBuffer();
        createDescriptors();
        createPipeline();
        transitionImagesInitial();

        initialized = true;
    }

    void WaterRippleSim::cleanup()
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();

        if (pipeline) { vkDevice.destroyPipeline(pipeline); pipeline = nullptr; }
        if (pipelineLayout) { vkDevice.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }
        shader.reset();

        if (descriptorPool) { vkDevice.destroyDescriptorPool(descriptorPool); descriptorPool = nullptr; }
        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }
        descriptorSets = {};

        if (impulseBuffer)
        {
            core::BufferUtilities::destroyBuffer(vkDevice, impulseBuffer, impulseAllocation,
                                                 device.getMemoryManager());
            impulseBuffer = nullptr;
            impulseAllocation = {};
        }

        if (sampler) { vkDevice.destroySampler(sampler); sampler = nullptr; }
        if (outputView) { vkDevice.destroyImageView(outputView); outputView = nullptr; }
        if (outputImage) { vkDevice.destroyImage(outputImage); outputImage = nullptr; }
        if (outputAllocation) { device.getMemoryManager().free(outputAllocation); outputAllocation = {}; }

        for (uint32_t i = 0; i < 2; ++i)
        {
            if (stateViews[i]) { vkDevice.destroyImageView(stateViews[i]); stateViews[i] = nullptr; }
            if (stateImages[i]) { vkDevice.destroyImage(stateImages[i]); stateImages[i] = nullptr; }
            if (stateAllocations[i])
            {
                device.getMemoryManager().free(stateAllocations[i]);
                stateAllocations[i] = {};
            }
        }

        pendingImpulses.clear();
        lastDispatchTime = -1.0f;
        stepAccumulator = 0.0f;
        parity = 0;
        needsReset = true;
        simulated = false;
        initialized = false;
    }

    void WaterRippleSim::createImages()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // State ping-pong. eTransferDst is only for the one-time clear at init.
        for (uint32_t i = 0; i < 2; ++i)
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                RIPPLE_RES, RIPPLE_RES, 1, 1,
                RIPPLE_FORMAT,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(req, stateImages[i], stateAllocations[i],
                                              device.getMemoryManager());

            core::ImageViewInfoRequest viewReq(vkDevice, stateImages[i],
                RIPPLE_FORMAT, vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
            core::ImageUtilities::createImageView(viewReq, stateViews[i]);
        }

        // Output: written as a storage image by the sim, sampled by the water vertex and fragment
        // stages. This is the only image that reaches set 9.
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                RIPPLE_RES, RIPPLE_RES, 1, 1,
                RIPPLE_FORMAT,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(req, outputImage, outputAllocation,
                                              device.getMemoryManager());

            core::ImageViewInfoRequest viewReq(vkDevice, outputImage,
                RIPPLE_FORMAT, vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
            core::ImageUtilities::createImageView(viewReq, outputView);
        }
    }

    void WaterRippleSim::createSampler()
    {
        // Clamp-to-edge, not repeat: this patch is a world window, not a tiling period. The window
        // fade in water_ripple.glsl is what actually neutralises the clamped border values.
        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        info.anisotropyEnable = VK_FALSE;
        info.maxAnisotropy = 1.0f;
        info.mipmapMode = vk::SamplerMipmapMode::eNearest;

        sampler = device.getLogicalDevice().createSampler(info);
    }

    void WaterRippleSim::createImpulseBuffer()
    {
        core::BufferInfoRequest req(device.getLogicalDevice(), device.getPhysicalDevice());
        req.size = IMPULSE_BYTES;
        // Device-local + TransferDst because the contents arrive through vkCmdUpdateBuffer rather
        // than a mapped pointer. That embeds the data in the command buffer itself, so there is no
        // frames-in-flight aliasing hazard and no need for per-frame copies or dynamic offsets.
        req.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(req, impulseBuffer, impulseAllocation,
                                            device.getMemoryManager());
    }

    void WaterRippleSim::createDescriptors()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
        for (uint32_t i = 0; i < 3; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = vk::DescriptorType::eStorageImage;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
        }
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageImage;
        poolSizes[0].descriptorCount = 6;    // 2 sets x (prev + curr + output)
        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = vkDevice.createDescriptorPool(poolInfo);

        // One set per ping-pong parity. Set p reads state[p] and writes state[1 - p], so a step is
        // just "bind set[parity], dispatch, parity ^= 1" with no descriptor writes per frame.
        for (uint32_t p = 0; p < 2; ++p)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;
            descriptorSets[p] = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        // imageInfos must not reallocate: the writes below hold pointers into it.
        std::array<vk::DescriptorImageInfo, 6> imageInfos{};
        std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};
        std::vector<vk::WriteDescriptorSet> writes;
        writes.reserve(8);

        for (uint32_t p = 0; p < 2; ++p)
        {
            imageInfos[p * 3 + 0] = {nullptr, stateViews[p], vk::ImageLayout::eGeneral};
            imageInfos[p * 3 + 1] = {nullptr, stateViews[1 - p], vk::ImageLayout::eGeneral};
            imageInfos[p * 3 + 2] = {nullptr, outputView, vk::ImageLayout::eGeneral};

            for (uint32_t b = 0; b < 3; ++b)
            {
                vk::WriteDescriptorSet w{};
                w.dstSet = descriptorSets[p];
                w.dstBinding = b;
                w.descriptorCount = 1;
                w.descriptorType = vk::DescriptorType::eStorageImage;
                w.pImageInfo = &imageInfos[p * 3 + b];
                writes.push_back(w);
            }

            bufferInfos[p] = vk::DescriptorBufferInfo{impulseBuffer, 0, IMPULSE_BYTES};

            vk::WriteDescriptorSet w{};
            w.dstSet = descriptorSets[p];
            w.dstBinding = 3;
            w.descriptorCount = 1;
            w.descriptorType = vk::DescriptorType::eStorageBuffer;
            w.pBufferInfo = &bufferInfos[p];
            writes.push_back(w);
        }

        vkDevice.updateDescriptorSets(writes, nullptr);
    }

    void WaterRippleSim::createPipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/water/ripple_sim.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("WaterRippleSim: failed to load ripple_sim.glsl: {}",
                       shader->getLastCompilationError());
            shader.reset();
            return;
        }

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(RippleSimPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;
        pipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);

        if (!pipeline)
            vfLogError("WaterRippleSim: failed to create the ripple compute pipeline");
    }

    void WaterRippleSim::transitionImagesInitial()
    {
        auto cmd = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), device.getStagingCommandPool());

        const vk::ClearColorValue zero(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
        const vk::ImageSubresourceRange range = colorRange();

        // Explicit zero rather than leaving the contents undefined: NaNs in the state would survive
        // every damping multiply and permanently poison the patch.
        for (uint32_t i = 0; i < 2; ++i)
        {
            core::ImageUtilities::transitionImageLayout(cmd.get(), stateImages[i],
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);
            cmd.get().clearColorImage(stateImages[i], vk::ImageLayout::eTransferDstOptimal, zero, range);
            core::ImageUtilities::transitionImageLayout(cmd.get(), stateImages[i],
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
                vk::ImageAspectFlagBits::eColor);
        }

        // The output settles in ShaderReadOnly, which is the layout its set-9 descriptor declares and
        // the layout every frame starts and ends in.
        core::ImageUtilities::transitionImageLayout(cmd.get(), outputImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);
        cmd.get().clearColorImage(outputImage, vk::ImageLayout::eTransferDstOptimal, zero, range);
        core::ImageUtilities::transitionImageLayout(cmd.get(), outputImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        cmd.get().fillBuffer(impulseBuffer, 0, IMPULSE_BYTES, 0);

        core::Utilities::endSingleTimeCommands(device, cmd);
    }

    void WaterRippleSim::setParams(const RippleSimParams& newParams)
    {
        // Resizing the patch re-scales what every stored texel means AND moves the snap lattice the
        // previous origin sits on, so the scroll re-index would no longer be the exact whole-texel
        // shift it is documented to be. Dropping the field is the only coherent answer, and the
        // reset path costs nothing (dispatch parks the previous origin a patch away, so every texel
        // re-indexes out of the old window and reads zero).
        if (::water::rippleNeedsReset(params.patchSize, newParams.patchSize))
            needsReset = true;

        params = newParams;
    }

    void WaterRippleSim::queueImpulses(const std::vector<::water::WaterImpulse>& impulses)
    {
        if (!initialized || impulses.empty())
            return;

        for (const auto& imp : impulses)
        {
            if (imp.radius <= 0.0f || imp.strength == 0.0f)
                continue;

            // Newest wins: a splash that just happened matters more than one already a frame old.
            if (pendingImpulses.size() >= ::water::MAX_WATER_IMPULSES)
                pendingImpulses.erase(pendingImpulses.begin());

            pendingImpulses.push_back({imp.positionXZ, imp.radius, imp.strength});
        }
    }

    void WaterRippleSim::insertStateBarrier(vk::CommandBuffer cmd)
    {
        // Global rather than per-image: the sub-steps alternate which state image they write, and
        // the output image is written every step (a WAW hazard of its own).
        vk::MemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eComputeShader,
                            {}, barrier, nullptr, nullptr);
    }

    void WaterRippleSim::recordImpulseUpload(vk::CommandBuffer cmd)
    {
        if (pendingImpulses.empty())
            return;

        const vk::DeviceSize bytes =
            static_cast<vk::DeviceSize>(pendingImpulses.size()) * sizeof(GPUWaterImpulse);
        cmd.updateBuffer(impulseBuffer, 0, bytes, pendingImpulses.data());

        vk::BufferMemoryBarrier barrier{};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.buffer = impulseBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eComputeShader,
                            {}, nullptr, barrier, nullptr);
    }

    void WaterRippleSim::dispatch(vk::CommandBuffer cmd, float time)
    {
        if (!initialized || !pipeline)
            return;

        const float frameDt = lastDispatchTime >= 0.0f ? (time - lastDispatchTime) : 0.0f;
        lastDispatchTime = time;

        if (!params.enabled)
        {
            // Drop the queue and arm a reset so re-enabling cannot pop a stale field back onto the
            // water. The output image keeps its layout, so its descriptor stays valid either way.
            pendingImpulses.clear();
            stepAccumulator = 0.0f;
            needsReset = true;
            simulated = false;
            return;
        }

        const uint32_t steps = ::water::rippleSubstepCount(stepAccumulator, frameDt);
        if (steps == 0)
            return;   // the output image simply keeps last frame's contents

        const float texelSize = ::water::rippleTexelSize(params.patchSize);
        if (texelSize <= 0.0f)
            return;

        if (needsReset)
        {
            // Park the previous origin a whole patch away: every texel then re-indexes outside the
            // old window and reads zero, which clears the field in the same step it simulates - no
            // separate clear pass, no extra barriers.
            previousOrigin = params.origin - glm::vec2(params.patchSize * 4.0f);
            needsReset = false;
        }

        RippleSimPushConstants pc{};
        pc.patchSize = params.patchSize;
        pc.dt = ::water::RIPPLE_SIM_STEP;
        pc.waveSpeed = ::water::rippleClampWaveSpeed(params.waveSpeed, ::water::RIPPLE_SIM_STEP, texelSize);
        pc.damping = ::water::rippleDampingFactor(params.damping, ::water::RIPPLE_SIM_STEP);
        pc.foamGain = params.foamGain;
        pc.foamDecay = ::water::rippleDampingFactor(params.foamDecay, ::water::RIPPLE_SIM_STEP);
        pc.resolution = RIPPLE_RES;

        recordImpulseUpload(cmd);

        // One barrier covering both hazards at the frame boundary: last frame's water draw sampled
        // the output, and last frame's final step wrote the state.
        {
            vk::MemoryBarrier stateBarrier{};
            stateBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            stateBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

            vk::ImageMemoryBarrier toGeneral{};
            toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral.image = outputImage;
            toGeneral.subresourceRange = colorRange();
            toGeneral.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            toGeneral.newLayout = vk::ImageLayout::eGeneral;
            toGeneral.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            toGeneral.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

            cmd.pipelineBarrier(RIPPLE_READ_STAGES | vk::PipelineStageFlagBits::eComputeShader,
                                vk::PipelineStageFlagBits::eComputeShader,
                                {}, stateBarrier, nullptr, toGeneral);
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);

        const uint32_t groups = (RIPPLE_RES + RIPPLE_GROUP - 1) / RIPPLE_GROUP;

        for (uint32_t i = 0; i < steps; ++i)
        {
            // The window only re-centres once per frame, so only the first sub-step scrolls. And the
            // impulses are a single velocity kick: applying them on every sub-step would make the
            // splash strength depend on the frame rate.
            pc.originXZ = params.origin;
            pc.prevOriginXZ = (i == 0) ? previousOrigin : params.origin;
            pc.impulseCount = (i == 0) ? static_cast<uint32_t>(pendingImpulses.size()) : 0u;

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0,
                                   descriptorSets[parity], nullptr);
            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                              sizeof(RippleSimPushConstants), &pc);
            cmd.dispatch(groups, groups, 1);

            parity ^= 1u;

            if (i + 1 < steps)
                insertStateBarrier(cmd);
        }

        // Back to the layout the set-9 descriptor declares, with the vertex stage named explicitly.
        {
            vk::ImageMemoryBarrier toRead{};
            toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.image = outputImage;
            toRead.subresourceRange = colorRange();
            toRead.oldLayout = vk::ImageLayout::eGeneral;
            toRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            toRead.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            toRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, RIPPLE_READ_STAGES,
                                {}, nullptr, nullptr, toRead);
        }

        previousOrigin = params.origin;
        pendingImpulses.clear();
        simulated = true;
    }
}
