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
                                          const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
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
        // Finding #15: only allocate the set-5 RVT descriptor + params UBO when RVT is actually enabled.
        // A runtime enable (setRVTSampleEnabled) creates it lazily; the layout path falls back to
        // emptyLayout while disabled, so nothing references it before it exists.
        if (rvtSampleEnabled)
            createRVTSampleDescriptor();
        createWeightMapDescriptor();
        createTerrainLayerBuffer();
        createTileDataBuffer();
        createStatsBuffer();
        createTerrainDataDescriptor();
        createTerrainGraphicsPipeline(iblLayout, bindlessTextureLayout, meshletDataLayout,
                                      vertexDataLayout, lightDataLayout,
                                      clusterGridLayout, cullingOutputLayout,
                                      shadowDataLayout, shadowTextureLayout, colorFormats, depthFormat);

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
                                              const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
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

        if (terrainShader)
        {
            terrainShader->cleanUp();
        }

        createTerrainGraphicsPipeline(iblLayout, bindlessTextureLayout,
                                      meshletDataLayout, vertexDataLayout,
                                      lightDataLayout, clusterGridLayout,
                                      cullingOutputLayout, shadowDataLayout,
                                      shadowTextureLayout, colorFormats, depthFormat);

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
        if (rvtSamplePool) { vkDevice.destroyDescriptorPool(rvtSamplePool); rvtSamplePool = nullptr; }
        if (rvtSampleLayout) { vkDevice.destroyDescriptorSetLayout(rvtSampleLayout); rvtSampleLayout = nullptr; }
        if (rvtParamsBuffer) { core::BufferUtilities::destroyBuffer(vkDevice, rvtParamsBuffer, rvtParamsAllocation, device.getMemoryManager()); }
        if (terrainDataPool) { vkDevice.destroyDescriptorPool(terrainDataPool); terrainDataPool = nullptr; }
        if (terrainDataLayout) { vkDevice.destroyDescriptorSetLayout(terrainDataLayout); terrainDataLayout = nullptr; }
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
        core::BufferUtilities::destroyBuffer(vkDevice, stampDummyBuffer, stampDummyAllocation, device.getMemoryManager());

        cleanupDescriptorResources();

        // Shared RT mask set (set 13): destroy its layout/pool/set; rebuilt + re-copied on next build.
        rtMaskSet.reset();
        rtMaskBound = false;

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

    void TerrainMeshShaderPipeline::createTerrainDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        // Binding 2: Stamp overlay heightmap (readonly, fragment only)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Bindings 3/4: plugin world mask sampler + params UBO (fragment only).
        // Written lazily on first bindWorldMask; statically unused until the
        // WORLD_MASK_ENABLED macro is compiled in, so they may stay unwritten.
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eFragment;

        terrainDataLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eStorageBuffer, 3};
        poolSizes[1] = {vk::DescriptorType::eCombinedImageSampler, 1};
        poolSizes[2] = {vk::DescriptorType::eUniformBuffer, 1};

        terrainDataPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, poolSizes.data(),
                                                                             static_cast<uint32_t>(poolSizes.size()));

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = terrainDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &terrainDataLayout;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        terrainDataDescriptorSet = sets[0];

        writeTerrainDataDescriptors(vkDevice, terrainDataDescriptorSet, tileDataBuffer, statsBuffer);

        // Create dedicated dummy buffer for stamp overlay binding 2
        core::BufferInfoRequest dummyRequest(vkDevice, device.getPhysicalDevice(),
            sizeof(float), vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::BufferUtilities::createBuffer(dummyRequest, stampDummyBuffer, stampDummyAllocation, device.getMemoryManager());
        writeStorageBufferDescriptor(vkDevice, terrainDataDescriptorSet, 2, stampDummyBuffer, sizeof(float));
    }

    void TerrainMeshShaderPipeline::updateWorldMaskResources(vk::ImageView maskView, vk::Sampler maskSampler,
                                                             vk::Buffer paramsBuffer, vk::DeviceSize paramsSize)
    {
        if (!terrainDataDescriptorSet || !maskView || !maskSampler || !paramsBuffer) return;

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = maskSampler;
        imageInfo.imageView = maskView;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = paramsBuffer;
        bufferInfo.range = paramsSize;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = terrainDataDescriptorSet;
        writes[0].dstBinding = 3;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &imageInfo;
        writes[1].dstSet = terrainDataDescriptorSet;
        writes[1].dstBinding = 4;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[1].pBufferInfo = &bufferInfo;

        device.getLogicalDevice().updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                                       writes.data(), 0, nullptr);
    }

    void TerrainMeshShaderPipeline::createRVTSampleDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // set 5: b0 page table (SSBO), b1 albedo atlas, b2 ORM atlas, b3 feedback (SSBO), b4 params (UBO).
        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
        for (uint32_t i = 0; i < 5; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
        }
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[4].descriptorType = vk::DescriptorType::eUniformBuffer;

        rvtSampleLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, bindings.data(),
                                                                              static_cast<uint32_t>(bindings.size()));

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eStorageBuffer, 2};
        poolSizes[1] = {vk::DescriptorType::eCombinedImageSampler, 2};
        poolSizes[2] = {vk::DescriptorType::eUniformBuffer, 1};
        rvtSamplePool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, poolSizes.data(),
                                                                           static_cast<uint32_t>(poolSizes.size()));

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = rvtSamplePool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &rvtSampleLayout;
        rvtSampleDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        // 64-byte RVTParams UBO (VTImageInfo + worldMin + invWorldExtent + virtualResTexels + pad).
        core::BufferInfoRequest req(vkDevice, device.getPhysicalDevice(), 64,
                                    vk::BufferUsageFlagBits::eUniformBuffer,
                                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(req, rvtParamsBuffer, rvtParamsAllocation, device.getMemoryManager());
    }

    void TerrainMeshShaderPipeline::updateRVTSampleResources(vk::Buffer pageTableBuffer, vk::ImageView albedoView,
                                                             vk::ImageView ormView, vk::Sampler sampler,
                                                             vk::Buffer feedbackBuffer, const void* params,
                                                             vk::DeviceSize paramsSize)
    {
        if (!rvtSampleDescriptorSet || !pageTableBuffer || !albedoView || !ormView || !sampler || !feedbackBuffer)
            return;

        if (params && rvtParamsAllocation.mappedPtr)
            std::memcpy(rvtParamsAllocation.mappedPtr, params, paramsSize > 64 ? 64 : static_cast<size_t>(paramsSize));

        vk::DescriptorBufferInfo ptInfo{pageTableBuffer, 0, VK_WHOLE_SIZE};
        vk::DescriptorImageInfo albedoInfo{sampler, albedoView, vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::DescriptorImageInfo ormInfo{sampler, ormView, vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::DescriptorBufferInfo fbInfo{feedbackBuffer, 0, VK_WHOLE_SIZE};
        vk::DescriptorBufferInfo paramInfo{rvtParamsBuffer, 0, 64};

        std::array<vk::WriteDescriptorSet, 5> writes{};
        writes[0].dstSet = rvtSampleDescriptorSet; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer; writes[0].pBufferInfo = &ptInfo;
        writes[1].dstSet = rvtSampleDescriptorSet; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler; writes[1].pImageInfo = &albedoInfo;
        writes[2].dstSet = rvtSampleDescriptorSet; writes[2].dstBinding = 2; writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler; writes[2].pImageInfo = &ormInfo;
        writes[3].dstSet = rvtSampleDescriptorSet; writes[3].dstBinding = 3; writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eStorageBuffer; writes[3].pBufferInfo = &fbInfo;
        writes[4].dstSet = rvtSampleDescriptorSet; writes[4].dstBinding = 4; writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eUniformBuffer; writes[4].pBufferInfo = &paramInfo;

        device.getLogicalDevice().updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    bool TerrainMeshShaderPipeline::loadTerrainShaders()
    {
        terrainShader = std::make_unique<core::Shader>(device);
        // All RT shadow masks share set 13 (directional binding 0, spot 1, point 2). Caustics keep
        // set 12 but, as before, are mutually exclusive with the RT-mask region: set 12 becomes a
        // placeholder when any RT type is on, so the caustic macro is suppressed in that case.
        const bool anyRT = anyRTMaskActive();
        if (rtShadowEnabled && rtShadowMaskLayout)
        {
            terrainShader->addMacroDefinition("RT_SHADOW_ENABLED");
        }
        else if (causticEnabled && cachedCausticLayout && !anyRT)
        {
            terrainShader->addMacroDefinition("CAUSTICS_ENABLED");
            terrainShader->addMacroDefinition("CAUSTIC_SET", "12");
        }
        if (worldMaskEnabled)
        {
            terrainShader->addMacroDefinition("WORLD_MASK_ENABLED");
        }
        if (rvtSampleEnabled)
        {
            terrainShader->addMacroDefinition("RVT_ENABLED");
        }
        if (rtSpotShadowEnabled && rtSpotShadowMaskLayout)
        {
            terrainShader->addMacroDefinition("RT_SPOT_SHADOW_ENABLED");
        }
        if (rtPointShadowEnabled && rtPointShadowMaskLayout)
        {
            terrainShader->addMacroDefinition("RT_POINT_SHADOW_ENABLED");
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
        const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        if (!loadTerrainShaders()) return;

        vk::Device vkDevice = device.getLogicalDevice();

        // Set 5 is the empty placeholder unless RVT is active, in which case it carries the
        // RVT page table / atlases / feedback / params (VK-1209). OFF -> byte-identical layout.
        std::vector<vk::DescriptorSetLayout> setLayouts = {
            iblLayout, weightMapLayout, bindlessTextureLayout, meshletDataLayout,
            vertexDataLayout, (rvtSampleEnabled ? rvtSampleLayout : emptyLayout),
            lightDataLayout, clusterGridLayout,
            cullingOutputLayout, shadowDataLayout, shadowTextureLayout, terrainDataLayout
        };

        pipelineHasSet12 = false;
        rtMaskBound = false;
        const bool anyRT = anyRTMaskActive();
        if (anyRT)
        {
            // All RT shadow masks share set 13; set 12 is a placeholder (caustics and the RT-mask
            // region remain mutually exclusive on terrain, as before). The ring slots are freshly
            // allocated here, so mark them all dirty (VK-1398) and let ensureRTMaskSlot() repopulate
            // each from the cached producer descriptors on its first bind — surviving this rebuild
            // regardless of the setRT*/updateRT*/recreate call order.
            if (!rtMaskSet)
            {
                rtMaskSet = std::make_unique<raytracing::RTShadowMaskSet>(device);
                rtMaskSet->init();
            }
            markAllRTMaskSlotsDirty();

            setLayouts.push_back(emptyLayout);            // Set 12 (placeholder)
            setLayouts.push_back(rtMaskSet->getLayout()); // Set 13 (shared mask)
            pipelineHasSet12 = true;
            rtMaskBound = true;
        }
        else if (causticEnabled && cachedCausticLayout)
        {
            setLayouts.push_back(cachedCausticLayout); // Set 12
            pipelineHasSet12 = true;
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
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = colorFormats,
            .depthAttachmentFormat = depthFormat,
            .shaderStages = terrainShader->getShaderStages(),
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .polygonMode = wireframeMode ? vk::PolygonMode::eLine : vk::PolygonMode::eFill,
            .depthTestEnable = true,
            .depthWriteEnable = true
        };
        config.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        // VRS: the FSR dynamic state may only be declared when the device enabled
        // VK_KHR_fragment_shading_rate; once declared, every dispatch must set the rate
        // (dispatch() does, 1x1 when VRS is off).
        if (device.isFragmentShadingRateSupported())
            config.dynamicStates.push_back(vk::DynamicState::eFragmentShadingRateKHR);
        config.dynamicSampleCount = true;

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
