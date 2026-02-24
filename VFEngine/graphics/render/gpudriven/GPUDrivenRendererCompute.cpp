#include "GPUDrivenRenderer.hpp"
#include "../../core/SwapChain.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include <algorithm>
#include <array>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven
{
    void GPUDrivenRenderer::collectShadowVisibleLights(std::unordered_set<uint32_t>& outLights, bool& outHasFilter)
    {
        outHasFilter = false;
        outLights.clear();

        if (useBVHLightCulling && !visibleLightIds.empty())
        {
            outLights = visibleLightIds;
            outHasFilter = true;
        }

        if (useLightOcclusionCulling && hasPrevFrameOcclusionData && !prevFrameOccludedLights.empty())
        {
            if (outHasFilter)
            {
                for (uint32_t occludedId : prevFrameOccludedLights)
                    outLights.erase(occludedId);
            }
            else
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto pointView = registry.view<components::PointLightComponent>();
                for (auto entity : pointView)
                {
                    uint32_t entityId = static_cast<uint32_t>(entity);
                    if (!prevFrameOccludedLights.contains(entityId))
                        outLights.insert(entityId);
                }
                auto spotView = registry.view<components::SpotLightComponent>();
                for (auto entity : spotView)
                {
                    uint32_t entityId = static_cast<uint32_t>(entity);
                    if (!prevFrameOccludedLights.contains(entityId))
                        outLights.insert(entityId);
                }
                outHasFilter = true;
            }
        }
    }

    void GPUDrivenRenderer::buildAndDispatchLightOcclusion(vk::CommandBuffer cmd)
    {
        if (!useLightOcclusionCulling || !lightOcclusionCulling || !lightOcclusionCulling->isInitialized())
        {
            return;
        }

        std::vector<occlusion::GPULightBounds> lightBounds;
        auto& registry = scene::EntityRegistry::getRegistry();

        auto pointView = registry.view<components::PointLightComponent, components::WorldTransformComponent>();
        for (auto entity : pointView)
        {
            uint32_t entityId = static_cast<uint32_t>(entity);
            if (useBVHLightCulling && !visibleLightIds.empty() && !visibleLightIds.contains(entityId))
                continue;

            const auto& light = pointView.get<components::PointLightComponent>(entity);
            const auto& transform = pointView.get<components::WorldTransformComponent>(entity);

            glm::vec3 position = glm::vec3(transform.worldMatrix[3]);

            occlusion::GPULightBounds bounds{};
            bounds.positionRadius = glm::vec4(position, light.radius);
            bounds.direction = glm::vec4(0.0f);
            bounds.entityId = entityId;
            bounds.lightType = static_cast<uint32_t>(occlusion::LightOcclusionType::Point);
            lightBounds.push_back(bounds);
        }

        auto spotView = registry.view<components::SpotLightComponent, components::WorldTransformComponent>();
        for (auto entity : spotView)
        {
            uint32_t entityId = static_cast<uint32_t>(entity);
            if (useBVHLightCulling && !visibleLightIds.empty() && !visibleLightIds.contains(entityId))
                continue;

            const auto& light = spotView.get<components::SpotLightComponent>(entity);
            const auto& transform = spotView.get<components::WorldTransformComponent>(entity);

            glm::vec3 position = glm::vec3(transform.worldMatrix[3]);
            glm::vec3 forward = glm::normalize(glm::vec3(transform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            occlusion::GPULightBounds bounds{};
            bounds.positionRadius = glm::vec4(position, light.range);
            bounds.direction = glm::vec4(forward, light.outerAngle);
            bounds.entityId = entityId;
            bounds.lightType = static_cast<uint32_t>(occlusion::LightOcclusionType::Spot);
            lightBounds.push_back(bounds);
        }

        if (!lightBounds.empty())
        {
            lightOcclusionCulling->updateLights(lightBounds);
            lightOcclusionCulling->recordLightUpload(cmd);

            const auto& camData = cameraBuffer->getData();
            glm::mat4 viewProj = camData.projection * camData.view;
            lightOcclusionCulling->updateCamera(viewProj, glm::vec3(camData.cameraPosition), camData.cameraPosition.w);

            lightOcclusionCulling->cull(cmd);
            lightOcclusionCulling->copyResultsToStaging(cmd);
        }
    }

    void GPUDrivenRenderer::recordShadowPasses(vk::CommandBuffer cmd, bool hasMeshObjects, bool hasTerrainTiles)
    {
        if (!shadowSystem || !shadowSystem->isShadowsEnabled())
        {
            return;
        }

        shadowSystem->uploadToGPU(cmd);

        shadow::ShadowPassParams shadowParams{};
        if (hasMeshObjects && meshShaderPipeline && boneMatrixManager && batchManager)
        {
            shadowParams.perDrawDataDescSet = meshShaderPipeline->getPerDrawDataDescriptorSet();
            shadowParams.meshletDataDescSet = meshShaderPipeline->getMeshletDataDescriptorSet();
            shadowParams.vertexDataDescSet = meshShaderPipeline->getVertexDataDescriptorSet();
            shadowParams.boneMatrixDescSet = boneMatrixManager->getDescriptorSet();
            shadowParams.drawCommandBuffer = batchManager->getCombinedDrawCommandBuffer();
            shadowParams.drawCountBuffer = batchManager->getCombinedDrawCountBuffer();
            shadowParams.batchCount = batchManager->getBatchCount();
            shadowParams.commandsPerSection = batchManager->getCommandsPerSection();
            shadowParams.shaderGroupCount = batchManager->getShaderGroupCount();
            shadowParams.transparentGroupIndex = 3;  // Transparent objects don't cast shadows
            shadowParams.drawCountStructSize = sizeof(BatchDrawStats);
        }

        shadow::TerrainShadowPassParams terrainShadowParams{};
        shadow::TerrainShadowPassParams* terrainShadowParamsPtr = nullptr;

        if (hasTerrainTiles && terrainPipeline && terrainMeshBuffer &&
            terrainMeshBuffer->isInitialized() && terrainPipeline->getCurrentTileCount() > 0)
        {
            vk::DescriptorSet terrainDataSet = terrainPipeline->getTerrainDataDescriptorSet();
            vk::DescriptorSet terrainMeshletSet = terrainPipeline->getTerrainMeshletDescriptorSet();
            vk::DescriptorSet terrainVertexSet = terrainPipeline->getTerrainVertexDescriptorSet();

            if (terrainDataSet && terrainMeshletSet && terrainVertexSet)
            {
                terrainShadowParams.terrainDataDescSet = terrainDataSet;
                terrainShadowParams.terrainMeshletDescSet = terrainMeshletSet;
                terrainShadowParams.terrainVertexDescSet = terrainVertexSet;
                terrainShadowParams.tileCount = terrainPipeline->getCurrentTileCount();
                terrainShadowParams.shadowLOD = terrainShadowLOD;
                terrainShadowParamsPtr = &terrainShadowParams;
            }
        }

        shadowSystem->recordShadowPass(cmd, shadowParams, terrainShadowParamsPtr);
    }

    void GPUDrivenRenderer::dispatchCompute(vk::CommandBuffer cmd)
    {
        if (!initialized || !enabled)
        {
            return;
        }

        if (mergedBuffer) mergedBuffer->flushPendingTransfers();
        if (meshletBuffer) meshletBuffer->flushPendingTransfers();
        if (terrainMeshBuffer) terrainMeshBuffer->flushPendingTransfers();

        batchManager->resetAllBatches(cmd);

        if (meshShaderPipeline)
        {
            meshShaderPipeline->resetStats(cmd);
        }

        {
            auto& registry = scene::EntityRegistry::getRegistry();
            uint32_t pointCount = static_cast<uint32_t>(registry.view<components::PointLightComponent>().size());
            uint32_t spotCount = static_cast<uint32_t>(registry.view<components::SpotLightComponent>().size());
            totalSceneLights = pointCount + spotCount;

            if (useBVHLightCulling && !visibleLightIds.empty())
            {
                lightsAfterBVHCull = std::min(static_cast<uint32_t>(visibleLightIds.size()), totalSceneLights);
            }
            else
            {
                lightsAfterBVHCull = totalSceneLights;
            }

            lightsAfterHiZCull = lightsAfterBVHCull;
        }

        if (shadowSystem && shadowSystem->isInitialized())
        {
            std::unordered_set<uint32_t> shadowVisibleLights;
            bool hasShadowFilter = false;
            collectShadowVisibleLights(shadowVisibleLights, hasShadowFilter);

            if (hasShadowFilter && !shadowVisibleLights.empty())
            {
                shadowSystem->beginFrame(cachedCameraView, cachedCameraProjection,
                                          cachedCameraNear, cachedCameraFar,
                                          &shadowVisibleLights);
            }
            else
            {
                shadowSystem->beginFrame(cachedCameraView, cachedCameraProjection,
                                          cachedCameraNear, cachedCameraFar);
            }
        }

        if (lightBufferManager)
        {
            if (useBVHLightCulling && !visibleLightIds.empty())
            {
                lightBufferManager->updateFromScene(visibleLightIds);
            }
            else
            {
                lightBufferManager->updateFromScene();
            }
            lightBufferManager->uploadToGPU(cmd);
        }

        bool hasMeshObjects = stats.totalObjects > 0;
        bool hasTerrainTiles = terrainRenderingEnabled && terrainPipeline &&
                               terrainPipeline->getCurrentTileCount() > 0;

        if (!hasMeshObjects && !hasTerrainTiles)
        {
            return;
        }

        if (hasMeshObjects)
        {
            mergedBuffer->uploadObjects(cmd);
        }

        if (boneMatrixManager)
        {
            boneMatrixManager->uploadToGPU(cmd);
        }

        buildAndDispatchLightOcclusion(cmd);

        if (clusterGridManager)
        {
            clusterGridManager->uploadToGPU(cmd);
        }

        vk::MemoryBarrier memBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        };

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &memBarrier,
            0, nullptr,
            0, nullptr);

        if (lightCullingPipeline && lightBufferManager)
        {
            lightCullingPipeline->dispatch(
                cmd,
                cameraBuffer->getData().view,
                lightBufferManager->getPointLightCount(),
                lightBufferManager->getSpotLightCount()
            );
        }

        cullPipeline->dispatch(cmd, stats.totalObjects);
        batchManager->insertBarriersAfterCompute(cmd);

        recordShadowPasses(cmd, hasMeshObjects, hasTerrainTiles);

        // Volumetric fog runs AFTER shadow passes so that:
        // 1. Shadow texture descriptor set is finalized (no updates after binding)
        // 2. Shadow maps are rendered and available for sampling
        if (volumetricPipeline && volumetricPipeline->isEnabled())
        {
            const auto& camData = cameraBuffer->getData();
            glm::mat4 viewProj = camData.projection * camData.view;
            glm::mat4 invViewProj = glm::inverse(viewProj);
            volumetricPipeline->update(viewProj, invViewProj,
                                       glm::vec3(camData.cameraPosition),
                                       cachedCameraNear, cachedCameraFar,
                                       cachedVolumetricSettings);
            volumetricPipeline->dispatch(cmd,
                                         clusterGridManager->getDescriptorSet(),
                                         lightBufferManager->getDescriptorSet(),
                                         lightCullingPipeline->getDescriptorSet(),
                                         shadowSystem ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
                                         shadowSystem ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{});
        }
    }

    void GPUDrivenRenderer::renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!initialized || !enabled || stats.totalObjects == 0 || !meshShaderPipeline)
        {
            return;
        }

        uint32_t batchCount = batchManager->getBatchCount();
        uint32_t commandsPerSection = batchManager->getCommandsPerSection();

        vk::Pipeline activePipeline = meshShaderPipeline->getPipeline();
        vk::PipelineLayout layout = meshShaderPipeline->getPipelineLayout();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);

        std::array<vk::DescriptorSet, 11> descriptorSets = {
            iblDescriptorSet,
            meshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            meshShaderPipeline->getMeshletDataDescriptorSet(),
            meshShaderPipeline->getVertexDataDescriptorSet(),
            boneMatrixManager->getDescriptorSet(),
            meshShaderPipeline->getLightDataDescriptorSet(),
            meshShaderPipeline->getClusterGridDescriptorSet(),
            meshShaderPipeline->getCullingOutputDescriptorSet(),
            meshShaderPipeline->getShadowDataDescriptorSet(),
            meshShaderPipeline->getShadowTextureDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        auto extent = swapChain.getSwapchainExtent();

        for (uint32_t shaderGroup = 0; shaderGroup <= 2; ++shaderGroup)
        {
            for (uint32_t batch = 0; batch < batchCount; ++batch)
            {
                vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, shaderGroup);
                vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, shaderGroup);

                MeshShaderPushConstants pushConstants{};
                pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, shaderGroup) * commandsPerSection;

                pushConstants.viewMode = currentViewMode;
                if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
                if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
                pushConstants.screenWidth = static_cast<float>(extent.width);
                pushConstants.screenHeight = static_cast<float>(extent.height);

                cmd.pushConstants(
                    layout,
                    vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                    vk::ShaderStageFlagBits::eFragment,
                    0,
                    sizeof(MeshShaderPushConstants),
                    &pushConstants);

                cmd.drawMeshTasksIndirectCountEXT(
                    batchManager->getCombinedDrawCommandBuffer(),
                    cmdOffset,
                    batchManager->getCombinedDrawCountBuffer(),
                    countOffset,
                    commandsPerSection,
                    sizeof(MeshTasksIndirectCommand));
            }
        }
    }

    void GPUDrivenRenderer::renderTransparentDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!initialized || !enabled || stats.totalObjects == 0 || !transparentMeshShaderPipeline)
        {
            return;
        }

        uint32_t batchCount = batchManager->getBatchCount();
        uint32_t commandsPerSection = batchManager->getCommandsPerSection();
        constexpr uint32_t transparentShaderGroup = 3;

        if (transparentShaderGroup >= batchManager->getShaderGroupCount())
        {
            return;
        }

        vk::Pipeline activePipeline = transparentMeshShaderPipeline->getPipeline();
        vk::PipelineLayout layout = transparentMeshShaderPipeline->getPipelineLayout();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);

        std::array<vk::DescriptorSet, 11> descriptorSets = {
            iblDescriptorSet,
            transparentMeshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            transparentMeshShaderPipeline->getMeshletDataDescriptorSet(),
            transparentMeshShaderPipeline->getVertexDataDescriptorSet(),
            boneMatrixManager->getDescriptorSet(),
            transparentMeshShaderPipeline->getLightDataDescriptorSet(),
            transparentMeshShaderPipeline->getClusterGridDescriptorSet(),
            transparentMeshShaderPipeline->getCullingOutputDescriptorSet(),
            transparentMeshShaderPipeline->getShadowDataDescriptorSet(),
            transparentMeshShaderPipeline->getShadowTextureDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        auto extent = swapChain.getSwapchainExtent();

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, transparentShaderGroup);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, transparentShaderGroup);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, transparentShaderGroup) * commandsPerSection;

            pushConstants.viewMode = currentViewMode;
            if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = static_cast<float>(extent.width);
            pushConstants.screenHeight = static_cast<float>(extent.height);

            cmd.pushConstants(
                layout,
                vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(MeshShaderPushConstants),
                &pushConstants);

            cmd.drawMeshTasksIndirectCountEXT(
                batchManager->getCombinedDrawCommandBuffer(),
                cmdOffset,
                batchManager->getCombinedDrawCountBuffer(),
                countOffset,
                commandsPerSection,
                sizeof(MeshTasksIndirectCommand));
        }
    }

    void GPUDrivenRenderer::renderWBOITDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!initialized || !enabled || stats.totalObjects == 0 || !wboitMeshShaderPipeline)
        {
            return;
        }

        uint32_t batchCount = batchManager->getBatchCount();
        uint32_t commandsPerSection = batchManager->getCommandsPerSection();
        constexpr uint32_t transparentShaderGroup = 3;

        if (transparentShaderGroup >= batchManager->getShaderGroupCount())
        {
            return;
        }

        vk::Pipeline activePipeline = wboitMeshShaderPipeline->getPipeline();
        vk::PipelineLayout layout = wboitMeshShaderPipeline->getPipelineLayout();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);

        std::array<vk::DescriptorSet, 11> descriptorSets = {
            iblDescriptorSet,
            wboitMeshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            wboitMeshShaderPipeline->getMeshletDataDescriptorSet(),
            wboitMeshShaderPipeline->getVertexDataDescriptorSet(),
            boneMatrixManager->getDescriptorSet(),
            wboitMeshShaderPipeline->getLightDataDescriptorSet(),
            wboitMeshShaderPipeline->getClusterGridDescriptorSet(),
            wboitMeshShaderPipeline->getCullingOutputDescriptorSet(),
            wboitMeshShaderPipeline->getShadowDataDescriptorSet(),
            wboitMeshShaderPipeline->getShadowTextureDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        auto extent = swapChain.getSwapchainExtent();

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, transparentShaderGroup);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, transparentShaderGroup);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, transparentShaderGroup) * commandsPerSection;

            pushConstants.viewMode = currentViewMode;
            if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = static_cast<float>(extent.width);
            pushConstants.screenHeight = static_cast<float>(extent.height);

            cmd.pushConstants(
                layout,
                vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(MeshShaderPushConstants),
                &pushConstants);

            cmd.drawMeshTasksIndirectCountEXT(
                batchManager->getCombinedDrawCommandBuffer(),
                cmdOffset,
                batchManager->getCombinedDrawCountBuffer(),
                countOffset,
                commandsPerSection,
                sizeof(MeshTasksIndirectCommand));
        }
    }
}
