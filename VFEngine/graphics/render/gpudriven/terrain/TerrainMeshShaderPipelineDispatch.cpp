#include "TerrainMeshShaderPipeline.hpp"
#include "TerrainMeshBuffer.hpp"
#include "../GPUDrivenRenderer.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include <array>

namespace render::gpudriven
{
    void TerrainMeshShaderPipeline::updateTileData(const std::vector<TerrainTileGPUData>& tiles)
    {
        if (tiles.empty())
        {
            currentTileCount = 0;
            return;
        }

        if (tiles.size() > maxTileCount)
        {
            vfLogWarning("TerrainMeshShaderPipeline: Tile count {} exceeds max {}, truncating",
                          tiles.size(), maxTileCount);
        }

        currentTileCount = static_cast<uint32_t>(std::min(tiles.size(), static_cast<size_t>(maxTileCount)));
        vk::DeviceSize dataSize = currentTileCount * sizeof(TerrainTileGPUData);
        std::memcpy(tileDataBufferMapped, tiles.data(), dataSize);
    }

    void TerrainMeshShaderPipeline::updateWeightMapDescriptor(vk::Buffer weightMapBuffer)
    {
        if (!initialized || !weightMapDescriptorSet || !weightMapBuffer) return;

        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = weightMapBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write{};
        write.dstSet = weightMapDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.pBufferInfo = &bufferInfo;

        vkDevice.updateDescriptorSets(write, {});
    }

