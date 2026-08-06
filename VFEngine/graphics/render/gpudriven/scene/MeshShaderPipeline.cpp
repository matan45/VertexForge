#include "MeshShaderPipeline.hpp"
#include "MeshletBuffer.hpp"
#include "MergedMeshBuffer.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../probe/ReflectionProbeTypes.hpp"
#include "print/Log.hpp"
#include <array>
#include <string>

namespace render::gpudriven
{
    MeshShaderPipeline::MeshShaderPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device)
          , swapChain(swapChain)
    {
    }

    MeshShaderPipeline::~MeshShaderPipeline()
    {
        cleanup();
    }

    void MeshShaderPipeline::init(const MeshPipelineInitInfo& info)
    {
        isTransparentMode = info.transparentMode;
        isWBOITMode = info.wboitMode;
        cachedLightDataLayout = info.lightDataLayout;
        cachedClusterGridLayout = info.clusterGridLayout;
        cachedCullingOutputLayout = info.cullingOutputLayout;
        cachedShadowDataLayout = info.shadowDataLayout;
        cachedShadowTextureLayout = info.shadowTextureLayout;
        cachedGIProbeDataLayout = info.giProbeDataLayout;
        cachedCausticLayout = info.causticLayout;


        createStatsBuffer();
        createPerDrawDataDescriptor();
        createMeshletDataDescriptor();
        createVertexDataDescriptor();
        createMeshShaderGraphicsPipeline(info);
    }

    void MeshShaderPipeline::createStatsBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = sizeof(MeshletCullingStats);
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, statsBuffer, statsBufferAllocation, device.getMemoryManager());

        std::memset(statsBufferAllocation.mappedPtr, 0, sizeof(MeshletCullingStats));

    }

    void MeshShaderPipeline::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (meshShader)
        {
            meshShader->cleanUp();
            meshShader.reset();
        }

        if (graphicsPipeline)
        {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        core::BufferUtilities::destroyBuffer(vkDevice, statsBuffer, statsBufferAllocation, device.getMemoryManager());

        if (perDrawDataPool)
        {
            vkDevice.destroyDescriptorPool(perDrawDataPool);
            perDrawDataPool = nullptr;
        }
        if (perDrawDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(perDrawDataLayout);
            perDrawDataLayout = nullptr;
        }

        if (meshletDataPool)
        {
            vkDevice.destroyDescriptorPool(meshletDataPool);
            meshletDataPool = nullptr;
        }
        if (meshletDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(meshletDataLayout);
            meshletDataLayout = nullptr;
        }

        if (vertexDataPool)
        {
            vkDevice.destroyDescriptorPool(vertexDataPool);
            vertexDataPool = nullptr;
        }
        if (vertexDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(vertexDataLayout);
            vertexDataLayout = nullptr;
        }
        if (emptyPlaceholderLayout)
        {
            vkDevice.destroyDescriptorSetLayout(emptyPlaceholderLayout);
            emptyPlaceholderLayout = nullptr;
        }
        // Shared RT mask set (set 13): destroyed here and lazily rebuilt on the next pipeline build,
        // which re-copies the cached producer descriptors (rt*MaskProducer) into the fresh set.
        rtMaskSet.reset();
    }

    void MeshShaderPipeline::recreate(const MeshPipelineInitInfo& info)
    {
        isTransparentMode = info.transparentMode;
        isWBOITMode = info.wboitMode;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        cachedLightDataLayout = info.lightDataLayout;
        cachedClusterGridLayout = info.clusterGridLayout;
        cachedCullingOutputLayout = info.cullingOutputLayout;
        cachedShadowDataLayout = info.shadowDataLayout;
        cachedShadowTextureLayout = info.shadowTextureLayout;
        cachedGIProbeDataLayout = info.giProbeDataLayout;
        cachedCausticLayout = info.causticLayout;


        if (graphicsPipeline)
        {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (meshShader)
        {
            meshShader->cleanUp();
        }

        createMeshShaderGraphicsPipeline(info);
    }

    void MeshShaderPipeline::updatePerDrawDescriptor(vk::Buffer perDrawDataBuffer)
    {
        vk::DescriptorBufferInfo perDrawInfo{};
        perDrawInfo.buffer = perDrawDataBuffer;
        perDrawInfo.offset = 0;
        perDrawInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet perDrawWrite{};
        perDrawWrite.dstSet = perDrawDataDescriptorSet;
        perDrawWrite.dstBinding = 0;
        perDrawWrite.dstArrayElement = 0;
        perDrawWrite.descriptorCount = 1;
        perDrawWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawWrite.pBufferInfo = &perDrawInfo;

        device.getLogicalDevice().updateDescriptorSets(perDrawWrite, {});
    }

    void MeshShaderPipeline::updateInstanceTransformDescriptor(vk::Buffer instanceTransformBuffer)
    {
        vk::DescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = instanceTransformBuffer;
        instanceInfo.offset = 0;
        instanceInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet instanceWrite{};
        instanceWrite.dstSet = perDrawDataDescriptorSet;
        instanceWrite.dstBinding = 1;
        instanceWrite.dstArrayElement = 0;
        instanceWrite.descriptorCount = 1;
        instanceWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        instanceWrite.pBufferInfo = &instanceInfo;

        device.getLogicalDevice().updateDescriptorSets(instanceWrite, {});
    }

    void MeshShaderPipeline::updateObjectBufferDescriptor(vk::Buffer objectBuffer)
    {
        vk::DescriptorBufferInfo objectInfo{};
        objectInfo.buffer = objectBuffer;
        objectInfo.offset = 0;
        objectInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet objectWrite{};
        objectWrite.dstSet = perDrawDataDescriptorSet;
        objectWrite.dstBinding = 2;
        objectWrite.dstArrayElement = 0;
        objectWrite.descriptorCount = 1;
        objectWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        objectWrite.pBufferInfo = &objectInfo;

        device.getLogicalDevice().updateDescriptorSets(objectWrite, {});
    }

    void MeshShaderPipeline::updateToonProfileDescriptor(vk::Buffer toonProfileBuffer)
    {
        if (!perDrawDataDescriptorSet || !toonProfileBuffer)
            return;

        vk::DescriptorBufferInfo toonInfo{toonProfileBuffer, 0, VK_WHOLE_SIZE};

        vk::WriteDescriptorSet toonWrite{};
        toonWrite.dstSet = perDrawDataDescriptorSet;
        toonWrite.dstBinding = 6;
        toonWrite.dstArrayElement = 0;
        toonWrite.descriptorCount = 1;
        toonWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        toonWrite.pBufferInfo = &toonInfo;

        device.getLogicalDevice().updateDescriptorSets(toonWrite, {});
    }

    void MeshShaderPipeline::updateWindDescriptor(vk::Buffer windUboBuffer)
    {
        if (!perDrawDataDescriptorSet || !windUboBuffer)
            return;

        vk::DescriptorBufferInfo windInfo{windUboBuffer, 0, VK_WHOLE_SIZE};

        vk::WriteDescriptorSet windWrite{};
        windWrite.dstSet = perDrawDataDescriptorSet;
        windWrite.dstBinding = 7;
        windWrite.dstArrayElement = 0;
        windWrite.descriptorCount = 1;
        windWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        windWrite.pBufferInfo = &windInfo;

        device.getLogicalDevice().updateDescriptorSets(windWrite, {});
    }

    void MeshShaderPipeline::updateSVTResources(vk::Buffer pageTableBuffer, vk::Buffer feedbackBuffer,
                                                vk::Buffer imageInfoBuffer)
    {
        if (!svtSampleEnabled || !perDrawDataDescriptorSet ||
            !pageTableBuffer || !feedbackBuffer || !imageInfoBuffer)
            return;

        vk::DescriptorBufferInfo pt{pageTableBuffer, 0, VK_WHOLE_SIZE};
        vk::DescriptorBufferInfo fb{feedbackBuffer, 0, VK_WHOLE_SIZE};
        vk::DescriptorBufferInfo ii{imageInfoBuffer, 0, VK_WHOLE_SIZE};

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0].dstSet = perDrawDataDescriptorSet; writes[0].dstBinding = 3; writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer; writes[0].pBufferInfo = &pt;
        writes[1].dstSet = perDrawDataDescriptorSet; writes[1].dstBinding = 4; writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer; writes[1].pBufferInfo = &fb;
        writes[2].dstSet = perDrawDataDescriptorSet; writes[2].dstBinding = 5; writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer; writes[2].pBufferInfo = &ii;

        device.getLogicalDevice().updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void MeshShaderPipeline::updateRVTBlendResources(vk::Buffer pageTableBuffer, vk::Buffer paramsBuffer)
    {
        // VK-1620. Only TWO bindings, not SVT's three, and both storage buffers:
        //  - the RVT atlas planes are reached through the BINDLESS heap (their slots ride
        //    VTImageInfo.pad0/1/2 inside the params buffer), exactly as SVT reaches its BC7 atlas,
        //    which is what keeps this off the crowded shared set space entirely;
        //  - there is no feedback binding, deliberately. vtDesiredMip keys off dFdx/dFdy of world
        //    XZ, which collapses toward zero on a near-vertical cliff face, so a prop would request
        //    far finer mips than the terrain ever needs and evict pages the terrain is using. The
        //    coarsest mip is pinned resident, so a lookup always resolves without asking.
        //
        // Storage rather than uniform for the params too: createUpdateAfterBindLayout stamps
        // eUpdateAfterBind on every binding, and a UBO there would add a
        // descriptorBindingUniformBufferUpdateAfterBind device-feature dependency. Same reasoning
        // as the VK-1580 wind block above.
        if (!rvtBlendEnabled || !perDrawDataDescriptorSet || !pageTableBuffer || !paramsBuffer)
            return;

        vk::DescriptorBufferInfo pt{pageTableBuffer, 0, VK_WHOLE_SIZE};
        vk::DescriptorBufferInfo pr{paramsBuffer, 0, VK_WHOLE_SIZE};

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = perDrawDataDescriptorSet; writes[0].dstBinding = 8; writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer; writes[0].pBufferInfo = &pt;
        writes[1].dstSet = perDrawDataDescriptorSet; writes[1].dstBinding = 9; writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer; writes[1].pBufferInfo = &pr;

        device.getLogicalDevice().updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void MeshShaderPipeline::updateMeshletDescriptors(MeshletBuffer& meshletBuffer)
    {
        std::array<vk::DescriptorBufferInfo, 3> bufferInfos{};

        bufferInfos[0].buffer = meshletBuffer.getMeshletBuffer();
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = meshletBuffer.getMeshletVertexBuffer();
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        bufferInfos[2].buffer = meshletBuffer.getMeshletPrimitiveBuffer();
        bufferInfos[2].offset = 0;
        bufferInfos[2].range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 3> writes{};
        for (uint32_t i = 0; i < 3; i++)
        {
            writes[i].dstSet = meshletDataDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[i].pBufferInfo = &bufferInfos[i];
        }

        device.getLogicalDevice().updateDescriptorSets(writes, {});
    }

    void MeshShaderPipeline::updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler)
    {
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = hiZSampler;
        imageInfo.imageView = hiZView;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = meshletDataDescriptorSet;
        write.dstBinding = 4;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &imageInfo;

        device.getLogicalDevice().updateDescriptorSets(write, {});
    }

    void MeshShaderPipeline::updateVertexDescriptors(MergedMeshBuffer& mergedBuffer)
    {
        vk::DescriptorBufferInfo vertexInfo{};
        vertexInfo.buffer = mergedBuffer.getVertexBuffer();
        vertexInfo.offset = 0;
        vertexInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet vertexWrite{};
        vertexWrite.dstSet = vertexDataDescriptorSet;
        vertexWrite.dstBinding = 0;
        vertexWrite.dstArrayElement = 0;
        vertexWrite.descriptorCount = 1;
        vertexWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        vertexWrite.pBufferInfo = &vertexInfo;

        device.getLogicalDevice().updateDescriptorSets(vertexWrite, {});
    }

    void MeshShaderPipeline::updateLightingDescriptors(vk::DescriptorSet lightDataDescSet,
                                                       vk::DescriptorSet clusterGridDescSet,
                                                       vk::DescriptorSet cullingOutputDescSet)
    {
        lightDataDescriptorSet = lightDataDescSet;
        clusterGridDescriptorSet = clusterGridDescSet;
        cullingOutputDescriptorSet = cullingOutputDescSet;
    }

    void MeshShaderPipeline::updateShadowDescriptors(vk::DescriptorSet shadowDataDescSet,
                                                     vk::DescriptorSet shadowTextureDescSet)
    {
        if (!shadowDataDescSet || !shadowTextureDescSet)
        {
            vfLogWarning("Invalid shadow descriptor sets provided");
            return;
        }
        shadowDataDescriptorSet = shadowDataDescSet;
        shadowTextureDescriptorSet = shadowTextureDescSet;
    }

    void MeshShaderPipeline::updateGIProbeDescriptor(vk::DescriptorSet giProbeDescSet)
    {
        giProbeDataDescriptorSet = giProbeDescSet;
    }

    void MeshShaderPipeline::updateCausticDescriptor(vk::DescriptorSet causticDescSet)
    {
        causticDescriptorSet = causticDescSet;
    }

    void MeshShaderPipeline::updateRTShadowMaskDescriptor(vk::DescriptorSet rtShadowMaskDescSet)
    {
        // VK-1398: cache the directional RT producer only; the copy into the ringed set (binding 0,
        // set 13) happens lazily per image slot in ensureRTMaskSlot(). Mark all slots dirty on change.
        if (rtDirectionalMaskProducer != rtShadowMaskDescSet)
        {
            rtDirectionalMaskProducer = rtShadowMaskDescSet;
            markAllRTMaskSlotsDirty();
        }
    }

    void MeshShaderPipeline::updateWorldMaskDescriptor(vk::DescriptorSet worldMaskDescSet)
    {
        worldMaskDescriptorSet = worldMaskDescSet;
    }

    void MeshShaderPipeline::updateRTSpotShadowMaskDescriptor(vk::DescriptorSet rtSpotShadowMaskDescSet)
    {
        // Cache the spot RT producer (binding 1, set 13); see updateRTShadowMaskDescriptor.
        if (rtSpotMaskProducer != rtSpotShadowMaskDescSet)
        {
            rtSpotMaskProducer = rtSpotShadowMaskDescSet;
            markAllRTMaskSlotsDirty();
        }
    }

    void MeshShaderPipeline::updateRTPointShadowMaskDescriptor(vk::DescriptorSet rtPointShadowMaskDescSet)
    {
        // Cache the point RT producer (binding 2, set 13); see updateRTShadowMaskDescriptor.
        if (rtPointMaskProducer != rtPointShadowMaskDescSet)
        {
            rtPointMaskProducer = rtPointShadowMaskDescSet;
            markAllRTMaskSlotsDirty();
        }
    }

    void MeshShaderPipeline::ensureRTMaskSlot(uint32_t imageIndex)
    {
        // Lazily populate the ring slot for the image currently being recorded from the cached
        // producers. Only writes when dirty, so steady state issues zero descriptor writes (no
        // write-after-bind hazard). The image's prior submission has retired (per-image fence), so
        // writing this slot is safe.
        if (!rtMaskSet || imageIndex >= core::MAX_SWAPCHAIN_IMAGES || !rtMaskSlotDirty[imageIndex])
            return;

        if (rtDirectionalMaskProducer)
            rtMaskSet->copyInto(raytracing::RTShadowMaskSet::BINDING_DIRECTIONAL, rtDirectionalMaskProducer, imageIndex);
        if (rtSpotMaskProducer)
            rtMaskSet->copyInto(raytracing::RTShadowMaskSet::BINDING_SPOT, rtSpotMaskProducer, imageIndex);
        if (rtPointMaskProducer)
            rtMaskSet->copyInto(raytracing::RTShadowMaskSet::BINDING_POINT, rtPointMaskProducer, imageIndex);

        rtMaskSlotDirty[imageIndex] = false;
    }

    void MeshShaderPipeline::createPerDrawDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::vector<vk::DescriptorSetLayoutBinding> bindings(3);

        // Binding 0: PerDrawData SSBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eTaskEXT |
            vk::ShaderStageFlagBits::eMeshEXT |
            vk::ShaderStageFlagBits::eFragment;

        // Binding 1: Instance transform SSBO (read by task shader)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        // Binding 2: GPUObjectData SSBO (read by task shader for per-instance LOD + culling;
        // VK-1580 also read by the mesh shader for the foliage-wind height AABB)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

        // VK-1209: SVT page table (3) / feedback (4) / image info (5), fragment stage. Always declared
        // so the layout matches the shader whether SVT_ENABLED is compiled in or not (a runtime toggle
        // recompiles the shader but does not rebuild this layout). When SVT is off the shader doesn't
        // reference them and they stay unwritten.
        //
        // That is safe, but NOT for the reason this comment used to give: createUpdateAfterBindLayout
        // (PipelineUtilities.cpp) stamps only eUpdateAfterBind — it has never set
        // descriptorBindingPartiallyBound. What actually makes it legal is that the shader declares
        // bindings 3-5 only under #ifdef SVT_ENABLED, so with SVT off they are not STATICALLY USED,
        // and Vulkan permits a bound-but-statically-unused binding to be unwritten. Bindings 6 and 7
        // below are compiled unconditionally and therefore really must always point at a live
        // buffer. VK-1620's 8/9 follow the SVT pattern, not the toon/wind one.
        for (uint32_t b = 3; b <= 5; ++b)
        {
            vk::DescriptorSetLayoutBinding sb{};
            sb.binding = b;
            sb.descriptorType = vk::DescriptorType::eStorageBuffer;
            sb.descriptorCount = 1;
            sb.stageFlags = vk::ShaderStageFlagBits::eFragment;
            bindings.push_back(sb);
        }

        // VK-1493: toon profile table SSBO (binding 6), fragment stage. Unlike SVT
        // (3-5), the toon branch is compiled unconditionally into the ubershader, so
        // binding 6 is STATICALLY USED on every draw and there is no ePartiallyBound
        // flag here — it must always point at a live buffer. The renderer binds the
        // fixed 128-entry ToonProfileGpuTable buffer once (updateToonProfileDescriptor);
        // slot 0 holds a valid default from frame 0, so the binding is always valid.
        {
            vk::DescriptorSetLayoutBinding tb{};
            tb.binding = 6;
            tb.descriptorType = vk::DescriptorType::eStorageBuffer;
            tb.descriptorCount = 1;
            tb.stageFlags = vk::ShaderStageFlagBits::eFragment;
            bindings.push_back(tb);
        }

        // VK-1580: foliage wind. Binding 7 = the global wind buffer (GPUWindData, reused
        // from the grass WindSystem), read in the MESH stage. Bound as a STORAGE buffer so
        // it uses the same update-after-bind storage path as toon binding 6 (createUpdate-
        // AfterBindLayout stamps eUpdateAfterBind on every binding, so a uniform buffer here
        // would add a uniform-buffer-UAB device-feature dependency; storage avoids it — the
        // std140/std430 layout of 2×vec4 is identical). Like toon, the wind branch is
        // compiled unconditionally, so binding 7 is STATICALLY USED and must always point at
        // a live buffer (no ePartiallyBound). The renderer binds the WindSystem buffer once
        // (updateWindDescriptor); it defaults to zero speed, so it's a valid no-op before
        // any wind is authored.
        {
            vk::DescriptorSetLayoutBinding wb{};
            wb.binding = 7;
            wb.descriptorType = vk::DescriptorType::eStorageBuffer;
            wb.descriptorCount = 1;
            wb.stageFlags = vk::ShaderStageFlagBits::eMeshEXT;
            bindings.push_back(wb);
        }

        // VK-1620: terrain RVT page table (8) and params (9), fragment stage — the plumbing that
        // lets a scene mesh blend into the terrain. Declared like SVT's 3-5 (always present in the
        // layout, referenced by the shader only under RVT_TERRAIN_BLEND_ENABLED) so the pipeline
        // can be recompiled for the toggle without rebuilding this set.
        //
        // These two are all that is needed: the RVT atlas planes are sampled through the bindless
        // heap, so nothing here is an image and the storage-only pool size below still covers the
        // whole set. That is what makes this fit at all — the scene mesh pipeline already binds up
        // to 16 descriptor sets and cannot afford another one.
        for (uint32_t b = 8; b <= 9; ++b)
        {
            vk::DescriptorSetLayoutBinding rb{};
            rb.binding = b;
            rb.descriptorType = vk::DescriptorType::eStorageBuffer;
            rb.descriptorCount = 1;
            rb.stageFlags = vk::ShaderStageFlagBits::eFragment;
            bindings.push_back(rb);
        }

        perDrawDataLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = static_cast<uint32_t>(bindings.size());

        perDrawDataPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 1, &poolSize, 1);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = perDrawDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &perDrawDataLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        perDrawDataDescriptorSet = sets[0];

    }

    void MeshShaderPipeline::createMeshletDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        // Binding 4: Hi-Z texture for meshlet occlusion culling
        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        meshletDataLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 4;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 1;

        meshletDataPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 1, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()));

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = meshletDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &meshletDataLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        meshletDataDescriptorSet = sets[0];

        vk::DescriptorBufferInfo statsInfo{};
        statsInfo.buffer = statsBuffer;
        statsInfo.offset = 0;
        statsInfo.range = sizeof(MeshletCullingStats);

        vk::WriteDescriptorSet statsWrite{};
        statsWrite.dstSet = meshletDataDescriptorSet;
        statsWrite.dstBinding = 3;
        statsWrite.dstArrayElement = 0;
        statsWrite.descriptorCount = 1;
        statsWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        statsWrite.pBufferInfo = &statsInfo;

        vkDevice.updateDescriptorSets(statsWrite, {});

    }

    void MeshShaderPipeline::createVertexDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetLayoutBinding vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        vertexBinding.descriptorCount = 1;
        vertexBinding.stageFlags = vk::ShaderStageFlagBits::eMeshEXT;

        vertexDataLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, &vertexBinding, 1);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vertexDataPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 1, &poolSize, 1);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = vertexDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &vertexDataLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        vertexDataDescriptorSet = sets[0];

    }

    void MeshShaderPipeline::createMeshShaderGraphicsPipeline(const MeshPipelineInitInfo& info)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        meshShader = std::make_unique<core::Shader>(device);
        if (isWBOITMode)
        {
            meshShader->addMacroDefinition("WBOIT_ENABLED");
        }
        if (info.giProbeDataLayout)
        {
            meshShader->addMacroDefinition("GI_ENABLED");
        }
        if (info.causticLayout)
        {
            meshShader->addMacroDefinition("CAUSTICS_ENABLED");
            meshShader->addMacroDefinition("CAUSTIC_SET", "12");
        }
        if (info.rtShadowMaskLayout)
        {
            meshShader->addMacroDefinition("RT_SHADOW_ENABLED");
        }
        if (info.rtSpotShadowMaskLayout)
        {
            meshShader->addMacroDefinition("RT_SPOT_SHADOW_ENABLED");
        }
        if (info.rtPointShadowMaskLayout)
        {
            meshShader->addMacroDefinition("RT_POINT_SHADOW_ENABLED");
        }
        if (info.worldMaskLayout)
        {
            meshShader->addMacroDefinition("WORLD_MASK_ENABLED");
            meshShader->addMacroDefinition("WORLD_MASK_SET", info.giProbeDataLayout ? "14" : "11");
        }
        if (svtSampleEnabled)
        {
            meshShader->addMacroDefinition("SVT_ENABLED"); // VK-1209 material SVT (set-1 bindings 3/4/5)
        }
        if (rvtBlendEnabled)
        {
            // VK-1620 mesh-into-terrain blending (set-1 bindings 8/9 + the bindless atlas slots).
            meshShader->addMacroDefinition("RVT_TERRAIN_BLEND_ENABLED");
        }
        if (info.selectionCoverageLayout)
        {
            meshShader->addMacroDefinition("SELECTION_COVERAGE_ENABLED");
        }
        if (info.reflectionProbesEnabled)
        {
            // VK-1577. MAX_REFLECTION_PROBES is injected rather than hardcoded in GLSL so the
            // shader array length and the set-0 binding-4 descriptorCount cannot drift apart.
            meshShader->addMacroDefinition("REFLECTION_PROBES_ENABLED");
            meshShader->addMacroDefinition("MAX_REFLECTION_PROBES",
                                           std::to_string(probe::MAX_REFLECTION_PROBES));
        }
        meshShader->readShader("../../resources/shaders/gpudriven/task_gpudriven.glsl");
        meshShader->readShader("../../resources/shaders/gpudriven/mesh_shader_gpudriven.glsl");

        const auto& stages = meshShader->getShaderStages();
        if (stages.size() < 3)
        {
            vfLogError("MeshShaderPipeline: Failed to load shaders (need Task + Mesh + Fragment): {}",
                        meshShader->getLastCompilationError());
            return;
        }

        bool hasTask = false, hasMesh = false, hasFrag = false;
        for (const auto& stage : stages)
        {
            if (stage.stage == vk::ShaderStageFlagBits::eTaskEXT) hasTask = true;
            if (stage.stage == vk::ShaderStageFlagBits::eMeshEXT) hasMesh = true;
            if (stage.stage == vk::ShaderStageFlagBits::eFragment) hasFrag = true;
        }

        if (!hasTask || !hasMesh || !hasFrag)
        {
            vfLogError("MeshShaderPipeline: Missing shader stages (Task={}, Mesh={}, Fragment={})",
                        hasTask, hasMesh, hasFrag);
            return;
        }

        std::vector<vk::DescriptorSetLayout> setLayouts = {
            info.iblLayout,
            perDrawDataLayout,
            info.bindlessTextureLayout,
            meshletDataLayout,
            vertexDataLayout,
            info.boneMatrixLayout,
            info.lightDataLayout,
            info.clusterGridLayout,
            info.cullingOutputLayout,
            info.shadowDataLayout,
            info.shadowTextureLayout
        };

        auto ensureEmptyPlaceholder = [&]()
        {
            if (!emptyPlaceholderLayout)
            {
                vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
                emptyLayoutInfo.bindingCount = 0;
                emptyLayoutInfo.pBindings = nullptr;
                emptyPlaceholderLayout = vkDevice.createDescriptorSetLayout(emptyLayoutInfo);
            }
        };

        // Set 11: GI probes when present; otherwise the plugin world mask may take it
        // (WORLD_MASK_SET = 11). With GI present the world mask moves to set 14.
        const bool worldMaskAtSet11 = info.worldMaskLayout && !info.giProbeDataLayout;

        if (info.giProbeDataLayout)
        {
            setLayouts.push_back(info.giProbeDataLayout); // Set 11
        }
        else if (worldMaskAtSet11)
        {
            setLayouts.push_back(info.worldMaskLayout); // Set 11
        }

        if (info.causticLayout)
        {
            // Pad with empty placeholders so caustics always occupy set 12
            ensureEmptyPlaceholder();
            while (setLayouts.size() < 12)
                setLayouts.push_back(emptyPlaceholderLayout);
            setLayouts.push_back(info.causticLayout); // Set 12
        }

        // Set 13: shared RT shadow mask set. Directional (binding 0), spot (binding 1) and point
        // (binding 2) RT masks are collapsed into one set so sets 15/16 are free — the layout now
        // needs at most 14 bound sets, or 15 when the plugin world mask also sits at set 14 (GI on),
        // down from 17 — so spot/point RT work on 16-bound-set GPUs.
        // Present when any RT shadow type is online; the shader declares a binding only under its
        // RT_*_ENABLED macro (each binding is PARTIALLY_BOUND, so inactive ones may stay unwritten).
        const bool anyRTMask = info.rtShadowMaskLayout || info.rtSpotShadowMaskLayout ||
                               info.rtPointShadowMaskLayout;
        if (anyRTMask)
        {
            if (!rtMaskSet)
            {
                rtMaskSet = std::make_unique<raytracing::RTShadowMaskSet>(device);
                rtMaskSet->init();
            }
            // VK-1398: the (re)created ring slots are freshly allocated (no in-flight command buffer
            // references them yet), so mark all slots dirty and let ensureRTMaskSlot() repopulate each
            // slot lazily from the cached producers on its first bind. A bound-but-unwritten binding
            // whose RT_*_ENABLED macro is active would otherwise be sampled as undefined.
            markAllRTMaskSlotsDirty();
            ensureEmptyPlaceholder();
            while (setLayouts.size() < 13)
                setLayouts.push_back(emptyPlaceholderLayout);
            setLayouts.push_back(rtMaskSet->getLayout()); // Set 13
            rtMaskBound = true;
        }
        else
        {
            rtMaskBound = false;
        }

        if (info.worldMaskLayout && !worldMaskAtSet11)
        {
            // GI occupies set 11 — world mask goes to set 14 (after the RT mask set's 13)
            ensureEmptyPlaceholder();
            while (setLayouts.size() < 14)
                setLayouts.push_back(emptyPlaceholderLayout);
            setLayouts.push_back(info.worldMaskLayout); // Set 14
        }
        worldMaskLayoutBound = info.worldMaskLayout != nullptr;
        worldMaskSetIndex = worldMaskLayoutBound ? (worldMaskAtSet11 ? 11u : 14u) : 0u;

        if (info.selectionCoverageLayout)
        {
            ensureEmptyPlaceholder();
            while (setLayouts.size() < 15)
                setLayouts.push_back(emptyPlaceholderLayout);
            setLayouts.push_back(info.selectionCoverageLayout); // Set 15
        }

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT |
            vk::ShaderStageFlagBits::eMeshEXT |
            vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(MeshShaderPushConstants);

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 1;
        layoutCreateInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        bool isTransparent = isTransparentMode || isWBOITMode;

        core::MeshShaderPipelineConfig config{
            .device = vkDevice,
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = info.colorAttachmentFormats,
            .depthAttachmentFormat = info.depthAttachmentFormat,
            .shaderStages = stages,
            .existingPipelineLayout = pipelineLayout,
            .cullMode = isTransparent ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
            .polygonMode = isWireframeMode ? vk::PolygonMode::eLine : vk::PolygonMode::eFill,
            .depthTestEnable = true,
            .depthWriteEnable = !isTransparent,
            .depthCompareOp = isWBOITMode ? vk::CompareOp::eLessOrEqual : vk::CompareOp::eLess,
            .blendEnable = isTransparentMode && !isWBOITMode,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        };
        config.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

        // Dynamic MSAA: this pipeline is reused across the MSAA main viewport, the
        // single-sample WBOIT pass, and RenderTexture/preview viewports — each pass
        // sets its own sample count via cmd.setRasterizationSamplesEXT().
        config.dynamicSampleCount = true;

        if (isWBOITMode)
        {
            // WBOIT dual-attachment blend states
            vk::PipelineColorBlendAttachmentState accumBlend{};
            accumBlend.blendEnable = VK_TRUE;
            accumBlend.srcColorBlendFactor = vk::BlendFactor::eOne;
            accumBlend.dstColorBlendFactor = vk::BlendFactor::eOne;
            accumBlend.colorBlendOp = vk::BlendOp::eAdd;
            accumBlend.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            accumBlend.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            accumBlend.alphaBlendOp = vk::BlendOp::eAdd;
            accumBlend.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                        vk::ColorComponentFlagBits::eG |
                                        vk::ColorComponentFlagBits::eB |
                                        vk::ColorComponentFlagBits::eA;

            vk::PipelineColorBlendAttachmentState revealageBlend{};
            revealageBlend.blendEnable = VK_TRUE;
            revealageBlend.srcColorBlendFactor = vk::BlendFactor::eZero;
            revealageBlend.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcColor;
            revealageBlend.colorBlendOp = vk::BlendOp::eAdd;
            revealageBlend.srcAlphaBlendFactor = vk::BlendFactor::eZero;
            revealageBlend.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            revealageBlend.alphaBlendOp = vk::BlendOp::eAdd;
            revealageBlend.colorWriteMask = vk::ColorComponentFlagBits::eR;

            config.colorBlendAttachments = {accumBlend, revealageBlend};
        }

        try
        {
            auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
            graphicsPipeline = result.pipeline;
        }
        catch (const std::exception& e)
        {
            vfLogError("MeshShaderPipeline: Failed to create pipeline - {}", e.what());
        }
    }

    void MeshShaderPipeline::resetStats(vk::CommandBuffer cmd)
    {
        cmd.fillBuffer(statsBuffer, 0, sizeof(MeshletCullingStats), 0);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = statsBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(MeshletCullingStats);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTaskShaderEXT,
            {},
            {},
            barrier,
            {});
    }

    MeshletCullingStats MeshShaderPipeline::readStats()
    {
        if (!statsBuffer)
        {
            return cachedStats;
        }

        device.waitGraphicsIdle();

        vk::Device vkDevice = device.getLogicalDevice();

        std::memcpy(&cachedStats, statsBufferAllocation.mappedPtr, sizeof(MeshletCullingStats));

        return cachedStats;
    }
}
