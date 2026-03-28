#include "TerrainMeshShaderPipeline.hpp"
#include "TerrainMeshBuffer.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include <array>

namespace
{
    void writeStorageBufferDescriptor(vk::Device vkDevice, vk::DescriptorSet set,
                                      uint32_t binding, vk::Buffer buffer, vk::DeviceSize range = VK_WHOLE_SIZE)
    {
        vk::DescriptorBufferInfo info{buffer, 0, range};
        vk::WriteDescriptorSet write{};
        write.dstSet = set; write.dstBinding = binding;
        write.descriptorCount = 1; write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.pBufferInfo = &info;
        vkDevice.updateDescriptorSets(write, {});
    }

    void writeTerrainDataDescriptors(vk::Device vkDevice, vk::DescriptorSet descriptorSet,
                                     vk::Buffer tileDataBuffer, vk::Buffer statsBuffer)
    {
        writeStorageBufferDescriptor(vkDevice, descriptorSet, 0, tileDataBuffer);
        writeStorageBufferDescriptor(vkDevice, descriptorSet, 1, statsBuffer,
                                     sizeof(render::gpudriven::TerrainCullingStats));
    }

    void writeMeshletDescriptors(vk::Device vkDevice, vk::DescriptorSet descriptorSet,
                                 render::gpudriven::TerrainMeshBuffer& terrainBuffer)
    {
        writeStorageBufferDescriptor(vkDevice, descriptorSet, 0, terrainBuffer.getMeshletBuffer());
        writeStorageBufferDescriptor(vkDevice, descriptorSet, 1, terrainBuffer.getMeshletVertexBuffer());
        writeStorageBufferDescriptor(vkDevice, descriptorSet, 2, terrainBuffer.getMeshletPrimitiveBuffer());
    }

