#include "GPUDrivenRenderer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "../../animation/AnimatorStateMachine.hpp"
#include "resource/ResourceManager.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include "../../core/SwapChain.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/Logger.hpp"
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

        TextureIndexResolver textureResolver = createTextureResolver();
        ShaderGroupResolver shaderGroupResolver = [this](const std::string& materialPath) -> uint32_t {
            if (materialPath.empty()) return 0;
            auto it = pbrCache.find(materialPath);
            if (it != pbrCache.end())
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
            .distanceCullingEnabled = distanceCullingEnabled,
            .categoryDistances = {categoryDistances[0], categoryDistances[1], categoryDistances[2], categoryDistances[3], categoryDistances[4]},
            .shadowDistanceMultiplier = shadowDistanceMultiplier,
            .globalLodBias = globalLodBias,
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
        }

        if (transparentMeshShaderPipeline)
        {
            transparentMeshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            transparentMeshShaderPipeline->updateVertexDescriptors(*mergedBuffer);
        }

        if (wboitMeshShaderPipeline)
        {
            wboitMeshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            wboitMeshShaderPipeline->updateVertexDescriptors(*mergedBuffer);
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

        if (terrainRenderingEnabled && terrainPipeline && terrainMeshBuffer &&
            terrainMeshBuffer->isInitialized() && terrainPipeline->getCurrentTileCount() > 0)
        {
            terrainPipeline->updateTerrainBufferDescriptors(*terrainMeshBuffer);
            terrainPipeline->updateWeightMapDescriptor(terrainMeshBuffer->getWeightMapBuffer());
        }

        if (shadowSystem && shadowSystem->isInitialized() && cameraBuffer)
        {
            shadowSystem->updateCameraDescriptor(cameraBuffer->getBuffer(),
                                                  static_cast<vk::DeviceSize>(sizeof(GPUCameraData)));
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
