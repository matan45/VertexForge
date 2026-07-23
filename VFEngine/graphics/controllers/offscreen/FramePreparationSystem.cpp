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
#include "../../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "terrain/TerrainTile.hpp"
#include "foliage/FoliageCompose.hpp"
#include "threading/ParallelCollect.hpp"
#include <cstdio>
#include <unordered_set>

namespace controllers::offscreen
{
    namespace
    {
        // Stable across map iteration order (std::map is name-sorted)
        uint64_t hashRuntimeOverrides(
            const render::mesh::MaterialPBRExtractor::ParameterOverrides& overrides)
        {
            uint64_t hash = 14695981039346656037ull;
            auto mix = [&hash](const void* data, size_t size)
            {
                const auto* bytes = static_cast<const unsigned char*>(data);
                for (size_t i = 0; i < size; ++i)
                {
                    hash ^= bytes[i];
                    hash *= 1099511628211ull;
                }
            };
            for (const auto& [name, value] : overrides)
            {
                mix(name.data(), name.size());
                size_t typeIndex = value.index();
                mix(&typeIndex, sizeof(typeIndex));
                std::visit([&mix](const auto& v) { mix(&v, sizeof(v)); }, value);
            }
            return hash;
        }

        constexpr size_t MAX_OVERRIDE_PBR_CACHE_ENTRIES = 4096;
    }

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

    const render::mesh::ExtractedPBRValues* FramePreparationSystem::getCachedPBRValues(
        const std::string& materialPath,
        const render::mesh::MaterialPBRExtractor::ParameterOverrides* runtimeOverrides)
    {
        if (!runtimeOverrides || runtimeOverrides->empty())
        {
            return getCachedPBRValues(materialPath);
        }
        if (materialPath.empty()) return nullptr;

        char hashBuf[20];
        snprintf(hashBuf, sizeof(hashBuf), "#%016llx",
                 static_cast<unsigned long long>(hashRuntimeOverrides(*runtimeOverrides)));
        std::string cacheKey = materialPath + hashBuf;

        auto it = pbrCache.find(cacheKey);
        if (it != pbrCache.end()) return &it->second;

        // Hashed entries accumulate as values change — sweep them past a cap
        if (pbrCache.size() > MAX_OVERRIDE_PBR_CACHE_ENTRIES)
        {
            std::erase_if(pbrCache, [](const auto& pair) {
                return pair.first.find('#') != std::string::npos;
            });
        }

        auto [inserted, success] = pbrCache.emplace(
            cacheKey,
            render::mesh::MaterialPBRExtractor::extractPBRFromPath(materialPath, runtimeOverrides));
        return &inserted->second;
    }