    void writeVertexDescriptor(vk::Device vkDevice, vk::DescriptorSet descriptorSet,
                               render::gpudriven::TerrainMeshBuffer& terrainBuffer)
    {
        writeStorageBufferDescriptor(vkDevice, descriptorSet, 0, terrainBuffer.getVertexBuffer());
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
            vfLogError("TerrainMeshShaderPipeline: Failed to allocate empty descriptor set!");
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

        weightMapLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 2;

        weightMapPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, &poolSize, 1);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = weightMapPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &weightMapLayout;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        weightMapDescriptorSet = sets[0];
    }

    void TerrainMeshShaderPipeline::createTerrainLayerBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        constexpr vk::DeviceSize layerBufferSize = terrain::MAX_TERRAIN_LAYERS * sizeof(TerrainLayerGPUData);

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = layerBufferSize;
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, terrainLayerBuffer, terrainLayerBufferAllocation, device.getMemoryManager());

        terrainLayerBufferMapped = terrainLayerBufferAllocation.mappedPtr;
        std::memset(terrainLayerBufferMapped, 0, layerBufferSize);

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = terrainLayerBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = layerBufferSize;

        vk::WriteDescriptorSet write{};
        write.dstSet = weightMapDescriptorSet;
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
        createSVTDescriptorLayout();
        createTerrainLayerBuffer();
        createTileDataBuffer();
        createStatsBuffer();
        createTerrainDataDescriptor();
        createTerrainGraphicsPipeline(iblLayout, bindlessTextureLayout, meshletDataLayout,
                                      vertexDataLayout, lightDataLayout,
                                      clusterGridLayout, cullingOutputLayout,
                                      shadowDataLayout, shadowTextureLayout, renderPass);

        initialized = true;
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

        vfLogInfo("TerrainMeshShaderPipeline: Recreated pipeline with updated viewport");
    }

    void TerrainMeshShaderPipeline::cleanupDescriptorResources()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (terrainBufferPool) { vkDevice.destroyDescriptorPool(terrainBufferPool); terrainBufferPool = nullptr; }
        if (weightMapPool) { vkDevice.destroyDescriptorPool(weightMapPool); weightMapPool = nullptr; }
        if (weightMapLayout) { vkDevice.destroyDescriptorSetLayout(weightMapLayout); weightMapLayout = nullptr; }
        if (emptyDescriptorPool) { vkDevice.destroyDescriptorPool(emptyDescriptorPool); emptyDescriptorPool = nullptr; }
        if (emptyLayout) { vkDevice.destroyDescriptorSetLayout(emptyLayout); emptyLayout = nullptr; }
        if (terrainDataPool) { vkDevice.destroyDescriptorPool(terrainDataPool); terrainDataPool = nullptr; }
        if (terrainDataLayout) { vkDevice.destroyDescriptorSetLayout(terrainDataLayout); terrainDataLayout = nullptr; }
        if (svtPool) { vkDevice.destroyDescriptorPool(svtPool); svtPool = nullptr; }
        if (svtLayout) { vkDevice.destroyDescriptorSetLayout(svtLayout); svtLayout = nullptr; }
    }

    void TerrainMeshShaderPipeline::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (terrainShader) { terrainShader->cleanUp(); terrainShader.reset(); }
        if (graphicsPipeline) { vkDevice.destroyPipeline(graphicsPipeline); graphicsPipeline = nullptr; }
        if (pipelineLayout) { vkDevice.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }

        tileDataBufferMapped = nullptr;
        core::BufferUtilities::destroyBuffer(vkDevice, tileDataBuffer, tileDataBufferAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, statsBuffer, statsBufferAllocation, device.getMemoryManager());

        terrainLayerBufferMapped = nullptr;
        core::BufferUtilities::destroyBuffer(vkDevice, terrainLayerBuffer, terrainLayerBufferAllocation, device.getMemoryManager());

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

        core::BufferUtilities::createBuffer(request, tileDataBuffer, tileDataBufferAllocation, device.getMemoryManager());

        tileDataBufferMapped = tileDataBufferAllocation.mappedPtr;
        std::memset(tileDataBufferMapped, 0, bufferSize);

        vfLogInfo("TerrainMeshShaderPipeline: Created tile data buffer for {} tiles ({} bytes)",
                   maxTileCount, bufferSize);
    }

    void TerrainMeshShaderPipeline::createStatsBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = sizeof(TerrainCullingStats);
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, statsBuffer, statsBufferAllocation, device.getMemoryManager());

        std::memset(statsBufferAllocation.mappedPtr, 0, sizeof(TerrainCullingStats));
    }

    void TerrainMeshShaderPipeline::createSVTDescriptorLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Set 12 layout for SVT (8 bindings):
        //   binding 0: page table SSBO
        //   binding 2: SVT params UBO
        //   binding 3-7: albedo, normal, ORM, emission, height cache sampler2DArray
        std::array<vk::DescriptorSetLayoutBinding, 7> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[1].binding = 2;
        bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        for (uint32_t i = 0; i < 5; ++i)
        {
            bindings[2 + i].binding = 3 + i;
            bindings[2 + i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[2 + i].descriptorCount = 1;
            bindings[2 + i].stageFlags = vk::ShaderStageFlagBits::eFragment;
        }

        svtLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));
    }

    void TerrainMeshShaderPipeline::initSVTDescriptorSet(
        vk::Buffer pageTableBuffer, vk::Buffer svtParamsBuffer,
        const SVTCacheViews caches[5])
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (!svtPool)
        {
            std::array<vk::DescriptorPoolSize, 3> poolSizes = {{
                {vk::DescriptorType::eStorageBuffer, 1},
                {vk::DescriptorType::eUniformBuffer, 1},
                {vk::DescriptorType::eCombinedImageSampler, 5}
            }};

            svtPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()));

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = svtPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &svtLayout;
            svtDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        // Write descriptors
        vk::DescriptorBufferInfo pageTableInfo{pageTableBuffer, 0, VK_WHOLE_SIZE};
        vk::DescriptorBufferInfo paramsInfo{svtParamsBuffer, 0, VK_WHOLE_SIZE};
        std::array<vk::DescriptorImageInfo, 5> imageInfos{};
        for (int i = 0; i < 5; ++i)
        {
            imageInfos[i].sampler = caches[i].sampler;
            imageInfos[i].imageView = caches[i].view;
            imageInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        std::array<vk::WriteDescriptorSet, 7> writes{};

        writes[0].dstSet = svtDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &pageTableInfo;

        writes[1].dstSet = svtDescriptorSet;
        writes[1].dstBinding = 2;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[1].pBufferInfo = &paramsInfo;

        for (int i = 0; i < 5; ++i)
        {
            writes[2 + i].dstSet = svtDescriptorSet;
            writes[2 + i].dstBinding = 3 + i;
            writes[2 + i].descriptorCount = 1;
            writes[2 + i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[2 + i].pImageInfo = &imageInfos[i];
        }

        vkDevice.updateDescriptorSets(writes, {});
        svtEnabled = true;
        vfLogInfo("TerrainMeshShaderPipeline: SVT descriptor set initialized (5 channels)");
    }

    void TerrainMeshShaderPipeline::updateSVTParamsBuffer(vk::Buffer svtParamsBuffer)
    {
        if (!svtDescriptorSet) return;
        vk::DescriptorBufferInfo paramsInfo{svtParamsBuffer, 0, VK_WHOLE_SIZE};
        vk::WriteDescriptorSet write{};
        write.dstSet = svtDescriptorSet;
        write.dstBinding = 2;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &paramsInfo;
        device.getLogicalDevice().updateDescriptorSets(write, {});
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

        terrainDataLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 2;

        terrainDataPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, &poolSize, 1);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = terrainDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &terrainDataLayout;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        terrainDataDescriptorSet = sets[0];

        writeTerrainDataDescriptors(vkDevice, terrainDataDescriptorSet, tileDataBuffer, statsBuffer);
    }

    bool TerrainMeshShaderPipeline::loadTerrainShaders()
    {
        terrainShader = std::make_unique<core::Shader>(device);
        if (svtEnabled)
        {
            terrainShader->addMacroDefinition("SVT_ENABLED");
        }
        if (causticEnabled && cachedCausticLayout)
        {
            terrainShader->addMacroDefinition("CAUSTICS_ENABLED");
            terrainShader->addMacroDefinition("CAUSTIC_SET", "13");
        }
        terrainShader->readShader("../../resources/shaders/gpudriven/task_terrain.glsl");
        terrainShader->readShader("../../resources/shaders/gpudriven/mesh_terrain.glsl");

        const auto& stages = terrainShader->getShaderStages();
        if (stages.size() < 3)
        {
            vfLogError("TerrainMeshShaderPipeline: Failed to load shaders (need Task + Mesh + Fragment): {}",
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
            vfLogError("TerrainMeshShaderPipeline: Missing shader stages (Task={}, Mesh={}, Fragment={})",
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
        if (!loadTerrainShaders()) return;

        vk::Device vkDevice = device.getLogicalDevice();

        std::vector<vk::DescriptorSetLayout> setLayouts = {
            iblLayout, weightMapLayout, bindlessTextureLayout, meshletDataLayout,
            vertexDataLayout, emptyLayout, lightDataLayout, clusterGridLayout,
            cullingOutputLayout, shadowDataLayout, shadowTextureLayout, terrainDataLayout,
            svtLayout  // Set 12: SVT page table + params + cache textures
        };

        if (causticEnabled && cachedCausticLayout)
        {
            setLayouts.push_back(cachedCausticLayout); // Set 13
        }

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
            .polygonMode = wireframeMode ? vk::PolygonMode::eLine : vk::PolygonMode::eFill,
            .depthTestEnable = true,
            .depthWriteEnable = true
        };
        config.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

        try
        {
            auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
            graphicsPipeline = result.pipeline;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainMeshShaderPipeline: Failed to create pipeline - {}", e.what());
        }
    }

    void TerrainMeshShaderPipeline::updateTerrainBufferDescriptors(TerrainMeshBuffer& terrainBuffer)
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();

        if (!terrainBufferPool)
        {
            std::array<vk::DescriptorPoolSize, 2> poolSizes = {{
                {vk::DescriptorType::eStorageBuffer, 6},
                {vk::DescriptorType::eCombinedImageSampler, 1}
            }};

            terrainBufferPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 2, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()));

            std::array<vk::DescriptorSetLayout, 2> layouts = { cachedMeshletLayout, cachedVertexLayout };

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

    void TerrainMeshShaderPipeline::updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler)
    {
        if (!initialized || !terrainMeshletDescriptorSet) return;

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = hiZSampler;
        imageInfo.imageView = hiZView;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = terrainMeshletDescriptorSet;
        write.dstBinding = 4;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &imageInfo;

        device.getLogicalDevice().updateDescriptorSets(write, {});
    }
}
