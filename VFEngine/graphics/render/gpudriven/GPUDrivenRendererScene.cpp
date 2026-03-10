#include "GPUDrivenRenderer.hpp"
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
#include "material/MaterialInstanceTypes.hpp"
#include "../../core/SwapChain.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "scene/EntityRegistry.hpp"
#include "lightbake/LightmapAtlas.hpp"
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
        registerSceneLightmapTextures(opaqueObjects);

        TextureIndexResolver textureResolver = createTextureResolver();
        LightmapIndexResolver lightmapResolver = createLightmapResolver();
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

        mergedBuffer->updateObjects(opaqueObjects, {textureResolver, shaderGroupResolver, boneOffsetResolver, lightmapResolver, time, cameraPosition});

        CameraUpdateParams cameraParams{
            .view = view,
            .projection = projection,
            .cameraPosition = cameraPosition,
            .nearPlane = nearPlane,
            .farPlane = farPlane,
            .time = time,
            .objectCount = mergedBuffer ? mergedBuffer->getObjectCount() : 0,
            .hiZMipLevels = hiZMipLevels,
            .frustumCullingEnabled = culling.frustumCullingEnabled,
            .occlusionCullingEnabled = culling.occlusionCullingEnabled,
            .lodSelectionEnabled = culling.lodSelectionEnabled,
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

        stats.totalObjects = mergedBuffer->getObjectCount();
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
        // Remove from registered set so it can be re-registered if needed again
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

    void GPUDrivenRenderer::registerSceneLightmapTextures(const std::vector<mesh::MeshRenderData>& opaqueObjects)
    {
        if (!bindlessTextures)
        {
            return;
        }

        for (const auto& meshRender : opaqueObjects)
        {
            if (meshRender.lightmapPath.empty())
            {
                continue;
            }

            if (materials.registeredLightmapPaths.contains(meshRender.lightmapPath))
            {
                continue;
            }

            auto lightmapData = lightbake::LightmapAtlas::load(meshRender.lightmapPath);
            if (lightmapData.width == 0 || lightmapData.height == 0 || lightmapData.texels.empty())
            {
                vfLogWarning("GPUDrivenRenderer: Failed to load lightmap: {}", meshRender.lightmapPath);
                materials.registeredLightmapPaths.insert(meshRender.lightmapPath);
                continue;
            }

            // Convert 3-channel (RGB) lightmap to 4-channel (RGBA) HDRData
            resource::HDRData hdrData;
            hdrData.width = lightmapData.width;
            hdrData.height = lightmapData.height;
            hdrData.numbersOfChannels = 4;
            hdrData.pixels.resize(lightmapData.width * lightmapData.height * 4);

            const uint32_t channels = lightmapData.channels;
            for (uint32_t i = 0; i < lightmapData.width * lightmapData.height; ++i)
            {
                hdrData.pixels[i * 4 + 0] = (channels > 0) ? lightmapData.texels[i * channels + 0] : 0.0f;
                hdrData.pixels[i * 4 + 1] = (channels > 1) ? lightmapData.texels[i * channels + 1] : 0.0f;
                hdrData.pixels[i * 4 + 2] = (channels > 2) ? lightmapData.texels[i * channels + 2] : 0.0f;
                hdrData.pixels[i * 4 + 3] = 1.0f;
            }

            auto texture = std::make_unique<core::Texture>(device);
            texture->loadHDRFromData(hdrData, false);

            vk::ImageView view = texture->getImageView();
            vk::Sampler sampler = texture->getSampler();

            if (view && sampler)
            {
                bindlessTextures->registerTexture(meshRender.lightmapPath, view, sampler);
                materials.lightmapTextureCache[meshRender.lightmapPath] = std::move(texture);
            }

            materials.registeredLightmapPaths.insert(meshRender.lightmapPath);
        }
    }

    LightmapIndexResolver GPUDrivenRenderer::createLightmapResolver()
    {
        if (!bindlessTextures)
        {
            return nullptr;
        }

        return [this](const std::string& lightmapPath) -> uint32_t
        {
            if (lightmapPath.empty())
            {
                return INVALID_TEXTURE_INDEX;
            }
            return bindlessTextures->getTextureIndex(lightmapPath);
        };
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
            auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
            if (!instanceData || instanceData->parentMaterialPath.empty())
            {
                vfLogWarning("GPUDrivenRenderer: Failed to load material instance: {}", materialPath);
                return false;
            }

            auto parentMatData = resource::ResourceManager::loadMaterial(instanceData->parentMaterialPath);
            if (!parentMatData)
            {
                vfLogWarning("GPUDrivenRenderer: Failed to load parent material: {}",
                              instanceData->parentMaterialPath);
                return false;
            }
            materials.loaded[instanceData->parentMaterialPath] = parentMatData;

            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromInstance(*instanceData, *parentMatData);
        }
        else
        {
            auto matData = resource::ResourceManager::loadMaterial(materialPath);
            if (!matData)
            {
                vfLogWarning("GPUDrivenRenderer: Failed to load material: {}", materialPath);
                return false;
            }
            materials.loaded[materialPath] = matData;

            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData);
        }

        bool registered = false;

        auto tryRegister = [&](const std::string& texPath)
        {
            if (texPath.empty()) return;

            if (!materials.textureCache->loadTexture(texPath))
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
            materials.registeredPaths.insert(materialPath);

            // Register texture dependencies so textures stay alive while material is in use
            auto& lifecycle = resource::AssetLifecycleManager::instance();
            // Ensure the material is tracked before adding dependencies
            // (the renderer may load materials that weren't yet acquired by the component service)
            if (!lifecycle.isTracked(materialPath))
            {
                lifecycle.acquire(materialPath, resource::AssetType::Material);
            }
            auto addDep = [&](const std::string& texPath) {
                if (!texPath.empty())
                    lifecycle.addDependency(materialPath, texPath, resource::AssetType::Texture);
            };
            addDep(pbrValues.albedoTexturePath);
            addDep(pbrValues.normalTexturePath);
            addDep(pbrValues.ormTexturePath);
            addDep(pbrValues.metallicTexturePath);
            addDep(pbrValues.roughnessTexturePath);
            addDep(pbrValues.aoTexturePath);
            addDep(pbrValues.emissionTexturePath);
            addDep(pbrValues.heightTexturePath);
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
}