    void TerrainMeshShaderPipeline::updateTerrainLayerInfo(const std::vector<TerrainLayerGPUData>& layers)
    {
        if (!terrainLayerBufferMapped) return;

        constexpr uint32_t maxLayers = terrain::MAX_TERRAIN_LAYERS;
        if (layers.empty())
        {
            std::memset(terrainLayerBufferMapped, 0, maxLayers * sizeof(TerrainLayerGPUData));
            return;
        }

        uint32_t count = static_cast<uint32_t>(std::min(layers.size(), static_cast<size_t>(maxLayers)));
        std::memcpy(terrainLayerBufferMapped, layers.data(), count * sizeof(TerrainLayerGPUData));
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
            {weightMapDescriptorSet, "weightMapDescriptorSet (set 1)"},
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
                    vfLogWarning("TerrainMeshShaderPipeline: {} is NULL!", name);
                }
            }
        }
        warnedMissing = true;

        if (hasCriticalMissing)
        {
            static bool warnedAbort = false;
            if (!warnedAbort)
            {
                vfLogWarning("TerrainMeshShaderPipeline: Aborting dispatch - missing critical descriptor sets.");
                warnedAbort = true;
            }
        }

        return !hasCriticalMissing;
    }

    void TerrainMeshShaderPipeline::bindDescriptorSetsInBatches(
        vk::CommandBuffer cmd, const vk::DescriptorSet* currentSets, uint32_t count) const
    {
        uint32_t batchStart = 0;
        std::vector<vk::DescriptorSet> batch;
        batch.reserve(count);

        auto flushBatch = [&]() {
            if (!batch.empty())
            {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                       batchStart, batch, {});
                batch.clear();
            }
        };

        for (uint32_t i = 0; i < count; ++i)
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
        if (frustumCullingEnabled) effectiveViewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        if (meshletCullingEnabled) effectiveViewMode |= TERRAIN_CULL_BACKFACE_BIT;
        if (meshletOcclusionCullingEnabled && hiZMipLevels > 0) effectiveViewMode |= TERRAIN_CULL_OCCLUSION_BIT;
        pc.viewMode = effectiveViewMode;
        pc.screenWidth = screenWidth;
        pc.screenHeight = screenHeight;
        pc.lodBias = lodBias;
        pc.errorThreshold = errorThreshold;
        pc.terrainTextureScale = textureScale;
        pc.terrainMaxDrawDistSq = terrainMaxDrawDistSq;
        pc.brushWorldPos = brushWorldPos;
        pc.brushWorldRadius = brushWorldRadius;
        pc.brushFalloff = brushFalloff;
        pc.brushShape = brushShape;
        pc.brushWorldY = brushWorldY;
        pc.hiZMipLevels = hiZMipLevels;
        pc._pad3 = 0.0f;
        // VK-1334: if this thread is recording an RTT pre-pass, the renderer published the RTT
        // view-projection through TLS so we don't have to mutate the shared `viewProjection`
        // member (which would race with a concurrent main recording on another worker).
        glm::mat4 rttVP;
        if (GPUDrivenRenderer::tryGetThreadLocalTerrainViewProjection(rttVP))
        {
            pc.viewProjection = rttVP;
        }
        else
        {
            pc.viewProjection = viewProjection;
        }
        pc.stampWidth = stampOverlayWidth;
        pc.stampHeight = stampOverlayHeight;
        pc.stampRotation = stampOverlayRotation;
        pc._padStamp = 0.0f;
        return pc;
    }

    void TerrainMeshShaderPipeline::ensureRTMaskSlot(uint32_t imageIndex)
    {
        // Lazily populate the ring slot for the image being recorded from the cached producers.
        // Only writes when dirty, so steady state issues zero descriptor writes (VK-1398).
        if (!rtMaskSet || imageIndex >= core::MAX_SWAPCHAIN_IMAGES || !rtMaskSlotDirty[imageIndex])
            return;

        if (rtShadowMaskDescriptorSet)
            rtMaskSet->copyInto(raytracing::RTShadowMaskSet::BINDING_DIRECTIONAL, rtShadowMaskDescriptorSet, imageIndex);
        if (rtSpotShadowMaskDescriptorSet)
            rtMaskSet->copyInto(raytracing::RTShadowMaskSet::BINDING_SPOT, rtSpotShadowMaskDescriptorSet, imageIndex);
        if (rtPointShadowMaskDescriptorSet)
            rtMaskSet->copyInto(raytracing::RTShadowMaskSet::BINDING_POINT, rtPointShadowMaskDescriptorSet, imageIndex);

        rtMaskSlotDirty[imageIndex] = false;
    }

    void TerrainMeshShaderPipeline::dispatch(vk::CommandBuffer cmd,
                                              uint32_t imageIndex,
                                              uint32_t viewMode,
                                              float screenWidth,
                                              float screenHeight,
                                              float lodBias,
                                              float errorThreshold,
                                              float textureScale)
    {
        if (!initialized || !graphicsPipeline || currentTileCount == 0) return;
        if (!validateDescriptorsForDispatch()) return;

        // Update stamp overlay descriptor if changed
        if (stampOverlayDirty)
        {
            vk::Device vkDevice = device.getLogicalDevice();
            if (stampOverlayBuffer)
            {
                vk::DescriptorBufferInfo stampInfo{stampOverlayBuffer, 0, VK_WHOLE_SIZE};
                vk::WriteDescriptorSet stampWrite{};
                stampWrite.dstSet = terrainDataDescriptorSet;
                stampWrite.dstBinding = 2;
                stampWrite.descriptorCount = 1;
                stampWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
                stampWrite.pBufferInfo = &stampInfo;
                vkDevice.updateDescriptorSets({stampWrite}, {});
            }
            stampOverlayDirty = false;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        // Set 5 = RVT sample set when RVT active (VK-1209), else the empty placeholder.
        std::vector<vk::DescriptorSet> currentSets = {
            iblDescriptorSet, weightMapDescriptorSet, bindlessDescriptorSet,
            terrainMeshletDescriptorSet, terrainVertexDescriptorSet,
            (rvtSampleEnabled ? rvtSampleDescriptorSet : emptyDescriptorSet5),
            lightDataDescriptorSet, clusterGridDescriptorSet, cullingOutputDescriptorSet,
            shadowDataDescriptorSet, shadowTextureDescriptorSet, terrainDataDescriptorSet
        };

        if (pipelineHasSet12)
        {
            if (rtMaskBound && rtMaskSet)
            {
                // Shared RT shadow mask set at set 13 (directional/spot/point in bindings 0/1/2).
                // Ringed per swapchain image (VK-1398): sync + bind the slot for this image.
                ensureRTMaskSlot(imageIndex);
                currentSets.push_back(emptyDescriptorSet5);                     // Set 12 (placeholder)
                currentSets.push_back(rtMaskSet->getDescriptorSet(imageIndex)); // Set 13 (shared mask)
            }
            else if (causticEnabled && causticDescriptorSet)
            {
                currentSets.push_back(causticDescriptorSet); // Set 12
            }
        }

        bindDescriptorSetsInBatches(cmd, currentSets.data(), static_cast<uint32_t>(currentSets.size()));

        TerrainPushConstants pushConstants = buildTerrainPushConstants(
            viewMode, screenWidth, screenHeight, lodBias, errorThreshold, textureScale);

        cmd.pushConstants(pipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT |
                          vk::ShaderStageFlagBits::eMeshEXT |
                          vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(TerrainPushConstants), &pushConstants);

        cmd.drawMeshTasksEXT(currentTileCount, 1, 1);
        render::FrameDrawStats::count(render::DrawCategory::Terrain);
    }

    TerrainCullingStats TerrainMeshShaderPipeline::readStats()
    {
        if (!statsBuffer) return cachedStats;

        device.waitGraphicsIdle();

        vk::Device vkDevice = device.getLogicalDevice();
        std::memcpy(&cachedStats, statsBufferAllocation.mappedPtr, sizeof(TerrainCullingStats));

        return cachedStats;
    }
}
