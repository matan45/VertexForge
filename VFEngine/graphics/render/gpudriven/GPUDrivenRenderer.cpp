#include "GPUDrivenRenderer.hpp"
#include "terrain/TerrainTile.hpp"
#include "../occlusion/HiZBuffer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "../../animation/AnimatorStateMachine.hpp"
#include "resource/ResourceManager.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include "material/MaterialManager.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/RenderManager.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <unordered_map>
#include <memory>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif


namespace render::gpudriven
{
    GPUDrivenRenderer::GPUDrivenRenderer(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    GPUDrivenRenderer::~GPUDrivenRenderer()
    {
        cleanup();
    }

    void GPUDrivenRenderer::init(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass)
    {
        if (initialized)
        {
            return;
        }

        loggerInfo("GPUDrivenRenderer: Initializing...");

        cachedIBLLayout = iblDescriptorSetLayout;
        cachedRenderPass = renderPass;

        mergedBuffer = std::make_unique<MergedMeshBuffer>(device);
        mergedBuffer->init();

        if (meshStreamingEnabled)
        {
            meshStreamManager = std::make_unique<mesh::MeshStreamManager>(device, *mergedBuffer);
            loggerInfo("GPUDrivenRenderer: Mesh streaming enabled by default");
        }

        batchManager = std::make_unique<IndirectBatchManager>(device);
        if (!batchManager->initWithAutoConfig())
        {
            loggerError("GPUDrivenRenderer: Failed to initialize batch manager - GPU memory allocation failed");
            if (!batchManager->init(2, 50000, 8))
            {
                loggerError(
                    "GPUDrivenRenderer: Even minimal batch configuration failed - GPU-driven rendering unavailable");
                batchManager.reset();
            }
        }

        bindlessTextures = std::make_unique<BindlessTextureManager>(device);
        bindlessTextures->init();

        cullPipeline = std::make_unique<GPUCullLODPipeline>(device);
        cullPipeline->init();

        cameraBuffer = std::make_unique<GPUDrivenCameraBuffer>(device, swapChain);
        cameraBuffer->init();

        const auto& meshCaps = device.getMeshShaderCapabilities();
        meshShaderSupported = meshCaps.meshShaderSupported && meshCaps.taskShaderSupported;

        if (meshShaderSupported &&
            (MESHLET_MAX_VERTICES > meshCaps.maxMeshOutputVertices ||
                MESHLET_MAX_PRIMITIVES > meshCaps.maxMeshOutputPrimitives))
        {
            loggerError(
                "GPUDrivenRenderer: Meshlet constants ({} vertices, {} primitives) exceed device limits ({}, {})",
                MESHLET_MAX_VERTICES, MESHLET_MAX_PRIMITIVES,
                meshCaps.maxMeshOutputVertices, meshCaps.maxMeshOutputPrimitives);
            meshShaderSupported = false;
        }

        if (meshShaderSupported)
        {
            loggerInfo("GPUDrivenRenderer: Mesh shader supported - using Task+Mesh shader pipeline");

            meshletBuffer = std::make_unique<MeshletBuffer>(device);
            meshletBuffer->init();

            boneMatrixManager = std::make_unique<BoneMatrixManager>(device);
            boneMatrixManager->init();

            lightBufferManager = std::make_unique<lighting::GPULightBufferManager>(device);
            lightBufferManager->init();

            clusterGridManager = std::make_unique<lighting::ClusterGridManager>(device);
            clusterGridManager->init();

            lightCullingPipeline = std::make_unique<lighting::LightCullingPipeline>(device);
            lightCullingPipeline->init(
                clusterGridManager->getTotalClusters(),
                clusterGridManager->getDescriptorSetLayout(),
                lightBufferManager->getDescriptorSetLayout()
            );

            shadowSystem = std::make_unique<shadow::ShadowSystem>(device);
            shadowSystem->init();
            shadowSystem->setLightBufferManager(lightBufferManager.get());

            if (!shadowSystem || !shadowSystem->isInitialized())
            {
                loggerError("GPUDrivenRenderer: Shadow system initialization failed");
                return;
            }

            lightBufferManager->setShadowSystem(shadowSystem.get());

            if (core::RenderManager::getGlobalDeletionQueue())
            {
                shadowSystem->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());
            }

            meshShaderPipeline = std::make_unique<MeshShaderPipeline>(device, swapChain);
            meshShaderPipeline->init(iblDescriptorSetLayout,
                                     bindlessTextures->getDescriptorSetLayout(),
                                     boneMatrixManager->getDescriptorSetLayout(),
                                     lightBufferManager->getDescriptorSetLayout(),
                                     clusterGridManager->getDescriptorSetLayout(),
                                     lightCullingPipeline->getDescriptorSetLayout(),
                                     shadowSystem->getShadowDataLayout(),
                                     shadowSystem->getShadowTextureLayout(),
                                     renderPass);

            shadowSystem->initShadowPass(
                meshShaderPipeline->getPerDrawDataLayout(),
                meshShaderPipeline->getMeshletDataLayout(),
                meshShaderPipeline->getVertexDataLayout(),
                boneMatrixManager->getDescriptorSetLayout()
            );

            if (meshStreamManager)
            {
                meshStreamManager->setMeshletBuffer(meshletBuffer.get());
                loggerInfo("GPUDrivenRenderer: Meshlet streaming enabled");
            }

            terrainMeshBuffer = std::make_unique<TerrainMeshBuffer>(device);
            terrainMeshBuffer->init();

            terrainAdapter = std::make_unique<TerrainGPUAdapter>(*terrainMeshBuffer);
            terrainStreamManager = std::make_unique<TerrainStreamManager>(*terrainMeshBuffer, *terrainAdapter);

            terrainPipeline = std::make_unique<TerrainMeshShaderPipeline>(device, swapChain);
            terrainPipeline->init(
                iblDescriptorSetLayout,
                bindlessTextures->getDescriptorSetLayout(),
                meshShaderPipeline->getMeshletDataLayout(),
                meshShaderPipeline->getVertexDataLayout(),
                lightBufferManager->getDescriptorSetLayout(),
                clusterGridManager->getDescriptorSetLayout(),
                lightCullingPipeline->getDescriptorSetLayout(),
                shadowSystem->getShadowDataLayout(),
                shadowSystem->getShadowTextureLayout(),
                renderPass
            );
            loggerInfo("GPUDrivenRenderer: Terrain mesh shader pipeline initialized");

            shadowSystem->initTerrainShadowPass(
                terrainPipeline->getTerrainDataLayout(),
                terrainPipeline->getCachedMeshletLayout(),
                terrainPipeline->getCachedVertexLayout()
            );
            loggerInfo("GPUDrivenRenderer: Terrain shadow pass initialized");
        }
        else
        {
            loggerError(
                "GPUDrivenRenderer: Mesh shaders not supported - GPU-driven rendering requires mesh shader support");
            loggerError("GPUDrivenRenderer: The VK_EXT_mesh_shader extension with task shader support is required");
            return;
        }

        if (!materialChangeCallbackId)
        {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath) {
                    pbrCache.erase(materialPath);
                    registeredMaterialPaths.erase(materialPath);

                    if (!material::isInstanceFile(materialPath))
                    {
                        std::erase_if(pbrCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                        std::erase_if(registeredMaterialPaths, [](const std::string& path) {
                            return material::isInstanceFile(path);
                        });
                    }
                });
        }

