#include "FramePreparationSystem.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include "SceneBVHManager.hpp"
#include "LightBVHManager.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "../../render/material/MaterialPBRExtractor.hpp"
#include "threading/ParallelCollect.hpp"

namespace controllers::offscreen
{
    const render::mesh::ExtractedPBRValues* FramePreparationSystem::getCachedPBRValues(const std::string& materialPath)
    {
        if (materialPath.empty()) return nullptr;

        auto it = pbrCache.find(materialPath);
        if (it != pbrCache.end()) return &it->second;

        auto [inserted, success] = pbrCache.emplace(materialPath,
                                                    render::mesh::MaterialPBRExtractor::extractPBRFromPath(
                                                        materialPath));
        return &inserted->second;
    }

    void FramePreparationSystem::populateMaterialInfo(render::mesh::SubMeshMaterialInfo& matInfo,
                                                      const std::string& materialPath)
    {
        matInfo.materialPath = materialPath;
        const auto* pbrValues = getCachedPBRValues(materialPath);
        if (pbrValues)
        {
            matInfo.albedo = pbrValues->albedo;
            matInfo.metallic = pbrValues->metallic;
            matInfo.roughness = pbrValues->roughness;
            matInfo.ao = pbrValues->ao;
            matInfo.emission = pbrValues->emission;
            matInfo.blendMode = static_cast<uint8_t>(pbrValues->blendMode);
            matInfo.opacity = pbrValues->opacity;
            matInfo.alphaCutoff = pbrValues->alphaCutoff;
            matInfo.iblDiffuse = pbrValues->iblDiffuse;
            matInfo.iblSpecular = pbrValues->iblSpecular;
        }
    }

    void FramePreparationSystem::invalidateMaterialCache(const std::string& materialPath)
    {
        pbrCache.erase(materialPath);
        instanceBatchCache.erase(materialPath);
    }

