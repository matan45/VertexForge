#include "TerrainMeshShaderPipeline.hpp"
#include "TerrainMeshBuffer.hpp"
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
                                          vk::DescriptorSetLayout clusterGridLayout,
                                          vk::DescriptorSetLayout cullingOutputLayout,
                                          vk::DescriptorSetLayout shadowDataLayout,
                                          vk::DescriptorSetLayout shadowTextureLayout,
                                          vk::RenderPass renderPass)
    {
        cachedIBLLayout = iblLayout;
        cachedBindlessLayout = bindlessTextureLayout;
        cachedMeshletLayout = meshletDataLayout;
        cachedVertexLayout = vertexDataLayout;
        cachedLightDataLayout = lightDataLayout;
        cachedClusterGridLayout = clusterGridLayout;
        cachedCullingOutputLayout = cullingOutputLayout;
        cachedShadowDataLayout = shadowDataLayout;
        cachedShadowTextureLayout = shadowTextureLayout;

        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        transferCommandPool = device.getLogicalDevice().createCommandPool(poolInfo);

        vk::Device vkDevice = device.getLogicalDevice();
        vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
        emptyLayoutInfo.bindingCount = 0;
        emptyLayoutInfo.pBindings = nullptr;
        emptyLayout = vkDevice.createDescriptorSetLayout(emptyLayoutInfo);

        // Some Vulkan implementations require at least one pool size even for empty sets
        vk::DescriptorPoolSize dummyPoolSize{};
        dummyPoolSize.type = vk::DescriptorType::eUniformBuffer;
        dummyPoolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo emptyPoolInfo{};
        emptyPoolInfo.maxSets = 2;
        emptyPoolInfo.poolSizeCount = 1;
        emptyPoolInfo.pPoolSizes = &dummyPoolSize;
        emptyPoolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        emptyDescriptorPool = vkDevice.createDescriptorPool(emptyPoolInfo);

        std::array<vk::DescriptorSetLayout, 2> emptyLayouts = {emptyLayout, emptyLayout};
        vk::DescriptorSetAllocateInfo emptyAllocInfo{};
        emptyAllocInfo.descriptorPool = emptyDescriptorPool;
        emptyAllocInfo.descriptorSetCount = 2;
        emptyAllocInfo.pSetLayouts = emptyLayouts.data();
        auto emptySets = vkDevice.allocateDescriptorSets(emptyAllocInfo);
        emptyDescriptorSet1 = emptySets[0];
        emptyDescriptorSet5 = emptySets[1];

        if (!emptyDescriptorSet1 || !emptyDescriptorSet5)
        {
            loggerError("TerrainMeshShaderPipeline: Failed to allocate empty descriptor sets!");
        }

        createTileDataBuffer();
        createStatsBuffer();
        createTerrainDataDescriptor();
        createTerrainGraphicsPipeline(iblLayout, bindlessTextureLayout, meshletDataLayout,
                                      vertexDataLayout, lightDataLayout,
                                      clusterGridLayout, cullingOutputLayout,
                                      shadowDataLayout, shadowTextureLayout, renderPass);

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

        if (terrainBufferPool)
        {
            vkDevice.destroyDescriptorPool(terrainBufferPool);
            terrainBufferPool = nullptr;
        }

        if (emptyDescriptorPool)
        {
            vkDevice.destroyDescriptorPool(emptyDescriptorPool);
            emptyDescriptorPool = nullptr;
        }

        if (emptyLayout)
        {
            vkDevice.destroyDescriptorSetLayout(emptyLayout);
            emptyLayout = nullptr;
        }

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
        vk::DescriptorSetLayout clusterGridLayout,
        vk::DescriptorSetLayout cullingOutputLayout,
        vk::DescriptorSetLayout shadowDataLayout,
        vk::DescriptorSetLayout shadowTextureLayout,
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

        // Layout order (matching mesh_shader_gpudriven.glsl for sets 0-10):
        // Set 0: IBL/Camera
        // Set 1: (unused - reserved for per-draw)
        // Set 2: Bindless textures
        // Set 3: Meshlet data
        // Set 4: Vertex data
        // Set 5: (unused - reserved for bones)
        // Set 6: Light data (same as mesh shader!)
        // Set 7: Cluster grid params
        // Set 8: Cluster culling output
        // Set 9: Shadow data
        // Set 10: Shadow textures
        // Set 11: Terrain tile data (terrain-specific)

        std::array<vk::DescriptorSetLayout, 12> setLayouts = {
            iblLayout,              // Set 0: IBL/Camera
            emptyLayout,            // Set 1: (unused) - member variable
            bindlessTextureLayout,  // Set 2: Bindless textures
            meshletDataLayout,      // Set 3: Meshlet data
            vertexDataLayout,       // Set 4: Vertex data
            emptyLayout,            // Set 5: (unused - bones) - member variable
            lightDataLayout,        // Set 6: Light data (same as mesh shader)
            clusterGridLayout,      // Set 7: Cluster grid params
            cullingOutputLayout,    // Set 8: Cluster culling output
            shadowDataLayout,       // Set 9: Shadow data
            shadowTextureLayout,    // Set 10: Shadow textures
            terrainDataLayout       // Set 11: Terrain tile data
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
        vk::DeviceSize dataSize = currentTileCount * sizeof(TerrainTileGPUData);

        vk::Device vkDevice = device.getLogicalDevice();
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        core::BufferInfoRequest stagingRequest(vkDevice, device.getPhysicalDevice());
        stagingRequest.size = dataSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        void* data = vkDevice.mapMemory(stagingMemory, 0, dataSize);
        std::memcpy(data, tiles.data(), dataSize);
        vkDevice.unmapMemory(stagingMemory);

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

        core::BufferUtilities::destroyBuffer(vkDevice, stagingBuffer, stagingMemory);
    }

    void TerrainMeshShaderPipeline::updateTerrainBufferDescriptors(TerrainMeshBuffer& terrainBuffer)
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();

        if (!terrainBufferPool)
        {
            std::array<vk::DescriptorPoolSize, 1> poolSizes = {{
                {vk::DescriptorType::eStorageBuffer, 6} // meshlets, vertices, primitives, vertex data, index
            }};

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = 2;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();

            terrainBufferPool = vkDevice.createDescriptorPool(poolInfo);

            std::array<vk::DescriptorSetLayout, 2> layouts = {
                cachedMeshletLayout,
                cachedVertexLayout
            };

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = terrainBufferPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts.data();

            auto allocatedSets = vkDevice.allocateDescriptorSets(allocInfo);
            terrainMeshletDescriptorSet = allocatedSets[0];
            terrainVertexDescriptorSet = allocatedSets[1];
        }

        // Update meshlet descriptor set (set 3)
        {
            vk::DescriptorBufferInfo meshletInfo{};
            meshletInfo.buffer = terrainBuffer.getMeshletBuffer();
            meshletInfo.offset = 0;
            meshletInfo.range = VK_WHOLE_SIZE;

            vk::DescriptorBufferInfo vertexIndicesInfo{};
            vertexIndicesInfo.buffer = terrainBuffer.getMeshletVertexBuffer();
            vertexIndicesInfo.offset = 0;
            vertexIndicesInfo.range = VK_WHOLE_SIZE;

            vk::DescriptorBufferInfo primitivesInfo{};
            primitivesInfo.buffer = terrainBuffer.getMeshletPrimitiveBuffer();
            primitivesInfo.offset = 0;
            primitivesInfo.range = VK_WHOLE_SIZE;

            std::array<vk::WriteDescriptorSet, 3> writes{};
            writes[0].dstSet = terrainMeshletDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[0].pBufferInfo = &meshletInfo;

            writes[1].dstSet = terrainMeshletDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[1].pBufferInfo = &vertexIndicesInfo;

            writes[2].dstSet = terrainMeshletDescriptorSet;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[2].pBufferInfo = &primitivesInfo;

            vkDevice.updateDescriptorSets(writes, {});
        }

        // Update vertex descriptor set (set 4)
        {
            vk::DescriptorBufferInfo vertexInfo{};
            vertexInfo.buffer = terrainBuffer.getVertexBuffer();
            vertexInfo.offset = 0;
            vertexInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write{};
            write.dstSet = terrainVertexDescriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.pBufferInfo = &vertexInfo;

            vkDevice.updateDescriptorSets(write, {});
        }
    }

    void TerrainMeshShaderPipeline::updateSharedDescriptors(vk::DescriptorSet iblDescSet,
                                                            vk::DescriptorSet bindlessDescSet,
                                                            vk::DescriptorSet lightDataDescSet,
                                                            vk::DescriptorSet clusterGridDescSet,
                                                            vk::DescriptorSet cullingOutputDescSet,
                                                            vk::DescriptorSet shadowDataDescSet,
                                                            vk::DescriptorSet shadowTextureDescSet)
    {
        iblDescriptorSet = iblDescSet;
        bindlessDescriptorSet = bindlessDescSet;
        lightDataDescriptorSet = lightDataDescSet;
        clusterGridDescriptorSet = clusterGridDescSet;
        cullingOutputDescriptorSet = cullingOutputDescSet;
        shadowDataDescriptorSet = shadowDataDescSet;
        shadowTextureDescriptorSet = shadowTextureDescSet;
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
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        std::array<vk::DescriptorSet, 12> currentSets = {
            iblDescriptorSet,              // Set 0: IBL/Camera
            emptyDescriptorSet1,           // Set 1: Empty
            bindlessDescriptorSet,         // Set 2: Bindless textures
            terrainMeshletDescriptorSet,   // Set 3: Meshlet data
            terrainVertexDescriptorSet,    // Set 4: Vertex data
            emptyDescriptorSet5,           // Set 5: Empty
            lightDataDescriptorSet,        // Set 6: Light data
            clusterGridDescriptorSet,      // Set 7: Cluster grid
            cullingOutputDescriptorSet,    // Set 8: Culling output
            shadowDataDescriptorSet,       // Set 9: Shadow data
            shadowTextureDescriptorSet,    // Set 10: Shadow textures
            terrainDataDescriptorSet       // Set 11: Terrain tile data
        };

        // Check for critical missing descriptor sets
        // The shader statically uses ALL these sets, so ALL must be bound
        bool hasCriticalMissing = false;
        static bool warnedMissing = false;

        if (!terrainDataDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: terrainDataDescriptorSet (set 11) is NULL!"); }
        if (!lightDataDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: lightDataDescriptorSet (set 6) is NULL!"); }
        if (!iblDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: iblDescriptorSet (set 0) is NULL!"); }
        if (!bindlessDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: bindlessDescriptorSet (set 2) is NULL!"); }
        if (!terrainMeshletDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: terrainMeshletDescriptorSet (set 3) is NULL!"); }
        if (!terrainVertexDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: terrainVertexDescriptorSet (set 4) is NULL!"); }
        if (!emptyDescriptorSet1) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: emptyDescriptorSet1 (set 1) is NULL!"); }
        if (!emptyDescriptorSet5) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: emptyDescriptorSet5 (set 5) is NULL!"); }
        if (!clusterGridDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: clusterGridDescriptorSet (set 7) is NULL!"); }
        if (!cullingOutputDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: cullingOutputDescriptorSet (set 8) is NULL!"); }
        if (!shadowDataDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: shadowDataDescriptorSet (set 9) is NULL!"); }
        if (!shadowTextureDescriptorSet) { hasCriticalMissing = true; if (!warnedMissing) loggerWarning("TerrainMeshShaderPipeline: shadowTextureDescriptorSet (set 10) is NULL!"); }

        warnedMissing = true;

        if (hasCriticalMissing)
        {
            static bool warnedAbort = false;
            if (!warnedAbort)
            {
                loggerWarning("TerrainMeshShaderPipeline: Aborting dispatch - missing critical descriptor sets. Ensure shadow system is initialized.");
                warnedAbort = true;
            }
            return;
        }

        // Bind descriptor sets in contiguous batches
        // IMPORTANT: When we hit a null set, we must flush the current batch
        // because vkCmdBindDescriptorSets binds CONTIGUOUS sets starting from firstSet.
        // We cannot skip slots - if set 9 is null, we bind [0-8] then [10-11] separately.
        uint32_t batchStart = 0;
        std::vector<vk::DescriptorSet> batch;
        batch.reserve(12);

        auto flushBatch = [&]() {
            if (!batch.empty())
            {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                       batchStart, batch, {});
                batch.clear();
            }
        };

        for (uint32_t i = 0; i < 12; ++i)
        {
            vk::DescriptorSet current = currentSets[i];

            // Null sets break the contiguous batch - must flush and skip
            if (!current)
            {
                flushBatch();
                batchStart = i + 1;
                continue;
            }

            if (batch.empty())
            {
                batchStart = i;
            }
            batch.push_back(current);
        }
        flushBatch();

        TerrainPushConstants pushConstants{};
        pushConstants.tileCount = currentTileCount;

        uint32_t effectiveViewMode = viewMode;
        if (frustumCullingEnabled)
        {
            effectiveViewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        }
        if (meshletCullingEnabled)
        {
            effectiveViewMode |= TERRAIN_CULL_BACKFACE_BIT;
        }
        if (debugForceLOD0)
        {
            effectiveViewMode |= TERRAIN_DEBUG_FORCE_LOD0_BIT;
        }
        pushConstants.viewMode = effectiveViewMode;
        pushConstants.screenWidth = screenWidth;
        pushConstants.screenHeight = screenHeight;
        pushConstants.lodBias = lodBias;
        pushConstants.errorThreshold = errorThreshold;
        pushConstants.terrainTextureScale = textureScale;
        pushConstants.padding = 0.0f;
        pushConstants.brushWorldPos = brushWorldPos_;
        pushConstants.brushWorldRadius = brushWorldRadius_;
        pushConstants.brushFalloff = brushFalloff_;
        pushConstants.brushShape = brushShape_;
        pushConstants._pad1 = 0.0f;
        pushConstants._pad2 = 0.0f;
        pushConstants._pad3 = 0.0f;
        pushConstants.viewProjection = viewProjection_;

        cmd.pushConstants(pipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT |
                          vk::ShaderStageFlagBits::eMeshEXT |
                          vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(TerrainPushConstants), &pushConstants);

        // Dispatch mesh tasks - one workgroup per tile
        cmd.drawMeshTasksEXT(currentTileCount, 1, 1);
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
