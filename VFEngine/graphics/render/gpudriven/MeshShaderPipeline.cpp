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
                                  vk::RenderPass renderPass)
    {
        createStatsBuffer();
        createPerDrawDataDescriptor();
        createMeshletDataDescriptor();
        createVertexDataDescriptor();
        createMeshShaderGraphicsPipeline(iblLayout, bindlessTextureLayout, renderPass);
    }

    void MeshShaderPipeline::createStatsBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = sizeof(MeshletCullingStats);
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, statsBuffer, statsBufferMemory);

        // Initialize to zero
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

        // Stats buffer
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
                                      vk::RenderPass renderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

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

        createMeshShaderGraphicsPipeline(iblLayout, bindlessTextureLayout, renderPass);
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

        // Binding 0: GPUMeshlet[] buffer
        bufferInfos[0].buffer = meshletBuffer.getMeshletBuffer();
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        // Binding 1: Meshlet vertex indices buffer
        bufferInfos[1].buffer = meshletBuffer.getMeshletVertexBuffer();
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        // Binding 2: Meshlet primitive buffer
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
        // For now, use the single interleaved vertex buffer
        // The mesh shader will unpack position/normal/texcoord from the interleaved format
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

    void MeshShaderPipeline::createPerDrawDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create descriptor set layout for per-draw data (Set 1)
        // Accessed by Task, Mesh, Fragment, and Vertex shaders (Vertex for custom material pipelines)
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

        // 4 bindings: meshlet buffer, vertex indices, primitive indices, stats
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        // Binding 0: GPUMeshlet[] buffer
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

        // Binding 1: Meshlet vertex indices buffer
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;

        // Binding 2: Meshlet primitive buffer
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;

        // Binding 3: Culling stats buffer (read/write by task shader)
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

        loggerInfo("MeshShaderPipeline: Created meshlet data descriptor with stats buffer");
    }

    void MeshShaderPipeline::createVertexDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create descriptor set layout for vertex data (Set 4)
        // Single binding for interleaved vertex buffer
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
                                                              vk::RenderPass renderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        meshShader = std::make_unique<core::Shader>(device);
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

        std::array<vk::DescriptorSetLayout, 5> setLayouts = {
            iblLayout, // Set 0
            perDrawDataLayout, // Set 1
            bindlessTextureLayout, // Set 2
            meshletDataLayout, // Set 3
            vertexDataLayout // Set 4
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

        core::MeshShaderPipelineConfig config{
            .device = vkDevice,
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = stages,
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = true
        };

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

        // Ensure all GPU writes to stats buffer are complete before host read.
        // Task shader writes to this buffer; wait for graphics queue to finish.
        device.getGraphicsQueue().waitIdle();

        vk::Device vkDevice = device.getLogicalDevice();

        void* data = vkDevice.mapMemory(statsBufferMemory, 0, sizeof(MeshletCullingStats));
        std::memcpy(&cachedStats, data, sizeof(MeshletCullingStats));
        vkDevice.unmapMemory(statsBufferMemory);

        return cachedStats;
    }
}
