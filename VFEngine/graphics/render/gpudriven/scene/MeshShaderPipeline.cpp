#include "MeshShaderPipeline.hpp"
#include "MeshletBuffer.hpp"
#include "MergedMeshBuffer.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <array>

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
        cachedSVTLayout = info.svtLayout;

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

        core::BufferUtilities::createBuffer(request, statsBuffer, statsBufferMemory);

        void* data = vkDevice.mapMemory(statsBufferMemory, 0, sizeof(MeshletCullingStats));
        std::memset(data, 0, sizeof(MeshletCullingStats));
        vkDevice.unmapMemory(statsBufferMemory);

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

        core::BufferUtilities::destroyBuffer(vkDevice, statsBuffer, statsBufferMemory);

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
        cachedSVTLayout = info.svtLayout;

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

    void MeshShaderPipeline::createPerDrawDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

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

        // Binding 2: GPUObjectData SSBO (read by task shader for per-instance LOD + culling)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        perDrawDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 3;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        perDrawDataPool = vkDevice.createDescriptorPool(poolInfo);

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

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        meshletDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 4;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        meshletDataPool = vkDevice.createDescriptorPool(poolInfo);

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

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &vertexBinding;

        vertexDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        vertexDataPool = vkDevice.createDescriptorPool(poolInfo);

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
        if (info.svtLayout)
        {
            meshShader->addMacroDefinition("SVT_ENABLED");
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

        if (info.giProbeDataLayout)
        {
            setLayouts.push_back(info.giProbeDataLayout);
        }

        if (info.svtLayout)
        {
            // Ensure SVT layout is at set 12 (pad with empty layouts if GI not present)
            while (setLayouts.size() < 12)
            {
                setLayouts.push_back(info.iblLayout); // Placeholder for missing sets
            }
            setLayouts.push_back(info.svtLayout);
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
            .renderPass = info.renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = stages,
            .existingPipelineLayout = pipelineLayout,
            .cullMode = isTransparent ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = !isTransparent,
            .depthCompareOp = isWBOITMode ? vk::CompareOp::eLessOrEqual : vk::CompareOp::eLess,
            .blendEnable = isTransparentMode && !isWBOITMode,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha
        };
        config.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

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

        device.getGraphicsQueue().waitIdle();

        vk::Device vkDevice = device.getLogicalDevice();

        void* data = vkDevice.mapMemory(statsBufferMemory, 0, sizeof(MeshletCullingStats));
        std::memcpy(&cachedStats, data, sizeof(MeshletCullingStats));
        vkDevice.unmapMemory(statsBufferMemory);

        return cachedStats;
    }
}
