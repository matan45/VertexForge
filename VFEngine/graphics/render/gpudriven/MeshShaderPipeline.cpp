#include "MeshShaderPipeline.hpp"
#include "MeshletBuffer.hpp"
#include "MergedMeshBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"
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

    void MeshShaderPipeline::init(vk::DescriptorSetLayout iblLayout,
                                  vk::DescriptorSetLayout bindlessTextureLayout,
                                  vk::DescriptorSetLayout boneMatrixLayout,
                                  vk::DescriptorSetLayout lightDataLayout,
                                  vk::DescriptorSetLayout clusterGridLayout,
                                  vk::DescriptorSetLayout cullingOutputLayout,
                                  vk::DescriptorSetLayout shadowDataLayout,
                                  vk::DescriptorSetLayout shadowTextureLayout,
                                  vk::RenderPass renderPass,
                                  bool transparentMode,
                                  bool wboitMode)
    {
        isTransparentMode = transparentMode;
        isWBOITMode = wboitMode;
        cachedLightDataLayout = lightDataLayout;
        cachedClusterGridLayout = clusterGridLayout;
        cachedCullingOutputLayout = cullingOutputLayout;
        cachedShadowDataLayout = shadowDataLayout;
        cachedShadowTextureLayout = shadowTextureLayout;

        createStatsBuffer();
        createPerDrawDataDescriptor();
        createMeshletDataDescriptor();
        createVertexDataDescriptor();
        createMeshShaderGraphicsPipeline(iblLayout, bindlessTextureLayout, boneMatrixLayout,
                                         lightDataLayout, clusterGridLayout, cullingOutputLayout,
                                         shadowDataLayout, shadowTextureLayout, renderPass);
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

        loggerInfo("MeshShaderPipeline: Created culling stats buffer");
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

    void MeshShaderPipeline::recreate(vk::DescriptorSetLayout iblLayout,
                                      vk::DescriptorSetLayout bindlessTextureLayout,
                                      vk::DescriptorSetLayout boneMatrixLayout,
                                      vk::DescriptorSetLayout lightDataLayout,
                                      vk::DescriptorSetLayout clusterGridLayout,
                                      vk::DescriptorSetLayout cullingOutputLayout,
                                      vk::DescriptorSetLayout shadowDataLayout,
                                      vk::DescriptorSetLayout shadowTextureLayout,
                                      vk::RenderPass renderPass,
                                      bool transparentMode,
                                      bool wboitMode)
    {
        isTransparentMode = transparentMode;
        isWBOITMode = wboitMode;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        cachedLightDataLayout = lightDataLayout;
        cachedClusterGridLayout = clusterGridLayout;
        cachedCullingOutputLayout = cullingOutputLayout;
        cachedShadowDataLayout = shadowDataLayout;
        cachedShadowTextureLayout = shadowTextureLayout;

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

        createMeshShaderGraphicsPipeline(iblLayout, bindlessTextureLayout, boneMatrixLayout,
                                         lightDataLayout, clusterGridLayout, cullingOutputLayout,
                                         shadowDataLayout, shadowTextureLayout, renderPass);
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
            loggerWarning("Invalid shadow descriptor sets provided");
            return;
        }
        shadowDataDescriptorSet = shadowDataDescSet;
        shadowTextureDescriptorSet = shadowTextureDescSet;
    }

    void MeshShaderPipeline::createPerDrawDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetLayoutBinding perDrawBinding{};
        perDrawBinding.binding = 0;
        perDrawBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawBinding.descriptorCount = 1;
        perDrawBinding.stageFlags = vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eTaskEXT |
            vk::ShaderStageFlagBits::eMeshEXT |
            vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &perDrawBinding;

        perDrawDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

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

        loggerInfo("MeshShaderPipeline: Created per-draw data descriptor");
    }

    void MeshShaderPipeline::createMeshletDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

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

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        meshletDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 4;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

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

        loggerInfo("MeshShaderPipeline: Created meshlet data descriptor");
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

        loggerInfo("MeshShaderPipeline: Created vertex data descriptor");
    }

    void MeshShaderPipeline::createMeshShaderGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                                              vk::DescriptorSetLayout bindlessTextureLayout,
                                                              vk::DescriptorSetLayout boneMatrixLayout,
                                                              vk::DescriptorSetLayout lightDataLayout,
                                                              vk::DescriptorSetLayout clusterGridLayout,
                                                              vk::DescriptorSetLayout cullingOutputLayout,
                                                              vk::DescriptorSetLayout shadowDataLayout,
                                                              vk::DescriptorSetLayout shadowTextureLayout,
                                                              vk::RenderPass renderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        meshShader = std::make_unique<core::Shader>(device);
        if (isWBOITMode)
        {
            meshShader->addMacroDefinition("WBOIT_ENABLED");
        }
        meshShader->readShader("../../resources/shaders/gpudriven/task_gpudriven.glsl");
        meshShader->readShader("../../resources/shaders/gpudriven/mesh_shader_gpudriven.glsl");

        const auto& stages = meshShader->getShaderStages();
        if (stages.size() < 3)
        {
            loggerError("MeshShaderPipeline: Failed to load shaders (need Task + Mesh + Fragment): {}",
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
            loggerError("MeshShaderPipeline: Missing shader stages (Task={}, Mesh={}, Fragment={})",
                        hasTask, hasMesh, hasFrag);
            return;
        }

        std::array<vk::DescriptorSetLayout, 11> setLayouts = {
            iblLayout,
            perDrawDataLayout,
            bindlessTextureLayout,
            meshletDataLayout,
            vertexDataLayout,
            boneMatrixLayout,
            lightDataLayout,
            clusterGridLayout,
            cullingOutputLayout,
            shadowDataLayout,
            shadowTextureLayout
        };

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
            .renderPass = renderPass,
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
            loggerError("MeshShaderPipeline: Failed to create pipeline - {}", e.what());
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
