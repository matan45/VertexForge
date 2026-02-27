#include "TerrainMeshShaderPipeline.hpp"
#include "TerrainMeshBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"
#include <array>

namespace
{
    void writeTerrainDataDescriptors(vk::Device vkDevice, vk::DescriptorSet descriptorSet,
                                     vk::Buffer tileDataBuffer, vk::Buffer statsBuffer)
    {
        std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};
        bufferInfos[0].buffer = tileDataBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = statsBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = sizeof(render::gpudriven::TerrainCullingStats);

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &bufferInfos[0];

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &bufferInfos[1];

        vkDevice.updateDescriptorSets(writes, {});
    }

    void writeMeshletDescriptors(vk::Device vkDevice, vk::DescriptorSet descriptorSet,
                                 render::gpudriven::TerrainMeshBuffer& terrainBuffer)
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
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &meshletInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &vertexIndicesInfo;

        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].pBufferInfo = &primitivesInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void writeVertexDescriptor(vk::Device vkDevice, vk::DescriptorSet descriptorSet,
                               render::gpudriven::TerrainMeshBuffer& terrainBuffer)
    {
        vk::DescriptorBufferInfo vertexInfo{};
        vertexInfo.buffer = terrainBuffer.getVertexBuffer();
        vertexInfo.offset = 0;
        vertexInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write{};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.pBufferInfo = &vertexInfo;

        vkDevice.updateDescriptorSets(write, {});
    }
}

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

    void TerrainMeshShaderPipeline::createEmptyDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
        emptyLayoutInfo.bindingCount = 0;
        emptyLayoutInfo.pBindings = nullptr;
        emptyLayout = vkDevice.createDescriptorSetLayout(emptyLayoutInfo);

        vk::DescriptorPoolSize dummyPoolSize{};
        dummyPoolSize.type = vk::DescriptorType::eUniformBuffer;
        dummyPoolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo emptyPoolInfo{};
        emptyPoolInfo.maxSets = 1;
        emptyPoolInfo.poolSizeCount = 1;
        emptyPoolInfo.pPoolSizes = &dummyPoolSize;
        emptyPoolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        emptyDescriptorPool = vkDevice.createDescriptorPool(emptyPoolInfo);

        vk::DescriptorSetAllocateInfo emptyAllocInfo{};
        emptyAllocInfo.descriptorPool = emptyDescriptorPool;
        emptyAllocInfo.descriptorSetCount = 1;
        emptyAllocInfo.pSetLayouts = &emptyLayout;
        auto emptySets = vkDevice.allocateDescriptorSets(emptyAllocInfo);
        emptyDescriptorSet5 = emptySets[0];

        if (!emptyDescriptorSet5)
        {
            loggerError("TerrainMeshShaderPipeline: Failed to allocate empty descriptor set!");
        }
    }

    void TerrainMeshShaderPipeline::createWeightMapDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        weightMapLayout_ = vkDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        weightMapPool_ = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = weightMapPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &weightMapLayout_;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        weightMapDescriptorSet_ = sets[0];
    }

    void TerrainMeshShaderPipeline::createTerrainLayerBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        constexpr vk::DeviceSize layerBufferSize = 16 * sizeof(TerrainLayerGPUData);

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = layerBufferSize;
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, terrainLayerBuffer_, terrainLayerBufferMemory_);

        terrainLayerBufferMapped_ = vkDevice.mapMemory(terrainLayerBufferMemory_, 0, layerBufferSize);
        std::memset(terrainLayerBufferMapped_, 0, layerBufferSize);

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = terrainLayerBuffer_;
        bufferInfo.offset = 0;
        bufferInfo.range = layerBufferSize;

        vk::WriteDescriptorSet write{};
        write.dstSet = weightMapDescriptorSet_;
        write.dstBinding = 1;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.pBufferInfo = &bufferInfo;

        vkDevice.updateDescriptorSets(write, {});
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

        createEmptyDescriptorSet();
        createWeightMapDescriptor();
        createTerrainLayerBuffer();
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

    void TerrainMeshShaderPipeline::recreate(vk::DescriptorSetLayout iblLayout,
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
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        cachedIBLLayout = iblLayout;
        cachedBindlessLayout = bindlessTextureLayout;
        cachedMeshletLayout = meshletDataLayout;
        cachedVertexLayout = vertexDataLayout;
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

        createTerrainGraphicsPipeline(iblLayout, bindlessTextureLayout,
                                      meshletDataLayout, vertexDataLayout,
                                      lightDataLayout, clusterGridLayout,
                                      cullingOutputLayout, shadowDataLayout,
                                      shadowTextureLayout, renderPass);

        loggerInfo("TerrainMeshShaderPipeline: Recreated pipeline with updated viewport");
    }

    void TerrainMeshShaderPipeline::cleanupDescriptorResources()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (terrainBufferPool)
        {
            vkDevice.destroyDescriptorPool(terrainBufferPool);
            terrainBufferPool = nullptr;
        }

        if (weightMapPool_)
        {
            vkDevice.destroyDescriptorPool(weightMapPool_);
            weightMapPool_ = nullptr;
        }

        if (weightMapLayout_)
        {
            vkDevice.destroyDescriptorSetLayout(weightMapLayout_);
            weightMapLayout_ = nullptr;
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

        if (tileDataBufferMapped_)
        {
            vkDevice.unmapMemory(tileDataBufferMemory);
            tileDataBufferMapped_ = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, tileDataBuffer, tileDataBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, statsBuffer, statsBufferMemory);

        if (terrainLayerBufferMapped_)
        {
            vkDevice.unmapMemory(terrainLayerBufferMemory_);
            terrainLayerBufferMapped_ = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, terrainLayerBuffer_, terrainLayerBufferMemory_);

        cleanupDescriptorResources();

        initialized = false;
    }

    void TerrainMeshShaderPipeline::createTileDataBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DeviceSize bufferSize = maxTileCount * sizeof(TerrainTileGPUData);

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = bufferSize;
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, tileDataBuffer, tileDataBufferMemory);

        tileDataBufferMapped_ = vkDevice.mapMemory(tileDataBufferMemory, 0, bufferSize);
        std::memset(tileDataBufferMapped_, 0, bufferSize);

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

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment;

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

        writeTerrainDataDescriptors(vkDevice, terrainDataDescriptorSet, tileDataBuffer, statsBuffer);

        loggerInfo("TerrainMeshShaderPipeline: Created terrain data descriptor");
    }

    bool TerrainMeshShaderPipeline::loadTerrainShaders()
    {
        terrainShader = std::make_unique<core::Shader>(device);
        terrainShader->readShader("../../resources/shaders/gpudriven/task_terrain.glsl");
        terrainShader->readShader("../../resources/shaders/gpudriven/mesh_terrain.glsl");

        const auto& stages = terrainShader->getShaderStages();
        if (stages.size() < 3)
        {
            loggerError("TerrainMeshShaderPipeline: Failed to load shaders (need Task + Mesh + Fragment): {}",
                        terrainShader->getLastCompilationError());
            return false;
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
            return false;
        }

        return true;
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
        if (!loadTerrainShaders())
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 12> setLayouts = {
            iblLayout,              // Set 0: IBL/Camera
            weightMapLayout_,       // Set 1: Weight map SSBO
            bindlessTextureLayout,  // Set 2: Bindless textures
            meshletDataLayout,      // Set 3: Meshlet data
            vertexDataLayout,       // Set 4: Vertex data
            emptyLayout,            // Set 5: (unused - bones)
            lightDataLayout,        // Set 6: Light data
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
            .shaderStages = terrainShader->getShaderStages(),
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = true
        };
        config.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

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

        std::memcpy(tileDataBufferMapped_, tiles.data(), dataSize);
    }

    void TerrainMeshShaderPipeline::updateTerrainBufferDescriptors(TerrainMeshBuffer& terrainBuffer)
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();

        if (!terrainBufferPool)
        {
            std::array<vk::DescriptorPoolSize, 1> poolSizes = {{
                {vk::DescriptorType::eStorageBuffer, 6}
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

        writeMeshletDescriptors(vkDevice, terrainMeshletDescriptorSet, terrainBuffer);
        writeVertexDescriptor(vkDevice, terrainVertexDescriptorSet, terrainBuffer);
    }

    void TerrainMeshShaderPipeline::updateWeightMapDescriptor(vk::Buffer weightMapBuffer)
    {
        if (!initialized || !weightMapDescriptorSet_ || !weightMapBuffer) return;

        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = weightMapBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write{};
        write.dstSet = weightMapDescriptorSet_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.pBufferInfo = &bufferInfo;

        vkDevice.updateDescriptorSets(write, {});
    }

    void TerrainMeshShaderPipeline::updateTerrainLayerInfo(const std::vector<TerrainLayerGPUData>& layers)
    {
        if (!terrainLayerBufferMapped_) return;

        constexpr uint32_t maxLayers = 16;
        if (layers.empty())
        {
            std::memset(terrainLayerBufferMapped_, 0, maxLayers * sizeof(TerrainLayerGPUData));
            return;
        }

        uint32_t count = static_cast<uint32_t>(std::min(layers.size(), static_cast<size_t>(maxLayers)));
        std::memcpy(terrainLayerBufferMapped_, layers.data(), count * sizeof(TerrainLayerGPUData));
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

    bool TerrainMeshShaderPipeline::validateDescriptorsForDispatch() const
    {
        const std::array<std::pair<vk::DescriptorSet, const char*>, 12> descriptors = {{
            {iblDescriptorSet, "iblDescriptorSet (set 0)"},
            {weightMapDescriptorSet_, "weightMapDescriptorSet_ (set 1)"},
            {bindlessDescriptorSet, "bindlessDescriptorSet (set 2)"},
            {terrainMeshletDescriptorSet, "terrainMeshletDescriptorSet (set 3)"},
            {terrainVertexDescriptorSet, "terrainVertexDescriptorSet (set 4)"},
            {emptyDescriptorSet5, "emptyDescriptorSet5 (set 5)"},
            {lightDataDescriptorSet, "lightDataDescriptorSet (set 6)"},
            {clusterGridDescriptorSet, "clusterGridDescriptorSet (set 7)"},
            {cullingOutputDescriptorSet, "cullingOutputDescriptorSet (set 8)"},
            {shadowDataDescriptorSet, "shadowDataDescriptorSet (set 9)"},
            {shadowTextureDescriptorSet, "shadowTextureDescriptorSet (set 10)"},
            {terrainDataDescriptorSet, "terrainDataDescriptorSet (set 11)"}
        }};

        static bool warnedMissing = false;
        bool hasCriticalMissing = false;

        for (const auto& [set, name] : descriptors)
        {
            if (!set)
            {
                hasCriticalMissing = true;
                if (!warnedMissing)
                {
                    loggerWarning("TerrainMeshShaderPipeline: {} is NULL!", name);
                }
            }
        }
        warnedMissing = true;

        if (hasCriticalMissing)
        {
            static bool warnedAbort = false;
            if (!warnedAbort)
            {
                loggerWarning("TerrainMeshShaderPipeline: Aborting dispatch - missing critical descriptor sets.");
                warnedAbort = true;
            }
        }

        return !hasCriticalMissing;
    }

    void TerrainMeshShaderPipeline::bindDescriptorSetsInBatches(
        vk::CommandBuffer cmd, const std::array<vk::DescriptorSet, 12>& currentSets) const
    {
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
    }

    TerrainPushConstants TerrainMeshShaderPipeline::buildTerrainPushConstants(
        uint32_t viewMode, float screenWidth, float screenHeight,
        float lodBias, float errorThreshold, float textureScale) const
    {
        TerrainPushConstants pc{};
        pc.tileCount = currentTileCount;

        uint32_t effectiveViewMode = viewMode;
        if (frustumCullingEnabled)
        {
            effectiveViewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        }
        if (meshletCullingEnabled)
        {
            effectiveViewMode |= TERRAIN_CULL_BACKFACE_BIT;
        }

        pc.viewMode = effectiveViewMode;
        pc.screenWidth = screenWidth;
        pc.screenHeight = screenHeight;
        pc.lodBias = lodBias;
        pc.errorThreshold = errorThreshold;
        pc.terrainTextureScale = textureScale;
        pc.terrainMaxDrawDistSq = terrainMaxDrawDistSq_;
        pc.brushWorldPos = brushWorldPos_;
        pc.brushWorldRadius = brushWorldRadius_;
        pc.brushFalloff = brushFalloff_;
        pc.brushShape = brushShape_;
        pc._pad1 = 0.0f;
        pc._pad2 = 0.0f;
        pc._pad3 = 0.0f;
        pc.viewProjection = viewProjection_;
        return pc;
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
            iblDescriptorSet,
            weightMapDescriptorSet_,
            bindlessDescriptorSet,
            terrainMeshletDescriptorSet,
            terrainVertexDescriptorSet,
            emptyDescriptorSet5,
            lightDataDescriptorSet,
            clusterGridDescriptorSet,
            cullingOutputDescriptorSet,
            shadowDataDescriptorSet,
            shadowTextureDescriptorSet,
            terrainDataDescriptorSet
        };

        if (!validateDescriptorsForDispatch())
        {
            return;
        }

        bindDescriptorSetsInBatches(cmd, currentSets);

        TerrainPushConstants pushConstants = buildTerrainPushConstants(
            viewMode, screenWidth, screenHeight, lodBias, errorThreshold, textureScale);

        cmd.pushConstants(pipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT |
                          vk::ShaderStageFlagBits::eMeshEXT |
                          vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(TerrainPushConstants), &pushConstants);

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
