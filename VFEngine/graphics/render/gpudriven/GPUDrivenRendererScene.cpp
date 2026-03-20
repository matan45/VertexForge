#include "GPUDrivenRenderer.hpp"
#include "../occlusion/HiZBuffer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "../../animation/AnimatorStateMachine.hpp"
#include "../../core/Texture.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "resource/Types.hpp"
#include "asset/AssetRef.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include "../../core/SwapChain.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <unordered_map>

namespace render::gpudriven
{
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

        // Update texture mip streaming distances and state
        if (textureStreamManager)
        {
            static uint64_t textureStreamFrame = 0;

            // Compute min distance from camera to each streamed texture
            textureStreamManager->resetDistances();
            for (const auto& meshRender : opaqueObjects)
            {
                glm::vec3 objPos = glm::vec3(meshRender.modelMatrix[3]);
                float dist = glm::length(objPos - cameraPosition);

                auto updateTexDist = [&](const std::string& matPath)
                {
                    if (matPath.empty()) return;
                    auto it = materials.pbrCache.find(matPath);
                    if (it == materials.pbrCache.end()) return;
                    const auto& pbr = it->second;
                    const std::string* paths[] = {
                        &pbr.albedoTexturePath, &pbr.normalTexturePath, &pbr.ormTexturePath,
                        &pbr.metallicTexturePath, &pbr.roughnessTexturePath, &pbr.aoTexturePath,
                        &pbr.emissionTexturePath, &pbr.heightTexturePath
                    };
                    for (const auto* p : paths)
                    {
                        if (!p->empty())
                            textureStreamManager->updateTextureDistance(*p, dist);
                    }
                };

                updateTexDist(meshRender.defaultMaterialPath);
                for (const auto& [name, subMat] : meshRender.submeshMaterials)
                {
                    updateTexDist(subMat.materialPath);
                }
            }

            textureStreamManager->update(cameraPosition, textureStreamFrame++);
        }

        TextureIndexResolver textureResolver = createTextureResolver();
        ShaderGroupResolver shaderGroupResolver = [this](const std::string& materialPath) -> uint32_t {
            if (materialPath.empty()) return 0;
            auto it = materials.pbrCache.find(materialPath);
            if (it != materials.pbrCache.end())
            {
                if (it->second.blendMode == material::BlendMode::Translucent)
                    return SHADER_GROUP_TRANSPARENT;
                if (it->second.blendMode == material::BlendMode::Additive ||
                    it->second.blendMode == material::BlendMode::Multiply)
                    return SHADER_GROUP_BLEND;
            }
            return 0;
        };
        BoneOffsetResolver boneOffsetResolver = updateAnimationBones();

        ObjectResolvers resolvers{textureResolver, shaderGroupResolver, boneOffsetResolver, time, cameraPosition};

        bool useStreaming = objectStreamingEnabled && objectStreamManager
                           && objectStreamManager->getStats().totalRegistered > 0;

        if (useStreaming)
        {
            if (!mergedBuffer->isPersistentMode())
            {
                mergedBuffer->setPersistentMode(true);
            }
            auto& registry = scene::EntityRegistry::getRegistry();
            objectStreamManager->update(cameraPosition, resolvers, registry);
        }
        else
        {
            if (mergedBuffer->isPersistentMode())
            {
                mergedBuffer->setPersistentMode(false);
            }
            mergedBuffer->updateObjects(opaqueObjects, resolvers);
        }

        uint32_t objectCount = useStreaming
            ? mergedBuffer->getActiveObjectCount()
            : mergedBuffer->getObjectCount();

        CameraUpdateParams cameraParams{
            .view = view,
            .projection = projection,
            .cameraPosition = cameraPosition,
            .nearPlane = nearPlane,
            .farPlane = farPlane,
            .time = time,
            .objectCount = objectCount,
            .hiZMipLevels = hiZMipLevels,
            .frustumCullingEnabled = culling.frustumCullingEnabled,
            .occlusionCullingEnabled = culling.occlusionCullingEnabled,
            .lodSelectionEnabled = culling.lodSelectionEnabled,
            .lodCrossfadeEnabled = culling.lodCrossfadeEnabled,
            .distanceCullingEnabled = culling.distanceCullingEnabled,
            .categoryDistances = {culling.categoryDistances[0], culling.categoryDistances[1], culling.categoryDistances[2], culling.categoryDistances[3], culling.categoryDistances[4], culling.categoryDistances[5], culling.categoryDistances[6]},
            .shadowDistanceMultiplier = culling.shadowDistanceMultiplier,
            .globalLodBias = culling.globalLodBias,
            .batchManager = batchManager.get()
        };
        cameraBuffer->update(cameraParams);