        initialized = true;
        loggerInfo("GPUDrivenRenderer: Initialized successfully");
    }

    void GPUDrivenRenderer::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        if (materialChangeCallbackId)
        {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
            materialChangeCallbackId = {};
        }

        pbrCache.clear();
        registeredMaterialPaths.clear();

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (terrainPipeline) terrainPipeline->cleanup();
        if (terrainMeshBuffer) terrainMeshBuffer->cleanup();
        if (lightOcclusionCulling) lightOcclusionCulling->cleanup();
        if (meshShaderPipeline) meshShaderPipeline->cleanup();
        if (shadowSystem) shadowSystem->cleanup();
        if (lightCullingPipeline) lightCullingPipeline->cleanup();
        if (clusterGridManager) clusterGridManager->cleanup();
        if (lightBufferManager) lightBufferManager->cleanup();
        if (boneMatrixManager) boneMatrixManager->cleanup();
        if (meshletBuffer) meshletBuffer->cleanup();
        if (cameraBuffer) cameraBuffer->cleanup();
        if (cullPipeline) cullPipeline->cleanup();
        if (bindlessTextures) bindlessTextures->cleanup();
        if (batchManager) batchManager->cleanup();
        if (mergedBuffer) mergedBuffer->cleanup();

        meshStreamManager.reset();
        terrainStreamManager.reset();
        terrainAdapter.reset();
        terrainPipeline.reset();
        terrainMeshBuffer.reset();
        lightOcclusionCulling.reset();
        meshShaderPipeline.reset();
        shadowSystem.reset();
        lightCullingPipeline.reset();
        clusterGridManager.reset();
        lightBufferManager.reset();
        boneMatrixManager.reset();
        meshletBuffer.reset();
        cameraBuffer.reset();
        cullPipeline.reset();
        bindlessTextures.reset();
        batchManager.reset();
        mergedBuffer.reset();

        initialized = false;
        loggerInfo("GPUDrivenRenderer: Cleaned up");
    }

    void GPUDrivenRenderer::setDefaultTexture(vk::ImageView view, vk::Sampler sampler)
    {
        if (!initialized || !bindlessTextures)
        {
            return;
        }

        bindlessTextures->setDefaultTexture(view, sampler);
    }

    void GPUDrivenRenderer::updateScene(
        const std::vector<mesh::MeshRenderData>& opaqueObjects,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        float nearPlane,
        float farPlane,
        float time)
    {
        if (!initialized || !enabled)
        {
            return;
        }

        updateMeshStreaming(opaqueObjects, cameraPosition);
        registerSceneMaterialTextures(opaqueObjects);

        TextureIndexResolver textureResolver = createTextureResolver();
        ShaderGroupResolver shaderGroupResolver = [](const std::string&) -> uint32_t { return 0; };
        BoneOffsetResolver boneOffsetResolver = updateAnimationBones();

        mergedBuffer->updateObjects(opaqueObjects, textureResolver, shaderGroupResolver, boneOffsetResolver, time);

        CameraUpdateParams cameraParams{
            .view = view,
            .projection = projection,
            .cameraPosition = cameraPosition,
            .nearPlane = nearPlane,
            .farPlane = farPlane,
            .time = time,
            .objectCount = mergedBuffer ? mergedBuffer->getObjectCount() : 0,
            .hiZMipLevels = hiZMipLevels,
            .frustumCullingEnabled = frustumCullingEnabled,
            .occlusionCullingEnabled = occlusionCullingEnabled,
            .lodSelectionEnabled = lodSelectionEnabled,
            .batchManager = batchManager.get()
        };
        cameraBuffer->update(cameraParams);

        cachedCameraView = view;
        cachedCameraProjection = projection;
        cachedCameraNear = nearPlane;
        cachedCameraFar = farPlane;

        if (terrainPipeline)
        {
            terrainPipeline->setViewProjection(projection * view);
        }

        updateClusterGrid(projection, nearPlane, farPlane);
        updatePipelineDescriptors();

        stats.totalObjects = mergedBuffer->getObjectCount();
    }

    void GPUDrivenRenderer::updateMeshStreaming(const std::vector<mesh::MeshRenderData>& opaqueObjects,
                                                 const glm::vec3& cameraPosition)
    {
        if (!meshStreamManager)
        {
            return;
        }

        for (const auto& meshRender : opaqueObjects)
        {
            meshStreamManager->requestMesh(meshRender.meshPath);
        }

        meshStreamManager->update(cameraPosition);
    }

    void GPUDrivenRenderer::registerSceneMaterialTextures(const std::vector<mesh::MeshRenderData>& opaqueObjects)
    {
        if (!materialTextureCache || !bindlessTextures)
        {
            return;
        }

        for (const auto& meshRender : opaqueObjects)
        {
            if (!meshRender.defaultMaterialPath.empty())
            {
                registerMaterialTextures(meshRender.defaultMaterialPath);
            }

            for (const auto& [submeshName, subMat] : meshRender.submeshMaterials)
            {
                if (!subMat.materialPath.empty())
                {
                    registerMaterialTextures(subMat.materialPath);
                }
            }
        }
    }

    TextureIndexResolver GPUDrivenRenderer::createTextureResolver()
    {
        if (!bindlessTextures)
        {
            return nullptr;
        }

        return [this](const std::string& materialPath, TextureSlotType slot) -> uint32_t
        {
            if (materialPath.empty())
            {
                return INVALID_TEXTURE_INDEX;
            }

            auto it = pbrCache.find(materialPath);
            if (it == pbrCache.end())
            {
                it = pbrCache.emplace(materialPath,
                                      mesh::MaterialPBRExtractor::extractPBRFromPath(materialPath)).first;
            }

            const auto& pbrValues = it->second;

            std::string texPath;
            switch (slot)
            {
            case TextureSlotType::Albedo: texPath = pbrValues.albedoTexturePath;
                break;
            case TextureSlotType::Normal: texPath = pbrValues.normalTexturePath;
                break;
            case TextureSlotType::ORM: texPath = pbrValues.ormTexturePath;
                break;
            case TextureSlotType::Metallic: texPath = pbrValues.metallicTexturePath;
                break;
            case TextureSlotType::Roughness: texPath = pbrValues.roughnessTexturePath;
                break;
            case TextureSlotType::AO: texPath = pbrValues.aoTexturePath;
                break;
            case TextureSlotType::Emission: texPath = pbrValues.emissionTexturePath;
                break;
            case TextureSlotType::Height: texPath = pbrValues.heightTexturePath;
                break;
            default: return INVALID_TEXTURE_INDEX;
            }

            if (texPath.empty())
            {
                return INVALID_TEXTURE_INDEX;
            }

            return bindlessTextures->getTextureIndex(texPath);
        };
    }

    BoneOffsetResolver GPUDrivenRenderer::updateAnimationBones()
    {
        if (!boneMatrixManager)
        {
            return nullptr;
        }

        auto& animatorSystem = animation::RuntimeAnimatorSystem::instance();
        auto& registry = scene::EntityRegistry::getRegistry();

        auto entityView = registry.view<components::MeshComponent>();
        for (auto entity : entityView)
        {
            const auto& meshComp = entityView.get<components::MeshComponent>(entity);

            if (meshComp.animatorPath.empty())
            {
                continue;
            }

            animation::AnimatorStateMachine* animator = animatorSystem.getAnimator(entity);
            if (!animator)
            {
                animatorSystem.initializeEntityAnimator(entity, meshComp.animatorPath);
                animator = animatorSystem.getAnimator(entity);
                if (!animator)
                {
                    continue;
                }
            }

            const std::vector<glm::mat4>& boneMatrices = animator->getBoneMatrices();
            if (boneMatrices.empty())
            {
                continue;
            }

            uint32_t boneCount = static_cast<uint32_t>(boneMatrices.size());
            uint32_t boneOffset = boneMatrixManager->allocate(entity, boneCount);

            if (boneOffset != INVALID_BONE_OFFSET)
            {
                boneMatrixManager->updateBoneMatrices(entity, boneMatrices);
            }
        }

        return [this](entt::entity entity) -> uint32_t
        {
            return boneMatrixManager->getBoneOffset(entity);
        };
    }

    void GPUDrivenRenderer::updateClusterGrid(const glm::mat4& projection, float nearPlane, float farPlane)
    {
        if (!clusterGridManager)
        {
            return;
        }

        auto extent = swapChain.getSwapchainExtent();
        lighting::ClusterCameraParams clusterCameraParams{};
        clusterCameraParams.nearPlane = nearPlane;
        clusterCameraParams.farPlane = farPlane;
        clusterCameraParams.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        clusterCameraParams.screenWidth = extent.width;
        clusterCameraParams.screenHeight = extent.height;
        clusterCameraParams.projection = projection;
        clusterCameraParams.invProjection = glm::inverse(projection);
        clusterCameraParams.fovY = 2.0f * glm::degrees(std::atan(1.0f / projection[1][1]));
        clusterGridManager->updateFromCamera(clusterCameraParams);
    }

    void GPUDrivenRenderer::updatePipelineDescriptors()
    {
        if (lightCullingPipeline && clusterGridManager && lightBufferManager)
        {
            lightCullingPipeline->updateExternalDescriptors(
                clusterGridManager->getDescriptorSet(),
                lightBufferManager->getDescriptorSet()
            );
        }

        cullPipeline->updateDescriptors(
            mergedBuffer->getObjectBuffer(),
            cameraBuffer->getBuffer(),
            batchManager->getCombinedDrawCommandBuffer(),
            batchManager->getCombinedPerDrawDataBuffer(),
            batchManager->getCombinedDrawCountBuffer()
        );

        bool hasMeshes = mergedBuffer->getObjectCount() > 0;

        // Always update meshlet and vertex descriptors - terrain uses these too
        if (meshShaderPipeline)
        {
            meshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            meshShaderPipeline->updateVertexDescriptors(*mergedBuffer);
        }

        if (meshShaderPipeline && hasMeshes)
        {
            meshShaderPipeline->updatePerDrawDescriptor(batchManager->getCombinedPerDrawDataBuffer());

            if (shadowSystem && shadowSystem->isInitialized())
            {
                meshShaderPipeline->updateShadowDescriptors(
                    shadowSystem->getShadowDataDescSet(),
                    shadowSystem->getShadowTextureDescSet());
            }
        }

        // Always update lighting descriptors when terrain or meshes may need them
        if (meshShaderPipeline && lightBufferManager && clusterGridManager && lightCullingPipeline)
        {
            meshShaderPipeline->updateLightingDescriptors(
                lightBufferManager->getDescriptorSet(),
                clusterGridManager->getDescriptorSet(),
                lightCullingPipeline->getDescriptorSet());
        }

        if (terrainRenderingEnabled && terrainPipeline && terrainMeshBuffer &&
            terrainMeshBuffer->isInitialized() && terrainPipeline->getCurrentTileCount() > 0)
        {
            terrainPipeline->updateTerrainBufferDescriptors(*terrainMeshBuffer);
            terrainPipeline->updateWeightMapDescriptor(terrainMeshBuffer->getWeightMapBuffer());
        }
    }

    void GPUDrivenRenderer::dispatchCompute(vk::CommandBuffer cmd)
    {
        if (!initialized || !enabled)
        {
            return;
        }

        if (mergedBuffer)
        {
            mergedBuffer->flushPendingTransfers();
        }
        if (meshletBuffer)
        {
            meshletBuffer->flushPendingTransfers();
        }
        if (terrainMeshBuffer)
        {
            terrainMeshBuffer->flushPendingTransfers();
        }

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

        // Light buffer must be updated even if there are no mesh objects,
        // because terrain rendering also needs light data.
        // Shadow system must update before light buffer manager so shadow indices are available
        if (shadowSystem && shadowSystem->isInitialized())
        {
            std::unordered_set<uint32_t> shadowVisibleLights;
            bool hasShadowFilter = false;

            if (useBVHLightCulling && !visibleLightIds.empty())
            {
                shadowVisibleLights = visibleLightIds;
                hasShadowFilter = true;
            }

            // Frame N-1 approach: use previous frame's occlusion results
            if (useLightOcclusionCulling && hasPrevFrameOcclusionData && !prevFrameOccludedLights.empty())
            {
                if (hasShadowFilter)
                {
                    for (uint32_t occludedId : prevFrameOccludedLights)
                        shadowVisibleLights.erase(occludedId);
                }
                else
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto pointView = registry.view<components::PointLightComponent>();
                    for (auto entity : pointView)
                    {
                        uint32_t entityId = static_cast<uint32_t>(entity);
                        if (!prevFrameOccludedLights.contains(entityId))
                            shadowVisibleLights.insert(entityId);
                    }
                    auto spotView = registry.view<components::SpotLightComponent>();
                    for (auto entity : spotView)
                    {
                        uint32_t entityId = static_cast<uint32_t>(entity);
                        if (!prevFrameOccludedLights.contains(entityId))
                            shadowVisibleLights.insert(entityId);
                    }
                    hasShadowFilter = true;
                }
            }

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

        if (useLightOcclusionCulling && lightOcclusionCulling && lightOcclusionCulling->isInitialized())
        {
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
                // cameraPosition is vec4: xyz = position, w = nearPlane
                lightOcclusionCulling->updateCamera(viewProj, glm::vec3(camData.cameraPosition), camData.cameraPosition.w);

                lightOcclusionCulling->cull(cmd);
                lightOcclusionCulling->copyResultsToStaging(cmd);
            }
        }

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

        if (shadowSystem && shadowSystem->isShadowsEnabled())
        {
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
                shadowParams.drawCountStructSize = sizeof(BatchDrawStats);
            }

            shadow::TerrainShadowPassParams terrainShadowParams{};
            shadow::TerrainShadowPassParams* terrainShadowParamsPtr = nullptr;

            if (terrainRenderingEnabled && terrainPipeline && terrainMeshBuffer &&
                terrainMeshBuffer->isInitialized() && terrainPipeline->getCurrentTileCount() > 0)
            {
                // Terrain buffer descriptors are already updated in updatePipelineDescriptors()
                // Just verify descriptor sets are valid before using them
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

    bool GPUDrivenRenderer::registerMaterialTextures(const std::string& materialPath)
    {
        if (!initialized || !bindlessTextures || !materialTextureCache)
        {
            return false;
        }

        if (registeredMaterialPaths.contains(materialPath))
        {
            return true;
        }

        mesh::ExtractedPBRValues pbrValues;

        if (material::isInstanceFile(materialPath))
        {
            auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
            if (!instanceData || instanceData->parentMaterialPath.empty())
            {
                loggerWarning("GPUDrivenRenderer: Failed to load material instance: {}", materialPath);
                return false;
            }

            auto parentMatData = resource::ResourceManager::loadMaterial(instanceData->parentMaterialPath);
            if (!parentMatData)
            {
                loggerWarning("GPUDrivenRenderer: Failed to load parent material: {}",
                              instanceData->parentMaterialPath);
                return false;
            }
            loadedMaterials[instanceData->parentMaterialPath] = parentMatData;

            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromInstance(*instanceData, *parentMatData);
        }
        else
        {
            auto matData = resource::ResourceManager::loadMaterial(materialPath);
            if (!matData)
            {
                loggerWarning("GPUDrivenRenderer: Failed to load material: {}", materialPath);
                return false;
            }
            loadedMaterials[materialPath] = matData;

            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData);
        }

        bool registered = false;

        auto tryRegister = [&](const std::string& texPath)
        {
            if (texPath.empty()) return;

            if (!materialTextureCache->loadTexture(texPath))
            {
                return;
            }

            vk::ImageView view = materialTextureCache->getViewForPath(texPath);
            vk::Sampler sampler = materialTextureCache->getSamplerForPath(texPath);

            if (view && sampler)
            {
                bindlessTextures->registerTexture(texPath, view, sampler);
                registered = true;
            }
        };

        tryRegister(pbrValues.albedoTexturePath);
        tryRegister(pbrValues.normalTexturePath);
        tryRegister(pbrValues.ormTexturePath);
        tryRegister(pbrValues.metallicTexturePath);
        tryRegister(pbrValues.roughnessTexturePath);
        tryRegister(pbrValues.aoTexturePath);
        tryRegister(pbrValues.emissionTexturePath);
        tryRegister(pbrValues.heightTexturePath);

        if (registered)
        {
            registeredMaterialPaths.insert(materialPath);
        }

        return registered;
    }

    void GPUDrivenRenderer::registerTerrainLayerTextures(const std::string& materialPath)
    {
        if (materialPath.empty() || materialPath == currentTerrainMaterialPath_)
        {
            return;
        }

        if (!bindlessTextures || !materialTextureCache)
        {
            return;
        }

        auto materialData = resource::ResourceManager::loadTerrainMaterial(materialPath);
        if (!materialData)
        {
            return;
        }

        terrainLayerData_.clear();
        terrainLayerData_.resize(materialData->activeLayerCount);

        for (uint8_t i = 0; i < materialData->activeLayerCount; ++i)
        {
            const auto& layer = materialData->layers[i];
            TerrainLayerGPUData& gpuLayer = terrainLayerData_[i];
            gpuLayer = {};

            auto tryRegisterLayerTex = [&](const std::string& texPath) -> uint32_t
            {
                if (texPath.empty()) return 0;
                if (!materialTextureCache->loadTexture(texPath)) return 0;
                vk::ImageView view = materialTextureCache->getViewForPath(texPath);
                vk::Sampler sampler = materialTextureCache->getSamplerForPath(texPath);
                if (!view || !sampler) return 0;
                return bindlessTextures->registerTexture(texPath, view, sampler);
            };

            gpuLayer.albedoTextureIndex = tryRegisterLayerTex(layer.albedoTexturePath);
            gpuLayer.normalTextureIndex = tryRegisterLayerTex(layer.normalTexturePath);

            gpuLayer.tilingScale = layer.tilingScale;
        }

        if (terrainPipeline)
        {
            terrainPipeline->updateTerrainLayerInfo(terrainLayerData_);
        }

        currentTerrainMaterialPath_ = materialPath;
        loggerInfo("GPUDrivenRenderer: Registered {} terrain layer textures from '{}'",
                   materialData->activeLayerCount, materialPath);
    }

    void GPUDrivenRenderer::updateHiZPyramid(vk::ImageView hiZView, vk::Sampler hiZSampler, uint32_t mipLevels)
    {
        if (!initialized)
        {
            return;
        }

        hiZMipLevels = mipLevels;

        if (cullPipeline && hiZView && hiZSampler)
        {
            cullPipeline->updateHiZDescriptor(hiZView, hiZSampler);
        }
    }

    uint32_t GPUDrivenRenderer::getMergedVertexCount() const
    {
        return mergedBuffer ? mergedBuffer->getTotalVertexCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getMergedIndexCount() const
    {
        return mergedBuffer ? mergedBuffer->getTotalIndexCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getRegisteredMeshCount() const
    {
        return mergedBuffer ? static_cast<uint32_t>(mergedBuffer->getRegisteredMeshes().size()) : 0;
    }

    uint32_t GPUDrivenRenderer::getRegisteredTextureCount() const
    {
        return bindlessTextures ? bindlessTextures->getRegisteredTextureCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getBatchCount() const
    {
        return batchManager ? batchManager->getBatchCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getCommandsPerBatch() const
    {
        return batchManager ? batchManager->getCommandsPerBatch() : 0;
    }

    uint32_t GPUDrivenRenderer::getTotalCapacity() const
    {
        return batchManager ? batchManager->getTotalCapacity() : 0;
    }

    uint64_t GPUDrivenRenderer::getDrawCommandBufferSize() const
    {
        return batchManager ? batchManager->getCombinedDrawCommandBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getDrawCountBufferSize() const
    {
        return batchManager ? batchManager->getCombinedDrawCountBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getPerDrawDataBufferSize() const
    {
        return batchManager ? batchManager->getCombinedPerDrawDataBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getTotalMemoryUsage() const
    {
        if (!batchManager) return 0;
        return batchManager->getCombinedDrawCommandBufferSize() +
            batchManager->getCombinedDrawCountBufferSize() +
            batchManager->getCombinedPerDrawDataBufferSize();
    }

    void GPUDrivenRenderer::updateStatsFromGPU()
    {
        if (!initialized || !enabled || !batchManager)
        {
            return;
        }

        GPUDrivenStats aggregated = batchManager->readBackAggregatedStats();

        stats.visibleObjects = aggregated.visibleObjects;
        stats.drawCalls = aggregated.drawCalls;

        stats.objectsLOD0 = aggregated.objectsLOD0;
        stats.objectsLOD1 = aggregated.objectsLOD1;
        stats.objectsLOD2 = aggregated.objectsLOD2;
        stats.objectsLOD3 = aggregated.objectsLOD3;

        stats.culledByFrustum = aggregated.culledByFrustum;
        stats.culledByOcclusion = aggregated.culledByOcclusion;
    }

    MeshletCullingStats GPUDrivenRenderer::getMeshletCullingStats()
    {
        if (!meshShaderPipeline)
        {
            return MeshletCullingStats{};
        }
        return meshShaderPipeline->readStats();
    }

    void GPUDrivenRenderer::setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights)
    {
        visibleLightIds.clear();
        visibleLightIds.insert(visibleLights.begin(), visibleLights.end());
        useBVHLightCulling = true;
    }

    void GPUDrivenRenderer::clearVisibleLights()
    {
        visibleLightIds.clear();
        useBVHLightCulling = false;
    }

    void GPUDrivenRenderer::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        if (shadowSystem)
        {
            shadowSystem->setDeletionQueue(queue);
        }
    }

    void GPUDrivenRenderer::initLightOcclusionCulling(occlusion::HiZBuffer* hiZBuffer)
    {
        if (!hiZBuffer)
        {
            loggerWarning("GPUDrivenRenderer: Cannot init light occlusion culling - HiZBuffer is null");
            return;
        }

        lightOcclusionCulling = std::make_unique<occlusion::LightOcclusionCulling>(device, swapChain);
        lightOcclusionCulling->init(hiZBuffer);
        useLightOcclusionCulling = true;

        loggerInfo("GPUDrivenRenderer: Light occlusion culling initialized");
    }

    void GPUDrivenRenderer::readBackLightOcclusionResults()
    {
        if (!useLightOcclusionCulling || !lightOcclusionCulling || !lightOcclusionCulling->isInitialized())
        {
            return;
        }

        lightOcclusionCulling->markResultsReady();

        const auto& visibleLights = lightOcclusionCulling->getVisibleLightIds();

        prevFrameOccludedLights = lightOcclusionCulling->getOccludedLightIds();
        hasPrevFrameOcclusionData = true;

        lightsAfterHiZCull = static_cast<uint32_t>(visibleLights.size());
    }

    void GPUDrivenRenderer::updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout)
    {
        if (!initialized) return;

        bool renderPassChanged = (cachedRenderPass != newRenderPass);
        bool iblLayoutChanged = (newIBLLayout && cachedIBLLayout != newIBLLayout);

        if (!renderPassChanged && !iblLayoutChanged) return;

        loggerInfo("GPUDrivenRenderer: Updating render pass/IBL layout, recreating pipelines");

        cachedRenderPass = newRenderPass;
        if (newIBLLayout)
        {
            cachedIBLLayout = newIBLLayout;
        }

        bool canRecreate = true;
        if (!meshShaderPipeline)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: meshShaderPipeline is null");
            canRecreate = false;
        }
        if (!boneMatrixManager)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: boneMatrixManager is null");
            canRecreate = false;
        }
        if (!lightBufferManager)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: lightBufferManager is null");
            canRecreate = false;
        }
        if (!clusterGridManager)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: clusterGridManager is null");
            canRecreate = false;
        }
        if (!lightCullingPipeline)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: lightCullingPipeline is null");
            canRecreate = false;
        }
        if (!bindlessTextures)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: bindlessTextures is null");
            canRecreate = false;
        }

        if (canRecreate && shadowSystem)
        {
            meshShaderPipeline->recreate(cachedIBLLayout,
                                         bindlessTextures->getDescriptorSetLayout(),
                                         boneMatrixManager->getDescriptorSetLayout(),
                                         lightBufferManager->getDescriptorSetLayout(),
                                         clusterGridManager->getDescriptorSetLayout(),
                                         lightCullingPipeline->getDescriptorSetLayout(),
                                         shadowSystem->getShadowDataLayout(),
                                         shadowSystem->getShadowTextureLayout(),
                                         cachedRenderPass);
        }
        else
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: Cannot recreate pipeline due to missing components");
        }
    }

    uint32_t GPUDrivenRenderer::getTotalSceneLights() const
    {
        return totalSceneLights;
    }

    uint32_t GPUDrivenRenderer::getLightsAfterBVHCull() const
    {
        return lightsAfterBVHCull;
    }

    uint32_t GPUDrivenRenderer::getLightsAfterHiZCull() const
    {
        return lightsAfterHiZCull;
    }

    void GPUDrivenRenderer::setTerrainFrustumCullingEnabled(bool enabled)
    {
        terrainFrustumCullingEnabled = enabled;
        if (terrainPipeline)
        {
            terrainPipeline->setFrustumCullingEnabled(enabled);
        }
    }

    void GPUDrivenRenderer::setTerrainMeshletCullingEnabled(bool enabled)
    {
        terrainMeshletCullingEnabled = enabled;
        if (terrainPipeline)
        {
            terrainPipeline->setMeshletCullingEnabled(enabled);
        }
    }

    void GPUDrivenRenderer::updateTerrain(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                          const glm::vec3& cameraPosition,
                                          const std::string& terrainMaterialPath)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();

        if (!initialized || !terrainRenderingEnabled || !terrainAdapter || !terrainPipeline)
        {
            return;
        }

        if (!terrainMaterialPath.empty())
        {
            registerTerrainLayerTextures(terrainMaterialPath);
        }

        if (visibleTiles.empty())
        {
            terrainTileData.clear();
            return;
        }

        auto streamStart = std::chrono::high_resolution_clock::now();

        if (terrainStreamManager)
        {
            terrainStreamManager->update(visibleTiles, cameraPosition);
        }
        else
        {
            // Fallback: upload all tiles directly (legacy behavior)
            for (terrain::TerrainTile* tile : visibleTiles)
            {
                if (!tile || !tile->isVisible)
                {
                    continue;
                }

                TerrainTileKey key{tile->coord.x, tile->coord.z};
                if (!terrainAdapter->hasTile(key))
                {
                    terrainAdapter->uploadTile(*tile);
                }
            }
        }

        auto streamEnd = std::chrono::high_resolution_clock::now();
        terrainStreamingUs_ = std::chrono::duration<float, std::micro>(streamEnd - streamStart).count();

        // Always rebuild tile data from current visible set — the build itself
        // is cheap (vector population from existing allocations) and the previous
        // first/last/count heuristic missed mid-set visibility changes.
        terrainAdapter->markGPUTileDataDirty();

        auto buildStart = std::chrono::high_resolution_clock::now();
        const auto& newTileData = terrainAdapter->buildGPUTileData(visibleTiles);
        auto buildEnd = std::chrono::high_resolution_clock::now();
        terrainBuildTileDataUs_ = std::chrono::duration<float, std::micro>(buildEnd - buildStart).count();

        auto uploadStart = std::chrono::high_resolution_clock::now();

        if (!newTileData.empty())
        {
            terrainTileData = newTileData;
            terrainPipeline->updateTileData(terrainTileData);
        }
        else
        {
            terrainTileData.clear();
        }

        auto uploadEnd = std::chrono::high_resolution_clock::now();
        terrainUploadTileDataUs_ = std::chrono::duration<float, std::micro>(uploadEnd - uploadStart).count();

        terrainUpdateUs_ = std::chrono::duration<float, std::micro>(uploadEnd - frameStart).count();
    }

    void GPUDrivenRenderer::clearTerrainData()
    {
        if (terrainStreamManager)
        {
            terrainStreamManager->clear();
        }
        else if (terrainAdapter)
        {
            terrainAdapter->clear();
        }
        terrainTileData.clear();
        currentTerrainMaterialPath_.clear();

        // Reset terrain pipeline tile count to prevent rendering stale data
        if (terrainPipeline)
        {
            terrainPipeline->updateTileData({});
        }

        if (terrainMeshBuffer)
        {
            terrainMeshBuffer->clear();
        }
    }

    void GPUDrivenRenderer::setBrushOverlay(const glm::vec2& worldPos, float worldRadius, float falloff, float shape)
    {
        if (terrainPipeline)
        {
            terrainPipeline->setBrushOverlay(worldPos, worldRadius, falloff, shape);
        }
    }

    const TerrainStreamingStats* GPUDrivenRenderer::getTerrainStreamingStats() const
    {
        if (terrainStreamManager)
        {
            return &terrainStreamManager->getStats();
        }
        return nullptr;
    }

    TerrainCullingStats GPUDrivenRenderer::getTerrainCullingStats()
    {
        if (terrainPipeline)
        {
            return terrainPipeline->readStats();
        }
        return {};
    }

    void GPUDrivenRenderer::renderTerrainDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!initialized || !terrainRenderingEnabled || !terrainPipeline || !meshShaderPipeline)
        {
            return;
        }

        if (terrainTileData.empty())
        {
            return;
        }

        // Terrain buffer descriptors are already updated in updatePipelineDescriptors()
        terrainPipeline->updateSharedDescriptors(
            iblDescriptorSet,
            bindlessTextures->getDescriptorSet(),
            lightBufferManager->getDescriptorSet(),
            clusterGridManager->getDescriptorSet(),
            lightCullingPipeline->getDescriptorSet(),
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{}
        );

        auto extent = swapChain.getSwapchainExtent();

        uint32_t viewMode = currentViewMode;
        if (meshletFrustumCullingEnabled) viewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        if (meshletBackfaceCullingEnabled) viewMode |= TERRAIN_CULL_BACKFACE_BIT;

        terrainPipeline->dispatch(
            cmd,
            viewMode,
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            terrainLODBias,
            terrainErrorThreshold,
            terrainTextureScale
        );
    }

}
