#include "TerrainMeshShaderPipeline.hpp"
#include "MeshletBuffer.hpp"
#include "MergedMeshBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"
#include <array>
#include <cstring>

namespace render::gpudriven
{
    TerrainMeshShaderPipeline::TerrainMeshShaderPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
    }

    TerrainMeshShaderPipeline::~TerrainMeshShaderPipeline()
    {
        cleanup();
    }

    void TerrainMeshShaderPipeline::init(vk::DescriptorSetLayout iblLayout,
                                          vk::DescriptorSetLayout bindlessTextureLayout,
                                          vk::DescriptorSetLayout meshletDataLayout,
                                          vk::DescriptorSetLayout vertexDataLayout,
                                          vk::DescriptorSetLayout lightDataLayout,
                                          vk::RenderPass renderPass)
    {
        cachedIBLLayout = iblLayout;
        cachedBindlessLayout = bindlessTextureLayout;
        cachedMeshletLayout = meshletDataLayout;
        cachedVertexLayout = vertexDataLayout;
        cachedLightDataLayout = lightDataLayout;

        // Create command pool for immediate transfers
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        transferCommandPool = device.getLogicalDevice().createCommandPool(poolInfo);

        createTileDataBuffer();
        createStatsBuffer();
        createTerrainDataDescriptor();
        createTerrainGraphicsPipeline(iblLayout, bindlessTextureLayout, meshletDataLayout,
                                      vertexDataLayout, lightDataLayout, renderPass);

        initialized = true;
        loggerInfo("TerrainMeshShaderPipeline: Initialized successfully");
    }

    void TerrainMeshShaderPipeline::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (terrainShader)
        {
            terrainShader->cleanUp();
            terrainShader.reset();
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

        core::BufferUtilities::destroyBuffer(vkDevice, tileDataBuffer, tileDataBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, statsBuffer, statsBufferMemory);

        if (terrainDataPool)
        {
            vkDevice.destroyDescriptorPool(terrainDataPool);
            terrainDataPool = nullptr;
        }
        if (terrainDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(terrainDataLayout);
            terrainDataLayout = nullptr;
        }

        if (transferCommandPool)
        {
            vkDevice.destroyCommandPool(transferCommandPool);
            transferCommandPool = nullptr;
        }

        initialized = false;
    }

    void TerrainMeshShaderPipeline::recreate(vk::DescriptorSetLayout iblLayout,
                                              vk::DescriptorSetLayout bindlessTextureLayout,
                                              vk::DescriptorSetLayout meshletDataLayout,
                                              vk::DescriptorSetLayout vertexDataLayout,
                                              vk::DescriptorSetLayout lightDataLayout,
                                              vk::RenderPass renderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        cachedIBLLayout = iblLayout;
        cachedBindlessLayout = bindlessTextureLayout;
        cachedMeshletLayout = meshletDataLayout;
        cachedVertexLayout = vertexDataLayout;
        cachedLightDataLayout = lightDataLayout;

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

        if (terrainShader)
        {
            terrainShader->cleanUp();
        }

        createTerrainGraphicsPipeline(iblLayout, bindlessTextureLayout, meshletDataLayout,
                                      vertexDataLayout, lightDataLayout, renderPass);
    }

    void TerrainMeshShaderPipeline::createTileDataBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DeviceSize bufferSize = maxTileCount * sizeof(TerrainTileGPUData);

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = bufferSize;
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::BufferUtilities::createBuffer(request, tileDataBuffer, tileDataBufferMemory);

        loggerInfo("TerrainMeshShaderPipeline: Created tile data buffer for {} tiles ({} bytes)",
                   maxTileCount, bufferSize);
    }

    void TerrainMeshShaderPipeline::createStatsBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = sizeof(TerrainCullingStats);
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, statsBuffer, statsBufferMemory);

        void* data = vkDevice.mapMemory(statsBufferMemory, 0, sizeof(TerrainCullingStats));
        std::memset(data, 0, sizeof(TerrainCullingStats));
        vkDevice.unmapMemory(statsBufferMemory);

        loggerInfo("TerrainMeshShaderPipeline: Created culling stats buffer");
    }

    void TerrainMeshShaderPipeline::createTerrainDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Binding 0: Terrain tile data buffer
        // Binding 1: Terrain stats buffer
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        terrainDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        terrainDataPool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = terrainDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &terrainDataLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        terrainDataDescriptorSet = sets[0];

        // Update descriptor with tile data buffer
        std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};

        bufferInfos[0].buffer = tileDataBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = statsBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = sizeof(TerrainCullingStats);

        std::array<vk::WriteDescriptorSet, 2> writes{};

        writes[0].dstSet = terrainDataDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &bufferInfos[0];

        writes[1].dstSet = terrainDataDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &bufferInfos[1];

        vkDevice.updateDescriptorSets(writes, {});

        loggerInfo("TerrainMeshShaderPipeline: Created terrain data descriptor");
    }

    void TerrainMeshShaderPipeline::createTerrainGraphicsPipeline(
        vk::DescriptorSetLayout iblLayout,
        vk::DescriptorSetLayout bindlessTextureLayout,
        vk::DescriptorSetLayout meshletDataLayout,
        vk::DescriptorSetLayout vertexDataLayout,
        vk::DescriptorSetLayout lightDataLayout,
        vk::RenderPass renderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        terrainShader = std::make_unique<core::Shader>(device);
        terrainShader->readShader("../../resources/shaders/gpudriven/task_terrain.glsl");
        terrainShader->readShader("../../resources/shaders/gpudriven/mesh_terrain.glsl");

        const auto& stages = terrainShader->getShaderStages();
        if (stages.size() < 3)
        {
            loggerError("TerrainMeshShaderPipeline: Failed to load shaders (need Task + Mesh + Fragment): {}",
                        terrainShader->getLastCompilationError());
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
            loggerError("TerrainMeshShaderPipeline: Missing shader stages (Task={}, Mesh={}, Fragment={})",
                        hasTask, hasMesh, hasFrag);
            return;
        }

        // Layout order (matching shader bindings):
        // Set 0: IBL/Camera
        // Set 1: (unused - reserved for per-draw)
        // Set 2: Bindless textures
        // Set 3: Meshlet data
        // Set 4: Vertex data
        // Set 5: (unused - reserved for bones)
        // Set 6: Terrain tile data
        // ... Sets 7-10 unused for now
        // Set 11: Light data

        // Create empty descriptor set layouts for unused sets
        vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
        emptyLayoutInfo.bindingCount = 0;
        emptyLayoutInfo.pBindings = nullptr;
        vk::DescriptorSetLayout emptyLayout = vkDevice.createDescriptorSetLayout(emptyLayoutInfo);

        std::array<vk::DescriptorSetLayout, 12> setLayouts = {
            iblLayout,              // Set 0: IBL/Camera
            emptyLayout,            // Set 1: (unused)
            bindlessTextureLayout,  // Set 2: Bindless textures
            meshletDataLayout,      // Set 3: Meshlet data
            vertexDataLayout,       // Set 4: Vertex data
            emptyLayout,            // Set 5: (unused - bones)
            terrainDataLayout,      // Set 6: Terrain tile data
            emptyLayout,            // Set 7: (unused)
            emptyLayout,            // Set 8: (unused)
            emptyLayout,            // Set 9: (unused)
            emptyLayout,            // Set 10: (unused)
            lightDataLayout         // Set 11: Light data
        };

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT |
            vk::ShaderStageFlagBits::eMeshEXT |
            vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainPushConstants);

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 1;
        layoutCreateInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        // Cleanup empty layout
        vkDevice.destroyDescriptorSetLayout(emptyLayout);

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
            loggerInfo("TerrainMeshShaderPipeline: Created graphics pipeline successfully");
        }
        catch (const std::exception& e)
        {
            loggerError("TerrainMeshShaderPipeline: Failed to create pipeline - {}", e.what());
        }
    }

    void TerrainMeshShaderPipeline::updateTileData(const std::vector<TerrainTileGPUData>& tiles)
    {
        if (tiles.empty())
        {
            currentTileCount = 0;
            return;
        }

        if (tiles.size() > maxTileCount)
        {
            loggerWarning("TerrainMeshShaderPipeline: Tile count {} exceeds max {}, truncating",
                          tiles.size(), maxTileCount);
        }

        currentTileCount = static_cast<uint32_t>(std::min(tiles.size(), static_cast<size_t>(maxTileCount)));

        static bool loggedOnce = false;
        if (!loggedOnce)
        {
            loggerInfo("TerrainMeshShaderPipeline::updateTileData: Setting currentTileCount={}", currentTileCount);
            loggedOnce = true;
        }
        vk::DeviceSize dataSize = currentTileCount * sizeof(TerrainTileGPUData);

        // Create staging buffer
        vk::Device vkDevice = device.getLogicalDevice();
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        core::BufferInfoRequest stagingRequest(vkDevice, device.getPhysicalDevice());
        stagingRequest.size = dataSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        // Copy data to staging buffer
        void* data = vkDevice.mapMemory(stagingMemory, 0, dataSize);
        std::memcpy(data, tiles.data(), dataSize);
        vkDevice.unmapMemory(stagingMemory);

        // Copy to device-local buffer using single-time command buffer
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = transferCommandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;

        auto cmdBuffers = vkDevice.allocateCommandBuffers(allocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = dataSize;
        cmd.copyBuffer(stagingBuffer, tileDataBuffer, copyRegion);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        device.getGraphicsQueue().submit(submitInfo, nullptr);
        device.getGraphicsQueue().waitIdle();

        vkDevice.freeCommandBuffers(transferCommandPool, cmd);

        // Cleanup staging buffer
        core::BufferUtilities::destroyBuffer(vkDevice, stagingBuffer, stagingMemory);
    }

    void TerrainMeshShaderPipeline::updateExternalDescriptors(vk::DescriptorSet iblDescSet,
                                                               vk::DescriptorSet bindlessDescSet,
                                                               vk::DescriptorSet meshletDescSet,
                                                               vk::DescriptorSet vertexDescSet,
                                                               vk::DescriptorSet lightDataDescSet)
    {
        iblDescriptorSet = iblDescSet;
        bindlessDescriptorSet = bindlessDescSet;
        meshletDescriptorSet = meshletDescSet;
        vertexDescriptorSet = vertexDescSet;
        lightDataDescriptorSet = lightDataDescSet;
    }

    void TerrainMeshShaderPipeline::dispatch(vk::CommandBuffer cmd,
                                              uint32_t viewMode,
                                              float screenWidth,
                                              float screenHeight,
                                              float lodBias,
                                              float errorThreshold,
                                              float textureScale)
    {
        if (!initialized || !graphicsPipeline || currentTileCount == 0)
        {
            static bool warnedOnce = false;
            if (!warnedOnce)
            {
                loggerWarning("TerrainMeshShaderPipeline::dispatch: Early exit - initialized={}, hasPipeline={}, tileCount={}",
                              initialized, (bool)graphicsPipeline, currentTileCount);
                warnedOnce = true;
            }
            return;
        }

        static bool loggedOnce = false;
        if (!loggedOnce)
        {
            loggerInfo("TerrainMeshShaderPipeline::dispatch: {} tiles, viewMode={}, screen={}x{}",
                       currentTileCount, viewMode, screenWidth, screenHeight);
            loggedOnce = true;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        // Bind descriptor sets
        std::array<vk::DescriptorSet, 7> descriptorSets = {
            iblDescriptorSet,           // Set 0
            vk::DescriptorSet{},        // Set 1 (unused)
            bindlessDescriptorSet,      // Set 2
            meshletDescriptorSet,       // Set 3
            vertexDescriptorSet,        // Set 4
            vk::DescriptorSet{},        // Set 5 (unused)
            terrainDataDescriptorSet    // Set 6
        };

        // Bind sets 0, 2-4, 6
        if (iblDescriptorSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                                   iblDescriptorSet, {});
        }
        if (bindlessDescriptorSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 2,
                                   bindlessDescriptorSet, {});
        }
        if (meshletDescriptorSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 3,
                                   meshletDescriptorSet, {});
        }
        if (vertexDescriptorSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 4,
                                   vertexDescriptorSet, {});
        }
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 6,
                               terrainDataDescriptorSet, {});
        if (lightDataDescriptorSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 11,
                                   lightDataDescriptorSet, {});
        }
        else
        {
            static bool warnedOnce = false;
            if (!warnedOnce)
            {
                loggerWarning("TerrainMeshShaderPipeline::dispatch: lightDataDescriptorSet is null - lighting will not work");
                warnedOnce = true;
            }
        }

        // Push constants
        TerrainPushConstants pushConstants{};
        pushConstants.tileCount = currentTileCount;

        // Add culling bits to viewMode based on enabled settings
        uint32_t effectiveViewMode = viewMode;
        if (frustumCullingEnabled)
        {
            effectiveViewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        }
        if (meshletCullingEnabled)
        {
            effectiveViewMode |= TERRAIN_CULL_BACKFACE_BIT;
        }
        pushConstants.viewMode = effectiveViewMode;
        pushConstants.screenWidth = screenWidth;
        pushConstants.screenHeight = screenHeight;
        pushConstants.lodBias = lodBias;
        pushConstants.errorThreshold = errorThreshold;
        pushConstants.terrainTextureScale = textureScale;
        pushConstants.padding = 0.0f;

        cmd.pushConstants(pipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT |
                          vk::ShaderStageFlagBits::eMeshEXT |
                          vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(TerrainPushConstants), &pushConstants);

        // Dispatch mesh tasks - one workgroup per tile
        cmd.drawMeshTasksEXT(currentTileCount, 1, 1);
    }

    void TerrainMeshShaderPipeline::resetStats(vk::CommandBuffer cmd)
    {
        cmd.fillBuffer(statsBuffer, 0, sizeof(TerrainCullingStats), 0);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = statsBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(TerrainCullingStats);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTaskShaderEXT,
            {},
            {},
            barrier,
            {});
    }

    TerrainCullingStats TerrainMeshShaderPipeline::readStats()
    {
        if (!statsBuffer)
        {
            return cachedStats;
        }

        device.getGraphicsQueue().waitIdle();

        vk::Device vkDevice = device.getLogicalDevice();

        void* data = vkDevice.mapMemory(statsBufferMemory, 0, sizeof(TerrainCullingStats));
        std::memcpy(&cachedStats, data, sizeof(TerrainCullingStats));
        vkDevice.unmapMemory(statsBufferMemory);

        return cachedStats;
    }
}