    void FramePreparationSystem::prepareMeshes(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        if (!renderHandler->isMeshPipelineInitialized())
            renderHandler->initMeshPipeline();

        auto* meshPipeline = renderHandler->getMeshPipeline();
        if (!meshPipeline)
        {
            renderHandler->setMeshDrawList({});
            renderHandler->setCurrentFrustum(&ctx.cameraController->getCurrentFrustum());
            return;
        }

        {
            auto& animatorSystem = animation::RuntimeAnimatorSystem::instance();
            if (ctx.playModeActive)
            {
                const auto& frustum = ctx.cameraController->getCurrentFrustum();
                if (frustum.isInitialized())
                {
                    glm::mat4 invView = glm::inverse(ctx.cameraController->getCurrentViewMatrix());
                    animatorSystem.setCullingContext(frustum, glm::vec3(invView[3]));
                }
                animatorSystem.syncWithRegistry();
                animatorSystem.updateAll(ctx.deltaTime);
            }
            animatorSystem.updateSocketAttachments();
        }

        // VK-1336: main-scene culling must always use the primary camera frustum.
        // CameraController::currentFrustum is only updated for MAIN_CAMERA_ID
        // (see CameraController.cpp). RTT/minimap cameras have their own isolated
        // per-RTT cull context in RenderTextureViewPort and must not influence
        // main-scene visibility.
        const math::Frustum& mainFrustum = ctx.cameraController->getCurrentFrustum();
        bool frustumReady = mainFrustum.isInitialized();

        std::vector<render::mesh::MeshRenderData> meshDrawList;
        auto& registry = scene::EntityRegistry::getRegistry();

        bool staticNeedsRebuild = ctx.bvhManager->isStaticDirty() && frustumReady;

        static int dynamicBvhCooldown = 0;
        if (dynamicBvhCooldown > 0)
            --dynamicBvhCooldown;
        else if (!ctx.bvhManager->needsDynamicRebuild())
        {
            auto dirtyIds = threading::parallelCollect<uint32_t,
                                                       components::TransformComponent, components::MeshComponent>(
                registry,
                [&registry](entt::entity entity) -> bool
                {
                    return !registry.get<components::TransformComponent>(entity).isStatic
                        && registry.get<components::TransformComponent>(entity).isDirty;
                },
                [](entt::entity entity) -> uint32_t { return static_cast<uint32_t>(entity); });

            for (uint32_t id : dirtyIds)
                ctx.bvhManager->markDynamicEntityDirty(id);
        }

        bool dynamicNeedsUpdate = ctx.bvhManager->isDynamicDirty() && frustumReady;
        if (staticNeedsRebuild) ctx.bvhManager->rebuildStaticBVH();
        if (dynamicNeedsUpdate)
        {
            ctx.bvhManager->updateDynamicBVH();
            dynamicBvhCooldown = 5;
        }

        auto* gpuDrivenRenderer = renderHandler->getGPUDrivenRenderer();
        bool useGPUDrivenCulling = renderHandler->isGPUDrivenRendererInitialized()
            && gpuDrivenRenderer && gpuDrivenRenderer->isEnabled();

        auto buildRenderData = [&](entt::entity entity, const components::MeshComponent& meshComp,
                                   const components::WorldTransformComponent& worldTransform) ->
            render::mesh::MeshRenderData
        {
            render::mesh::MeshRenderData rd;
            rd.entity = entity;
            rd.meshPath = meshComp.meshRef.resolve();
            rd.modelMatrix = worldTransform.worldMatrix;
            rd.albedo = glm::vec4(1.0f);
            rd.metallic = 0.0f;
            rd.roughness = 0.5f;
            rd.ao = 1.0f;
            rd.emission = 0.0f;
            rd.showBoundingBox = (!ctx.playModeActive && ctx.showDebugRendering) ? meshComp.showBoundingBox : false;
            rd.maxDrawDistance = meshComp.maxDrawDistance;
            rd.submeshIndex = meshComp.submeshIndex;

            if (registry.all_of<components::TransformComponent>(entity))
                rd.isStatic = registry.get<components::TransformComponent>(entity).isStatic;

            if (registry.all_of<components::MaterialComponent>(entity))
            {
                const auto& materialComp = registry.get<components::MaterialComponent>(entity);
                rd.defaultMaterialPath = materialComp.defaultMaterialRef.resolve();
                const auto* pbrValues = getCachedPBRValues(materialComp.defaultMaterialRef.resolve());
                if (pbrValues)
                {
                    rd.albedo = pbrValues->albedo;
                    rd.metallic = pbrValues->metallic;
                    rd.roughness = pbrValues->roughness;
                    rd.ao = pbrValues->ao;
                    rd.emission = pbrValues->emission;
                }
                for (const auto& [submeshName, materialRef] : materialComp.subMeshMaterials)
                {
                    render::mesh::SubMeshMaterialInfo matInfo;
                    populateMaterialInfo(matInfo, materialRef.resolve());
                    rd.submeshMaterials[submeshName] = matInfo;
                }
            }
            return rd;
        };

        auto canBatch = [&](entt::entity entity, const render::mesh::MeshRenderData& rd) -> bool
        {
            if (rd.showBoundingBox) return false;
            if (registry.all_of<components::AnimatorComponent>(entity)) return false;
            return true;
        };

        struct BatchKey
        {
            std::string meshPath;
            std::string materialPath;
            size_t submeshMaterialHash;
            float maxDrawDistance;
            int32_t submeshIndex;

            bool operator==(const BatchKey& o) const
            {
                return meshPath == o.meshPath && materialPath == o.materialPath && submeshMaterialHash == o.
                    submeshMaterialHash && maxDrawDistance == o.maxDrawDistance && submeshIndex == o.submeshIndex;
            }
        };
        struct BatchKeyHash
        {
            size_t operator()(const BatchKey& k) const
            {
                size_t h = std::hash<std::string>{}(k.meshPath);
                h ^= std::hash<std::string>{}(k.materialPath) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= k.submeshMaterialHash + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<float>{}(k.maxDrawDistance) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<int32_t>{}(k.submeshIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        auto hashSubmeshMaterials = [](
            const std::unordered_map<std::string, render::mesh::SubMeshMaterialInfo>& mats) -> size_t
        {
            if (mats.empty()) return 0;
            size_t h = 0;
            for (const auto& [name, info] : mats)
            {
                size_t entry = std::hash<std::string>{}(name);
                entry ^= std::hash<std::string>{}(info.materialPath) + 0x9e3779b9 + (entry << 6) + (entry >> 2);
                h ^= entry;
            }
            return h;
        };

        std::unordered_map<BatchKey, size_t, BatchKeyHash> batchMap;

        auto collectEntity = [&](entt::entity entity, const components::MeshComponent& meshComp,
                                 const components::WorldTransformComponent& worldTransform)
        {
            auto renderData = buildRenderData(entity, meshComp, worldTransform);

            if (canBatch(entity, renderData))
            {
                std::string batchMaterialPath = renderData.defaultMaterialPath;
                bool hasInstancePBR = false;
                if (material::isInstanceFile(batchMaterialPath))
                {
                    auto cacheIt = instanceBatchCache.find(batchMaterialPath);
                    if (cacheIt == instanceBatchCache.end())
                    {
                        InstanceBatchInfo info;
                        auto instanceData = resource::ResourceManager::loadMaterialInstance(
                            asset::AssetRef::fromPath(batchMaterialPath));
                        if (instanceData && instanceData->parentMaterialRef.isValid())
                        {
                            info.parentMaterialPath = instanceData->parentMaterialRef.resolve();
                            info.hasTextureOverrides = !instanceData->textureOverrides.empty();
                        }
                        cacheIt = instanceBatchCache.emplace(batchMaterialPath, std::move(info)).first;
                    }
                    const auto& info = cacheIt->second;
                    if (!info.parentMaterialPath.empty() && !info.hasTextureOverrides)
                    {
                        batchMaterialPath = info.parentMaterialPath;
                        hasInstancePBR = true;
                    }
                }

                BatchKey key{
                    renderData.meshPath, batchMaterialPath, hashSubmeshMaterials(renderData.submeshMaterials),
                    renderData.maxDrawDistance, renderData.submeshIndex
                };
                auto it = batchMap.find(key);
                if (it != batchMap.end())
                {
                    render::mesh::MeshRenderData::InstanceData instData;
                    instData.modelMatrix = worldTransform.worldMatrix;
                    if (hasInstancePBR)
                    {
                        auto* pbr = getCachedPBRValues(renderData.defaultMaterialPath);
                        if (pbr)
                        {
                            instData.albedo = pbr->albedo;
                            instData.pbrParams = glm::vec4(pbr->metallic, pbr->roughness, pbr->ao, pbr->emission);
                            instData.iblParams = glm::vec4(pbr->iblDiffuse, pbr->iblSpecular, pbr->alphaCutoff, 1.0f);
                        }
                        else { instData.iblParams.w = 0.0f; }
                    }
                    meshDrawList[it->second].instanceTransforms.push_back(instData);
                    return;
                }

                size_t idx = meshDrawList.size();
                render::mesh::MeshRenderData::InstanceData firstInst;
                firstInst.modelMatrix = worldTransform.worldMatrix;
                if (hasInstancePBR)
                {
                    auto* pbr = getCachedPBRValues(renderData.defaultMaterialPath);
                    if (pbr)
                    {
                        firstInst.albedo = pbr->albedo;
                        firstInst.pbrParams = glm::vec4(pbr->metallic, pbr->roughness, pbr->ao, pbr->emission);
                        firstInst.iblParams = glm::vec4(pbr->iblDiffuse, pbr->iblSpecular, pbr->alphaCutoff, 1.0f);
                    }
                }
                renderData.instanceTransforms.push_back(firstInst);
                meshDrawList.push_back(std::move(renderData));
                batchMap[key] = idx;
                return;
            }
            meshDrawList.push_back(std::move(renderData));
        };

        auto meshView = registry.view<components::MeshComponent, components::WorldTransformComponent>();

        if (useGPUDrivenCulling)
        {
            for (auto entity : meshView)
            {
                if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
                const auto& meshComp = meshView.get<components::MeshComponent>(entity);
                if (!meshComp.meshRef.isValid() || !meshPipeline->isMeshLoaded(meshComp.meshRef.resolve())) continue;
                collectEntity(entity, meshComp, meshView.get<components::WorldTransformComponent>(entity));
            }
        }
        else if (ctx.bvhManager->isBuilt() && frustumReady)
        {
            std::vector<uint32_t> visibleEntities;
            ctx.bvhManager->queryFrustum(mainFrustum, visibleEntities);
            for (uint32_t entityId : visibleEntities)
            {
                auto entity = static_cast<entt::entity>(entityId);
                if (!registry.valid(entity)) continue;
                if (!registry.all_of<components::MeshComponent, components::WorldTransformComponent>(entity)) continue;
                if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                if (!meshComp.meshRef.isValid() || !meshPipeline->isMeshLoaded(meshComp.meshRef.resolve())) continue;
                collectEntity(entity, meshComp, registry.get<components::WorldTransformComponent>(entity));
            }
        }
        else
        {
            for (auto entity : meshView)
            {
                if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
                const auto& meshComp = meshView.get<components::MeshComponent>(entity);
                const auto& worldTransform = meshView.get<components::WorldTransformComponent>(entity);
                if (!meshComp.meshRef.isValid() || !meshPipeline->isMeshLoaded(meshComp.meshRef.resolve())) continue;
                if (frustumReady)
                {
                    const math::AABB* boundingBox = meshPipeline->getMeshBoundingBox(meshComp.meshRef.resolve());
                    if (boundingBox && !mainFrustum.intersectsAABB(*boundingBox, worldTransform.worldMatrix))
                        continue;
                }
                collectEntity(entity, meshComp, worldTransform);
            }
        }

        if (useGPUDrivenCulling && ctx.lightBvhManager && frustumReady)
        {
            ctx.lightBvhManager->update();
            std::vector<uint32_t> visibleLights;
            ctx.lightBvhManager->queryFrustum(mainFrustum, visibleLights);
            renderHandler->setVisibleLightsFromBVH(visibleLights);
        }
        else if (useGPUDrivenCulling)
        {
            renderHandler->clearVisibleLights();
        }

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&ctx.cameraController->getCurrentFrustum());
    }
}
