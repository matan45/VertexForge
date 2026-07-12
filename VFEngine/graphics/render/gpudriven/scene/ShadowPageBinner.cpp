#include "ShadowPageBinner.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../GPUDrivenTypes.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace render::gpudriven
{
    namespace
    {
        // B2: a binned VIEW is a directional level, the spot view, or a point-cube face. Sized to
        // cover many binned lights (directional levels + spot + up to a few point lights * 6 faces).
        constexpr uint32_t kMaxShadowViews = 256;
    }

    ShadowPageBinner::ShadowPageBinner(core::Device& device)
        : device(device)
    {
    }

    ShadowPageBinner::~ShadowPageBinner()
    {
        cleanup();
    }

    void ShadowPageBinner::init(vk::DescriptorSetLayout perDrawLayout)
    {
        if (initialized)
        {
            return;
        }

        vfLogInfo("ShadowPageBinner: Initializing...");

        // B2: global page-index space across ALL binned views (directional levels + spot + point
        // faces). Each view occupies a contiguous [pageBaseOffset, +pagesX*pagesY) block; the sum
        // is capped here (lights whose block would overflow fall back to legacy for that light).
        maxPageTableEntries = 8192;
        drawSetLayout = perDrawLayout;

        createBuffers();
        createComputePipeline();
        createDescriptors(perDrawLayout);

        initialized = true;
    }

    void ShadowPageBinner::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (computePipeline)
        {
            vkDevice.destroyPipeline(computePipeline);
            computePipeline = nullptr;
        }

        if (computePipelineLayout)
        {
            vkDevice.destroyPipelineLayout(computePipelineLayout);
            computePipelineLayout = nullptr;
        }

        if (computeSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(computeSetLayout);
            computeSetLayout = nullptr;
        }

        if (cullShader)
        {
            cullShader->cleanUp();
            cullShader.reset();
        }

        destroyBuffers();

        // drawSetLayout is owned by the mesh pipeline (passed into init) — do not destroy it.
        // computeDescriptorSet / drawDescriptorSet were allocated from descriptorPool and are
        // freed with it above.
        computeDescriptorSet = nullptr;
        drawDescriptorSet = nullptr;

        initialized = false;
    }

    void ShadowPageBinner::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();
        auto& memManager = device.getMemoryManager();

        auto makeDeviceLocal = [&](vk::DeviceSize size, vk::BufferUsageFlags usage,
                                   vk::Buffer& buffer, core::VulkanAllocation& alloc)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = size;
            request.usage = usage;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, buffer, alloc, memManager);
        };

        const vk::DeviceSize binSlots =
            static_cast<vk::DeviceSize>(MAX_RENDERED_SHADOW_PAGES) * SHADOW_BIN_CAPACITY;

        // Indirect draw commands, one per bin slot.
        makeDeviceLocal(binSlots * sizeof(MeshTasksIndirectCommand),
                        vk::BufferUsageFlagBits::eStorageBuffer |
                        vk::BufferUsageFlagBits::eIndirectBuffer |
                        vk::BufferUsageFlagBits::eTransferDst,
                        binCommandBuffer, binCommandAlloc);

        // Per-draw data, one per bin slot (parallel to the commands).
        makeDeviceLocal(binSlots * sizeof(PerDrawData),
                        vk::BufferUsageFlagBits::eStorageBuffer,
                        binPerDrawBuffer, binPerDrawAlloc);

        // Per-page draw counts (atomic).
        makeDeviceLocal(static_cast<vk::DeviceSize>(MAX_RENDERED_SHADOW_PAGES) * sizeof(uint32_t),
                        vk::BufferUsageFlagBits::eStorageBuffer |
                        vk::BufferUsageFlagBits::eIndirectBuffer |
                        vk::BufferUsageFlagBits::eTransferDst,
                        binCountBuffer, binCountAlloc);

        // Overflow diagnostics: [overflowedPages, droppedDraws, ...]. TransferSrc for optional readback.
        makeDeviceLocal(4 * sizeof(uint32_t),
                        vk::BufferUsageFlagBits::eStorageBuffer |
                        vk::BufferUsageFlagBits::eTransferDst |
                        vk::BufferUsageFlagBits::eTransferSrc,
                        overflowBuffer, overflowAlloc);

        // Per-level view-projection + biases (device-local target of the ring copy).
        makeDeviceLocal(static_cast<vk::DeviceSize>(kMaxShadowViews) * sizeof(ShadowLevelData),
                        vk::BufferUsageFlagBits::eStorageBuffer |
                        vk::BufferUsageFlagBits::eTransferDst,
                        levelDataBuffer, levelDataAlloc);

        // Page -> render-slot table (device-local target of the ring copy).
        makeDeviceLocal(static_cast<vk::DeviceSize>(maxPageTableEntries) * sizeof(uint32_t),
                        vk::BufferUsageFlagBits::eStorageBuffer |
                        vk::BufferUsageFlagBits::eTransferDst,
                        pageBinBaseBuffer, pageBinBaseAlloc);

        // Host-visible ring staging for the per-frame inputs (persistently mapped).
        for (auto& sf : stagingFrames)
        {
            {
                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = static_cast<vk::DeviceSize>(kMaxShadowViews) * sizeof(ShadowLevelData);
                request.usage = vk::BufferUsageFlagBits::eTransferSrc;
                request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent;
                core::BufferUtilities::createBuffer(request, sf.levelBuffer, sf.levelAlloc, memManager);
                sf.levelMapped = sf.levelAlloc.mappedPtr;
            }
            {
                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = static_cast<vk::DeviceSize>(maxPageTableEntries) * sizeof(uint32_t);
                request.usage = vk::BufferUsageFlagBits::eTransferSrc;
                request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent;
                core::BufferUtilities::createBuffer(request, sf.pageBaseBuffer, sf.pageBaseAlloc, memManager);
                sf.pageBaseMapped = sf.pageBaseAlloc.mappedPtr;
            }
        }

        vfLogDebug("ShadowPageBinner: Created bin arenas ({} slots, {} MB perDraw), {} staging frames",
                   static_cast<uint32_t>(binSlots),
                   (binSlots * sizeof(PerDrawData)) / (1024 * 1024),
                   core::MAX_FRAMES_IN_FLIGHT);
    }

    void ShadowPageBinner::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        auto& memManager = device.getMemoryManager();

        for (auto& sf : stagingFrames)
        {
            sf.levelMapped = nullptr;
            sf.pageBaseMapped = nullptr;
            core::BufferUtilities::destroyBuffer(logicalDevice, sf.levelBuffer, sf.levelAlloc, memManager);
            core::BufferUtilities::destroyBuffer(logicalDevice, sf.pageBaseBuffer, sf.pageBaseAlloc, memManager);
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, binCommandBuffer, binCommandAlloc, memManager);
        core::BufferUtilities::destroyBuffer(logicalDevice, binPerDrawBuffer, binPerDrawAlloc, memManager);
        core::BufferUtilities::destroyBuffer(logicalDevice, binCountBuffer, binCountAlloc, memManager);
        core::BufferUtilities::destroyBuffer(logicalDevice, overflowBuffer, overflowAlloc, memManager);
        core::BufferUtilities::destroyBuffer(logicalDevice, levelDataBuffer, levelDataAlloc, memManager);
        core::BufferUtilities::destroyBuffer(logicalDevice, pageBinBaseBuffer, pageBinBaseAlloc, memManager);
    }

    void ShadowPageBinner::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Compute descriptor set layout (set 0) — MUST match gpu_cull_shadow_bin.glsl:
        //   b0 objects[] (readonly SSBO)        b1 CameraUBO (uniform)
        //   b2 activeIndices[] (readonly SSBO)  b3 levels[] (readonly SSBO)
        //   b4 pageBinBase[] (readonly SSBO)    b5 binCommands[] (writeonly SSBO)
        //   b6 binPerDraw[] (writeonly SSBO)    b7 binCount[] (SSBO)
        //   b8 overflow[] (SSBO)
        std::array<vk::DescriptorSetLayoutBinding, 9> bindings{};
        auto ssbo = [](uint32_t binding)
        {
            vk::DescriptorSetLayoutBinding b{};
            b.binding = binding;
            b.descriptorType = vk::DescriptorType::eStorageBuffer;
            b.descriptorCount = 1;
            b.stageFlags = vk::ShaderStageFlagBits::eCompute;
            return b;
        };

        bindings[0] = ssbo(0);
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings[2] = ssbo(2);
        bindings[3] = ssbo(3);
        bindings[4] = ssbo(4);
        bindings[5] = ssbo(5);
        bindings[6] = ssbo(6);
        bindings[7] = ssbo(7);
        bindings[8] = ssbo(8);

        computeSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(ShadowBinPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        computePipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        cullShader = std::make_unique<core::Shader>(device);
        cullShader->readShader("../../resources/shaders/gpudriven/gpu_cull_shadow_bin.glsl");

        const auto& stages = cullShader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("ShadowPageBinner: Failed to load shader: {}", cullShader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = computePipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("ShadowPageBinner: Failed to create compute pipeline");
            return;
        }
        vfLogInfo("ShadowPageBinner: Created compute pipeline");
    }

    void ShadowPageBinner::createDescriptors(vk::DescriptorSetLayout perDrawLayout)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Pool holds the compute set (8 storage + 1 uniform) and the draw set, which is
        // allocated from the shared mesh perDrawLayout. That layout carries 7 storage
        // bindings: 0 PerDrawData, 1 instance, 2 object, 3-5 SVT (VK-1209), 6 toon table
        // (VK-1493) — the allocation consumes all of them even though the binner only writes
        // binding 0. Update-after-bind so the externally-owned perDrawLayout can be allocated
        // from it and so per-frame descriptor writes are legal on the bound sets.
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 15; // 8 (compute b0,b2..b8) + 7 (draw b0..b6)
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;  // compute b1

        descriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 2, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()));

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &computeSetLayout;
            computeDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &perDrawLayout;
            drawDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        // Write the binner's own buffers once — these never change:
        //   compute b3..b8 (levels/pageBinBase/binCommands/binPerDraw/binCount/overflow)
        //   draw b0 (bin PerDrawData). Draw b1/b2 and compute b0..b2 come from updateComputeDescriptors.
        auto bufInfo = [](vk::Buffer buffer)
        {
            vk::DescriptorBufferInfo info{};
            info.buffer = buffer;
            info.offset = 0;
            info.range = VK_WHOLE_SIZE;
            return info;
        };

        vk::DescriptorBufferInfo levelInfo = bufInfo(levelDataBuffer);
        vk::DescriptorBufferInfo pageBaseInfo = bufInfo(pageBinBaseBuffer);
        vk::DescriptorBufferInfo cmdInfo = bufInfo(binCommandBuffer);
        vk::DescriptorBufferInfo perDrawInfo = bufInfo(binPerDrawBuffer);
        vk::DescriptorBufferInfo countInfo = bufInfo(binCountBuffer);
        vk::DescriptorBufferInfo overflowInfo = bufInfo(overflowBuffer);

        auto ssboWrite = [](vk::DescriptorSet set, uint32_t binding, const vk::DescriptorBufferInfo& info)
        {
            vk::WriteDescriptorSet w{};
            w.dstSet = set;
            w.dstBinding = binding;
            w.dstArrayElement = 0;
            w.descriptorCount = 1;
            w.descriptorType = vk::DescriptorType::eStorageBuffer;
            w.pBufferInfo = &info;
            return w;
        };

        std::array<vk::WriteDescriptorSet, 7> writes{};
        writes[0] = ssboWrite(computeDescriptorSet, 3, levelInfo);
        writes[1] = ssboWrite(computeDescriptorSet, 4, pageBaseInfo);
        writes[2] = ssboWrite(computeDescriptorSet, 5, cmdInfo);
        writes[3] = ssboWrite(computeDescriptorSet, 6, perDrawInfo);
        writes[4] = ssboWrite(computeDescriptorSet, 7, countInfo);
        writes[5] = ssboWrite(computeDescriptorSet, 8, overflowInfo);
        writes[6] = ssboWrite(drawDescriptorSet, 0, perDrawInfo);

        vkDevice.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        vfLogInfo("ShadowPageBinner: Allocated + wrote descriptor sets");
    }

    void ShadowPageBinner::updateFrameData(const std::vector<ShadowLevelData>& levels,
                                           const std::vector<uint32_t>& pageBinBase)
    {
        StagingFrame& sf = stagingFrames[currentStagingFrame];

        stagedLevelCount = std::min(static_cast<uint32_t>(levels.size()), kMaxShadowViews);
        stagedPageCount = std::min(static_cast<uint32_t>(pageBinBase.size()), maxPageTableEntries);

        if (sf.levelMapped && stagedLevelCount > 0)
        {
            std::memcpy(sf.levelMapped, levels.data(),
                        static_cast<size_t>(stagedLevelCount) * sizeof(ShadowLevelData));
        }
        if (sf.pageBaseMapped && stagedPageCount > 0)
        {
            std::memcpy(sf.pageBaseMapped, pageBinBase.data(),
                        static_cast<size_t>(stagedPageCount) * sizeof(uint32_t));
        }
    }

    void ShadowPageBinner::updateComputeDescriptors(vk::Buffer objectBuffer, vk::Buffer cameraBuffer,
                                                    vk::Buffer instanceTransformBuffer,
                                                    vk::Buffer activeIndexBuffer)
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo objectInfo{};
        objectInfo.buffer = objectBuffer;
        objectInfo.offset = 0;
        objectInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cameraBuffer;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUCameraData);

        vk::DescriptorBufferInfo activeInfo{};
        activeInfo.buffer = activeIndexBuffer;
        activeInfo.offset = 0;
        activeInfo.range = VK_WHOLE_SIZE;

        vk::DescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = instanceTransformBuffer;
        instanceInfo.offset = 0;
        instanceInfo.range = VK_WHOLE_SIZE;

        std::vector<vk::WriteDescriptorSet> writes;
        writes.reserve(5);

        // Skip any binding whose buffer isn't ready — storage/uniform descriptors must not bind
        // VK_NULL_HANDLE. The caller supplies real buffers before dispatch(); mirrors GPUCullLODPipeline.
        auto pushBuf = [&](vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                           const vk::DescriptorBufferInfo& info)
        {
            if (!info.buffer) return;
            vk::WriteDescriptorSet w{};
            w.dstSet = set;
            w.dstBinding = binding;
            w.dstArrayElement = 0;
            w.descriptorCount = 1;
            w.descriptorType = type;
            w.pBufferInfo = &info;
            writes.push_back(w);
        };

        // Compute set: b0 objects, b1 camera, b2 activeIndices (own buffers b3..b8 stay as written in
        // createDescriptors — they never change).
        pushBuf(computeDescriptorSet, 0, vk::DescriptorType::eStorageBuffer, objectInfo);
        pushBuf(computeDescriptorSet, 1, vk::DescriptorType::eUniformBuffer, cameraInfo);
        pushBuf(computeDescriptorSet, 2, vk::DescriptorType::eStorageBuffer, activeInfo);

        // Draw set: b1 instance transforms, b2 objects (b0 = bin PerDrawData written in createDescriptors),
        // so the shadow task/mesh shader's instanced path reads real instance/object data.
        pushBuf(drawDescriptorSet, 1, vk::DescriptorType::eStorageBuffer, instanceInfo);
        pushBuf(drawDescriptorSet, 2, vk::DescriptorType::eStorageBuffer, objectInfo);

        if (!writes.empty())
        {
            vkDevice.updateDescriptorSets(writes, {});
        }
    }

    void ShadowPageBinner::recordReset(vk::CommandBuffer cmd)
    {
        // Clear per-page counts + overflow diagnostics for this frame.
        cmd.fillBuffer(binCountBuffer, 0, VK_WHOLE_SIZE, 0);
        cmd.fillBuffer(overflowBuffer, 0, VK_WHOLE_SIZE, 0);
        // No barrier here: the caller issues one transfer->compute barrier covering these fills and
        // the recordUpload() copies before dispatch() (recordUpload is the "pre-barrier" stage).
    }

    void ShadowPageBinner::recordUpload(vk::CommandBuffer cmd)
    {
        StagingFrame& sf = stagingFrames[currentStagingFrame];

        if (stagedLevelCount > 0)
        {
            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = static_cast<vk::DeviceSize>(stagedLevelCount) * sizeof(ShadowLevelData);
            cmd.copyBuffer(sf.levelBuffer, levelDataBuffer, region);
        }
        if (stagedPageCount > 0)
        {
            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = static_cast<vk::DeviceSize>(stagedPageCount) * sizeof(uint32_t);
            cmd.copyBuffer(sf.pageBaseBuffer, pageBinBaseBuffer, region);
        }
        // These transfer writes ride the caller's transfer->compute barrier before dispatch().
    }

    void ShadowPageBinner::dispatch(vk::CommandBuffer cmd, uint32_t objectCount, uint32_t viewCount,
                                    uint32_t flags)
    {
        if (!initialized || objectCount == 0 || viewCount == 0)
        {
            return;
        }

        viewCount = std::min(viewCount, kMaxShadowViews);

        ShadowBinPushConstants pushData{};
        pushData.objectCount = objectCount;
        pushData.viewCount = viewCount;
        pushData.binCapacity = SHADOW_BIN_CAPACITY;
        pushData.flags = flags;
        pushData.pad0 = 0;
        pushData.pad1 = 0;
        pushData.pad2 = 0;
        pushData.pad3 = 0;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout, 0,
                               computeDescriptorSet, {});
        cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                          sizeof(pushData), &pushData);

        uint32_t groupCountX = (objectCount + 63u) / 64u;
        cmd.dispatch(groupCountX, viewCount, 1);
    }

    void ShadowPageBinner::recordPostBarrier(vk::CommandBuffer cmd)
    {
        // Compute writes (bin commands/counts/per-draw) -> consumed by the indirect-count draw
        // (commands + counts) and the task shader (per-draw data).
        auto mk = [](vk::Buffer buffer)
        {
            vk::BufferMemoryBarrier b{};
            b.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            b.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eShaderRead;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.buffer = buffer;
            b.offset = 0;
            b.size = VK_WHOLE_SIZE;
            return b;
        };

        std::array<vk::BufferMemoryBarrier, 3> barriers{
            mk(binCommandBuffer),
            mk(binCountBuffer),
            mk(binPerDrawBuffer)
        };

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eTaskShaderEXT,
            {},
            {},
            barriers,
            {});
    }
}