    void FramePreparationSystem::populateMaterialInfo(render::mesh::SubMeshMaterialInfo& matInfo,
                                                      const std::string& materialPath,
                                                      const render::mesh::MaterialPBRExtractor::ParameterOverrides* runtimeOverrides)
    {
        matInfo.materialPath = materialPath;
        const auto* pbrValues = getCachedPBRValues(materialPath, runtimeOverrides);
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
            matInfo.shadingModel = pbrValues->shadingModel;
            matInfo.toonProfileIndex = pbrValues->toonProfileIndex;
            matInfo.receiveWind = pbrValues->receiveWind;  // VK-1580
        }
    }

    void FramePreparationSystem::invalidateMaterialCache(const std::string& materialPath)
    {
        pbrCache.erase(materialPath);
        instanceBatchCache.erase(materialPath);

        // Drop runtime-override variants of this material (keyed "<path>#<hash>")
        std::string hashedPrefix = materialPath + "#";
        std::erase_if(pbrCache, [&hashedPrefix](const auto& pair) {
            return pair.first.starts_with(hashedPrefix);
        });
    }

    void FramePreparationSystem::clearAllMaterialCache()
    {
        pbrCache.clear();
        instanceBatchCache.clear();
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
                // VK-992: animator playback advances on scaled gameplay delta (0 when frozen
                // → holds pose), not raw ctx.deltaTime which UI systems still use.
                animatorSystem.updateAll(ctx.gameplayDeltaTime);
            }
            else
            {
                animatorSystem.updateEditModePreview();
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
            rd.renderLayer = meshComp.renderLayer; // VK-1415

            if (registry.all_of<components::TransformComponent>(entity))
                rd.isStatic = registry.get<components::TransformComponent>(entity).isStatic;

            if (registry.all_of<components::MaterialComponent>(entity))
            {
                const auto& materialComp = registry.get<components::MaterialComponent>(entity);
                const auto* runtimeOverrides = materialComp.parameterOverrides.empty()
                    ? nullptr : &materialComp.parameterOverrides;
                rd.defaultMaterialPath = materialComp.defaultMaterialRef.resolve();
                const auto* pbrValues = getCachedPBRValues(rd.defaultMaterialPath, runtimeOverrides);
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
                    populateMaterialInfo(matInfo, materialRef.resolve(), runtimeOverrides);
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
            uint32_t renderLayer; // VK-1415: distinct layers must not merge into one instanced draw

            bool operator==(const BatchKey& o) const
            {
                return meshPath == o.meshPath && materialPath == o.materialPath && submeshMaterialHash == o.
                    submeshMaterialHash && maxDrawDistance == o.maxDrawDistance && submeshIndex == o.submeshIndex &&
                    renderLayer == o.renderLayer;
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
                h ^= std::hash<uint32_t>{}(k.renderLayer) + 0x9e3779b9 + (h << 6) + (h >> 2);
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
                const render::mesh::MaterialPBRExtractor::ParameterOverrides* runtimeOverrides = nullptr;
                if (registry.all_of<components::MaterialComponent>(entity))
                {
                    const auto& materialComp = registry.get<components::MaterialComponent>(entity);
                    if (!materialComp.parameterOverrides.empty())
                    {
                        runtimeOverrides = &materialComp.parameterOverrides;
                    }
                }

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
                            info.hasTextureOverrides = !instanceData->textureOverrides.empty() ||
                                                       !instanceData->textureParameterOverrides.empty();
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
                else if (runtimeOverrides)
                {
                    // Runtime value overrides ride the per-instance PBR mechanism so the
                    // entity stays batched under its material (textures are unchanged)
                    hasInstancePBR = true;
                }

                auto fillInstancePBR = [&](render::mesh::MeshRenderData::InstanceData& instData)
                {
                    auto* pbr = getCachedPBRValues(renderData.defaultMaterialPath, runtimeOverrides);
                    if (pbr)
                    {
                        instData.albedo = pbr->albedo;
                        instData.pbrParams = glm::vec4(pbr->metallic, pbr->roughness, pbr->ao, pbr->emission);
                        instData.iblParams = glm::vec4(pbr->iblDiffuse, pbr->iblSpecular, pbr->alphaCutoff, 1.0f);
                    }
                    else { instData.iblParams.w = 0.0f; }
                };

                BatchKey key{
                    renderData.meshPath, batchMaterialPath, hashSubmeshMaterials(renderData.submeshMaterials),
                    renderData.maxDrawDistance, renderData.submeshIndex, renderData.renderLayer
                };
                auto it = batchMap.find(key);
                if (it != batchMap.end())
                {
                    render::mesh::MeshRenderData::InstanceData instData;
                    instData.modelMatrix = worldTransform.worldMatrix;
                    if (hasInstancePBR)
                    {
                        fillInstancePBR(instData);
                    }
                    meshDrawList[it->second].instanceTransforms.push_back(instData);
                    return;
                }

                size_t idx = meshDrawList.size();
                render::mesh::MeshRenderData::InstanceData firstInst;
                firstInst.modelMatrix = worldTransform.worldMatrix;
                if (hasInstancePBR)
                {
                    fillInstancePBR(firstInst);
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

        // VK-1573: append pre-instanced foliage draws (terrain-tile instances) into the same
        // draw list so they ride the existing GPU-instancing path (per-instance cull/LOD/crossfade)
        // and get mesh residency for free via updateMeshStreaming. Must run before the move.
        {
            const glm::vec3 cameraPos(glm::inverse(ctx.cameraController->getCurrentViewMatrix())[3]);
            collectFoliage(meshDrawList, ctx, cameraPos);
        }

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&ctx.cameraController->getCurrentFrustum());
    }

    void FramePreparationSystem::collectFoliage(std::vector<render::mesh::MeshRenderData>& meshDrawList,
                                                const FrameContext& ctx, const glm::vec3& cameraPos)
    {
        auto* provider = ctx.renderHandler->getTerrainRenderProvider();
        if (!provider || !provider->hasActiveTerrain()) return;

        const auto& palette = provider->getFoliagePalette();
        if (palette.empty()) return;

        std::vector<terrain::TerrainTile*> tiles = provider->getAllLoadedTiles();

        // Live tile set for this frame — used to prune cache entries of unloaded/emptied tiles.
        std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> liveKeys;
        liveKeys.reserve(tiles.size());

        for (auto* tile : tiles)
        {
            if (!tile || !tile->hasFoliageInstances()) continue;
            liveKeys.insert(tile->coord);

            // Rebuild this tile's composed cache only when its GPU-dirty flag is set (or first sight).
            // The persistent per-tile cache means we can rebuild once on edit regardless of distance
            // and simply re-emit from it every frame — never the vegetation "re-collect everything".
            auto cacheIt = foliageDrawCache.find(tile->coord);
            if (tile->foliageInstancesGPUDirty || cacheIt == foliageDrawCache.end())
            {
                std::vector<ComposedFoliageDraw> composed;
                auto groups = foliage::groupInstanceIndicesByType(tile->foliageInstances, palette.size());
                composed.reserve(groups.size());

                for (auto& [typeIndex, indices] : groups)
                {
                    const foliage::FoliageType& type = palette[typeIndex];
                    if (type.meshPath.empty() || !type.visible) continue;

                    // Resolve the material's PBR once per type so enabling a per-instance tint
                    // override does not blank the material's metallic/roughness/ao/emission/IBL.
                    const render::mesh::ExtractedPBRValues* pbr =
                        type.materialPath.empty() ? nullptr : getCachedPBRValues(type.materialPath);

                    ComposedFoliageDraw draw;
                    draw.typeIndex = typeIndex;
                    draw.transforms.reserve(indices.size());
                    for (uint32_t idx : indices)
                    {
                        const foliage::FoliageInstance& fi = tile->foliageInstances[idx];
                        render::mesh::MeshRenderData::InstanceData inst;
                        inst.modelMatrix = foliage::composeFoliageModelMatrix(fi, type);
                        // Per-instance tint: only turn the override on when a tint is actually set,
                        // and mirror the real material PBR so the override branch (hasOverride) keeps
                        // correct shading. Untinted instances keep InstanceData defaults (no override).
                        if (fi.tint != 0xFFFFFFFFu && pbr)
                        {
                            inst.albedo = foliage::unpackTintRGBA8(fi.tint);
                            inst.pbrParams = glm::vec4(pbr->metallic, pbr->roughness, pbr->ao, pbr->emission);
                            inst.iblParams = glm::vec4(pbr->iblDiffuse, pbr->iblSpecular, pbr->alphaCutoff, 1.0f);
                        }
                        draw.transforms.push_back(inst);
                    }
                    if (!draw.transforms.empty())
                        composed.push_back(std::move(draw));
                }

                foliageDrawCache[tile->coord] = std::move(composed);
                tile->foliageInstancesGPUDirty = false;
                cacheIt = foliageDrawCache.find(tile->coord);
            }

            // Emit one MeshRenderData per (tile, type) from the cache, with a per-tile distance cull.
            const float tileSize = tile->config.worldTileSize;
            const glm::vec3 tileCenter(
                static_cast<float>(tile->coord.x) * tileSize + tileSize * 0.5f,
                0.0f,
                static_cast<float>(tile->coord.z) * tileSize + tileSize * 0.5f);

            for (const auto& draw : cacheIt->second)
            {
                if (draw.typeIndex >= palette.size()) continue; // palette may have shrunk
                const foliage::FoliageType& type = palette[draw.typeIndex];

                // endCullDistance is enforced HERE: the GPU skips group distance cull for instanced
                // draws, so tile-granularity CPU culling is what honours the type's cull distance.
                if (!foliage::foliageTileInRange(tileCenter, cameraPos, type.endCullDistance + tileSize))
                    continue;

                render::mesh::MeshRenderData rd;
                rd.meshPath = type.meshPath;
                rd.defaultMaterialPath = type.materialPath;
                rd.maxDrawDistance = type.endCullDistance;
                rd.isStatic = true;
                rd.renderLayer = 0;
                rd.instanceTransforms = draw.transforms; // copy the composed cache into the record
                meshDrawList.push_back(std::move(rd));
            }
        }

        // Prune cache entries for tiles no longer loaded (or that lost all their foliage).
        for (auto it = foliageDrawCache.begin(); it != foliageDrawCache.end();)
        {
            if (liveKeys.find(it->first) == liveKeys.end())
                it = foliageDrawCache.erase(it);
            else
                ++it;
        }
    }
}