        cachedCamera.view = view;
        cachedCamera.projection = projection;
        cachedCamera.position = cameraPosition;
        cachedCamera.nearPlane = nearPlane;
        cachedCamera.farPlane = farPlane;
        cachedCamera.time = time;

        if (terrain.pipeline)
        {
            terrain.pipeline->setViewProjection(projection * view);
        }

        updateClusterGrid(projection, nearPlane, farPlane);
        updatePipelineDescriptors();

        // Phase 3: Detect scene changes for shadow page invalidation
        uint32_t currentObjectCount = objectCount;
        if (shadowSystem && shadowSystem->isInitialized())
        {
            if (currentObjectCount != stats.totalObjects)
            {
                shadowSystem->notifySceneChanged();
            }
        }

        stats.totalObjects = currentObjectCount;
    }

    void GPUDrivenRenderer::updateCameraForRTT(const RTTCameraParams& params)
    {
        if (!initialized || !enabled)
        {
            return;
        }

        CameraUpdateParams cameraParams{
            .view = params.view,
            .projection = params.projection,
            .cameraPosition = params.cameraPosition,
            .nearPlane = params.nearPlane,
            .farPlane = params.farPlane,
            .time = 0.0f,
            .objectCount = mergedBuffer ? mergedBuffer->getObjectCount() : 0,
            .hiZMipLevels = hiZMipLevels,
            .frustumCullingEnabled = culling.frustumCullingEnabled,
            .occlusionCullingEnabled = false,  // No HiZ data for RTT
            .lodSelectionEnabled = culling.lodSelectionEnabled,
            .lodCrossfadeEnabled = culling.lodCrossfadeEnabled,
            .distanceCullingEnabled = culling.distanceCullingEnabled,
            .categoryDistances = {culling.categoryDistances[0], culling.categoryDistances[1], culling.categoryDistances[2], culling.categoryDistances[3], culling.categoryDistances[4], culling.categoryDistances[5], culling.categoryDistances[6]},
            .shadowDistanceMultiplier = culling.shadowDistanceMultiplier,
            .globalLodBias = culling.globalLodBias,
            .batchManager = batchManager.get(),
            .screenWidth = params.screenWidth,
            .screenHeight = params.screenHeight
        };
        cameraBuffer->update(cameraParams);

        if (terrain.pipeline)
        {
            terrain.pipeline->setViewProjection(params.projection * params.view);
        }
    }

    void GPUDrivenRenderer::restoreMainCamera()
    {
        if (!initialized || !enabled)
        {
            return;
        }

        CameraUpdateParams cameraParams{
            .view = cachedCamera.view,
            .projection = cachedCamera.projection,
            .cameraPosition = cachedCamera.position,
            .nearPlane = cachedCamera.nearPlane,
            .farPlane = cachedCamera.farPlane,
            .time = cachedCamera.time,
            .objectCount = mergedBuffer ? mergedBuffer->getObjectCount() : 0,
            .hiZMipLevels = hiZMipLevels,
            .frustumCullingEnabled = culling.frustumCullingEnabled,
            .occlusionCullingEnabled = culling.occlusionCullingEnabled,
            .lodSelectionEnabled = culling.lodSelectionEnabled,
            .lodCrossfadeEnabled = culling.lodCrossfadeEnabled,
            .distanceCullingEnabled = culling.distanceCullingEnabled,
            .categoryDistances = {culling.categoryDistances[0], culling.categoryDistances[1], culling.categoryDistances[2], culling.categoryDistances[3], culling.categoryDistances[4], culling.categoryDistances[5], culling.categoryDistances[6]},
            .shadowDistanceMultiplier = culling.shadowDistanceMultiplier,
            .globalLodBias = culling.globalLodBias,
            .batchManager = batchManager.get()
        };
        cameraBuffer->update(cameraParams);

        if (terrain.pipeline)
        {
            terrain.pipeline->setViewProjection(cachedCamera.projection * cachedCamera.view);
        }
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

    void GPUDrivenRenderer::releaseMeshAsset(const std::string& meshPath)
    {
        if (meshStreamManager)
        {
            meshStreamManager->unrequestMesh(meshPath);
        }
    }

    void GPUDrivenRenderer::releaseTextureAsset(const std::string& texturePath)
    {
        if (bindlessTextures)
        {
            bindlessTextures->unregisterTexture(texturePath);
        }
        if (materials.textureCache)
        {
            materials.textureCache->unloadTexture(texturePath);
        }

        // Invalidate any materials that used this texture so they re-register on next use
        std::vector<std::string> materialsToInvalidate;
        for (const auto& [matPath, pbrValues] : materials.pbrCache)
        {
            if (pbrValues.albedoTexturePath == texturePath ||
                pbrValues.normalTexturePath == texturePath ||
                pbrValues.ormTexturePath == texturePath ||
                pbrValues.metallicTexturePath == texturePath ||
                pbrValues.roughnessTexturePath == texturePath ||
                pbrValues.aoTexturePath == texturePath ||
                pbrValues.emissionTexturePath == texturePath ||
                pbrValues.heightTexturePath == texturePath)
            {
                materialsToInvalidate.push_back(matPath);
            }
        }
        for (const auto& matPath : materialsToInvalidate)
        {
            materials.registeredPaths.erase(matPath);
            materials.pbrCache.erase(matPath);
        }
    }

    void GPUDrivenRenderer::releaseMaterialAsset(const std::string& materialPath)
    {
        if (materials.textureCache)
        {
            materials.textureCache->invalidateMaterialDescriptorSet(materialPath);
        }
        materials.registeredPaths.erase(materialPath);
        materials.pbrCache.erase(materialPath);
        materials.loaded.erase(materialPath);
    }

    void GPUDrivenRenderer::registerSceneMaterialTextures(const std::vector<mesh::MeshRenderData>& opaqueObjects)
    {
        if (!materials.textureCache || !bindlessTextures)
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

            auto it = materials.pbrCache.find(materialPath);
            if (it == materials.pbrCache.end())
            {
                it = materials.pbrCache.emplace(materialPath,
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

            if (registry.all_of<components::PhysicsAnimationComponent>(entity))
            {
                const auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(entity);
                if (!physAnimComp.overrideBoneMatrices.empty())
                {
                    uint32_t boneCount = static_cast<uint32_t>(physAnimComp.overrideBoneMatrices.size());
                    boneMatrixManager->allocate(entity, boneCount);
                    boneMatrixManager->updateBoneMatrices(entity, physAnimComp.overrideBoneMatrices);
                    continue;
                }
            }

            if (!meshComp.animatorRef.isValid())
            {
                continue;
            }

            animation::AnimatorStateMachine* animator = animatorSystem.getAnimator(entity);
            if (!animator)
            {
                animatorSystem.initializeEntityAnimator(entity, meshComp.animatorRef.resolve());
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

    std::pair<vk::ImageView, vk::Sampler> GPUDrivenRenderer::getHiZViewSampler() const
    {
        if (prepassHiZ && prepassHiZ->isInitialized())
            return {prepassHiZ->getHiZImageView(), prepassHiZ->getHiZSampler()};
        if (bindlessTextures && bindlessTextures->hasDefaultTexture())
            return {bindlessTextures->getDefaultImageView(), bindlessTextures->getDefaultSampler()};
        return {nullptr, nullptr};
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
            batchManager->getCombinedDrawCountBuffer(),
            mergedBuffer->getActiveIndexBuffer()
        );

        bool hasMeshes = mergedBuffer->getObjectCount() > 0;

        if (meshShaderPipeline)
        {
            meshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            meshShaderPipeline->updateVertexDescriptors(*mergedBuffer);
            meshShaderPipeline->updateInstanceTransformDescriptor(mergedBuffer->getInstanceTransformBuffer());
            meshShaderPipeline->updateObjectBufferDescriptor(mergedBuffer->getObjectBuffer());
        }

        if (transparentMeshShaderPipeline)
        {
            transparentMeshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            transparentMeshShaderPipeline->updateVertexDescriptors(*mergedBuffer);
            transparentMeshShaderPipeline->updateInstanceTransformDescriptor(mergedBuffer->getInstanceTransformBuffer());
            transparentMeshShaderPipeline->updateObjectBufferDescriptor(mergedBuffer->getObjectBuffer());
        }

        if (wboitMeshShaderPipeline)
        {
            wboitMeshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            wboitMeshShaderPipeline->updateVertexDescriptors(*mergedBuffer);
            wboitMeshShaderPipeline->updateInstanceTransformDescriptor(mergedBuffer->getInstanceTransformBuffer());
            wboitMeshShaderPipeline->updateObjectBufferDescriptor(mergedBuffer->getObjectBuffer());
        }

        // Initialize Hi-Z descriptors with prepass texture or default fallback
        {
            auto [hiZView, hiZSampler] = getHiZViewSampler();
            if (hiZView && hiZSampler)
            {
                if (meshShaderPipeline)
                    meshShaderPipeline->updateHiZDescriptor(hiZView, hiZSampler);
                if (transparentMeshShaderPipeline)
                    transparentMeshShaderPipeline->updateHiZDescriptor(hiZView, hiZSampler);
                if (wboitMeshShaderPipeline)
                    wboitMeshShaderPipeline->updateHiZDescriptor(hiZView, hiZSampler);
            }
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

        if (transparentMeshShaderPipeline && hasMeshes)
        {
            transparentMeshShaderPipeline->updatePerDrawDescriptor(batchManager->getCombinedPerDrawDataBuffer());

            if (shadowSystem && shadowSystem->isInitialized())
            {
                transparentMeshShaderPipeline->updateShadowDescriptors(
                    shadowSystem->getShadowDataDescSet(),
                    shadowSystem->getShadowTextureDescSet());
            }
        }

        if (wboitMeshShaderPipeline && hasMeshes)
        {
            wboitMeshShaderPipeline->updatePerDrawDescriptor(batchManager->getCombinedPerDrawDataBuffer());

            if (shadowSystem && shadowSystem->isInitialized())
            {
                wboitMeshShaderPipeline->updateShadowDescriptors(
                    shadowSystem->getShadowDataDescSet(),
                    shadowSystem->getShadowTextureDescSet());
            }
        }

        if (meshShaderPipeline && lightBufferManager && clusterGridManager && lightCullingPipeline)
        {
            meshShaderPipeline->updateLightingDescriptors(
                lightBufferManager->getDescriptorSet(),
                clusterGridManager->getDescriptorSet(),
                lightCullingPipeline->getDescriptorSet());
        }

        if (transparentMeshShaderPipeline && lightBufferManager && clusterGridManager && lightCullingPipeline)
        {
            transparentMeshShaderPipeline->updateLightingDescriptors(
                lightBufferManager->getDescriptorSet(),
                clusterGridManager->getDescriptorSet(),
                lightCullingPipeline->getDescriptorSet());
        }

        if (wboitMeshShaderPipeline && lightBufferManager && clusterGridManager && lightCullingPipeline)
        {
            wboitMeshShaderPipeline->updateLightingDescriptors(
                lightBufferManager->getDescriptorSet(),
                clusterGridManager->getDescriptorSet(),
                lightCullingPipeline->getDescriptorSet());
        }

        if (terrain.renderingEnabled && terrain.pipeline && terrain.meshBuffer &&
            terrain.meshBuffer->isInitialized() && terrain.pipeline->getCurrentTileCount() > 0)
        {
            terrain.pipeline->updateTerrainBufferDescriptors(*terrain.meshBuffer);
            terrain.pipeline->updateWeightMapDescriptor(terrain.meshBuffer->getWeightMapBuffer());

            auto [hiZView, hiZSampler] = getHiZViewSampler();
            if (hiZView && hiZSampler)
                terrain.pipeline->updateHiZDescriptor(hiZView, hiZSampler);
        }

        if (shadowSystem && shadowSystem->isInitialized() && cameraBuffer)
        {
            shadowSystem->updateCameraDescriptor(cameraBuffer->getBuffer(),
                                                  static_cast<vk::DeviceSize>(sizeof(GPUCameraData)));
        }
    }

    bool GPUDrivenRenderer::registerMaterialTextures(const std::string& materialPath)
    {
        if (!initialized || !bindlessTextures || !materials.textureCache)
        {
            return false;
        }

        if (materials.registeredPaths.contains(materialPath))
        {
            return true;
        }

        mesh::ExtractedPBRValues pbrValues;

        if (material::isInstanceFile(materialPath))
        {
            auto instanceData = resource::ResourceManager::loadMaterialInstance(asset::AssetRef::fromPath(materialPath));
            if (!instanceData || !instanceData->parentMaterialRef.isValid())
            {
                vfLogWarning("GPUDrivenRenderer: Failed to load material instance: {}", materialPath);
                return false;
            }

            auto parentMatData = resource::ResourceManager::loadMaterial(instanceData->parentMaterialRef);
            if (!parentMatData)
            {
                vfLogWarning("GPUDrivenRenderer: Failed to load parent material: {}",
                              instanceData->parentMaterialRef.resolve());
                return false;
            }
            materials.loaded[instanceData->parentMaterialRef.resolve()] = parentMatData;

            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromInstance(*instanceData, *parentMatData);
        }
        else
        {
            auto matData = resource::ResourceManager::loadMaterial(asset::AssetRef::fromPath(materialPath));
            if (!matData)
            {
                vfLogWarning("GPUDrivenRenderer: Failed to load material: {}", materialPath);
                return false;
            }
            materials.loaded[materialPath] = matData;

            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData);
        }

        bool registered = false;

        auto tryRegister = [&](const std::string& texPath, vk::Format format = vk::Format::eR8G8B8A8Unorm)
        {
            if (texPath.empty()) return;

            // Try mip-streaming path for .vfImage files
            if (textureStreamManager && texPath.ends_with(".vfImage"))
            {
                uint32_t idx = textureStreamManager->registerTexture(texPath, format);
                if (idx != INVALID_TEXTURE_INDEX)
                {
                    registered = true;
                    return;
                }
                // Fall through to legacy path on failure
            }

            if (!materials.textureCache->loadTexture(texPath, format))
            {
                return;
            }

            vk::ImageView view = materials.textureCache->getViewForPath(texPath);
            vk::Sampler sampler = materials.textureCache->getSamplerForPath(texPath);

            if (view && sampler)
            {
                bindlessTextures->registerTexture(texPath, view, sampler);
                registered = true;
            }
        };

        tryRegister(pbrValues.albedoTexturePath, vk::Format::eR8G8B8A8Srgb);
        tryRegister(pbrValues.normalTexturePath);
        tryRegister(pbrValues.ormTexturePath);
        tryRegister(pbrValues.metallicTexturePath);
        tryRegister(pbrValues.roughnessTexturePath);
        tryRegister(pbrValues.aoTexturePath);
        tryRegister(pbrValues.emissionTexturePath, vk::Format::eR8G8B8A8Srgb);
        tryRegister(pbrValues.heightTexturePath);

        if (registered)
        {
            materials.registeredPaths.insert(materialPath);

            registerTextureDependencies(materialPath, {
                pbrValues.albedoTexturePath,
                pbrValues.normalTexturePath,
                pbrValues.ormTexturePath,
                pbrValues.metallicTexturePath,
                pbrValues.roughnessTexturePath,
                pbrValues.aoTexturePath,
                pbrValues.emissionTexturePath,
                pbrValues.heightTexturePath
            });
        }

        return registered;
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

    const TextureStreamStats* GPUDrivenRenderer::getTextureStreamStats() const
    {
        if (textureStreamManager)
        {
            return &textureStreamManager->getStats();
        }
        return nullptr;
    }

    void GPUDrivenRenderer::registerTextureDependencies(const std::string& materialPath,
                                                         const std::vector<std::string>& texturePaths)
    {
        auto& lifecycle = resource::AssetLifecycleManager::instance();
        auto matGUID = asset::AssetRef::fromPath(materialPath).getGUID();
        if (!lifecycle.isTracked(matGUID))
        {
            lifecycle.acquire(matGUID, resource::AssetType::Material);
        }
        for (const auto& texPath : texturePaths)
        {
            if (!texPath.empty())
                lifecycle.addDependency(matGUID, asset::AssetRef::fromPath(texPath).getGUID(), resource::AssetType::Texture);
        }
    }
}
