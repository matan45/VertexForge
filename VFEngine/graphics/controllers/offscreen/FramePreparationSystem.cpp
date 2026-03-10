#include "FramePreparationSystem.hpp"
#include "SceneBVHManager.hpp"
#include "LightBVHManager.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/billboard/BillboardTypes.hpp"
#include "../../render/billboard/BillboardPipeline.hpp"

#include "../../render/text/TextTypes.hpp"
#include "../../render/text/TextPipeline.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../../render/occlusion/CameraOcclusionManager.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/LightTextComponents.hpp"
#include "resource/ResourceManager.hpp"
#include "../../render/material/MaterialPBRExtractor.hpp"
#include "threading/JobSystem.hpp"


namespace controllers::offscreen
{
    const render::mesh::ExtractedPBRValues* FramePreparationSystem::getCachedPBRValues(
        const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return nullptr;
        }

        auto it = pbrCache.find(materialPath);
        if (it != pbrCache.end())
        {
            return &it->second;
        }

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
    }

    void FramePreparationSystem::prepareMeshes(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        // Ensure the mesh pipeline + GPU-driven renderer are initialized so
        // terrain/water can render even when the scene contains no mesh assets.
        if (!renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->initMeshPipeline();
        }

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
                // Pass frustum culling context to skip bone evaluation for off-screen entities
                const auto& frustum = ctx.cameraController->getCurrentFrustum();
                if (frustum.isInitialized())
                {
                    // Extract camera position from inverse view matrix
                    glm::mat4 invView = glm::inverse(ctx.cameraController->getCurrentViewMatrix());
                    glm::vec3 cameraPos = glm::vec3(invView[3]);
                    animatorSystem.setCullingContext(frustum, cameraPos);
                }

                animatorSystem.syncWithRegistry();
                animatorSystem.updateAll(ctx.deltaTime);
            }
            animatorSystem.updateSocketAttachments();
        }

        auto* cameraManager = renderHandler->getCameraOcclusionManager();
        auto* activeCamera = cameraManager->getCamera(cameraManager->getActiveCameraId());
        const math::Frustum* activeFrustum = activeCamera ? &activeCamera->frustum : nullptr;
        bool frustumReady = activeFrustum && activeFrustum->isInitialized();

        std::vector<render::mesh::MeshRenderData> meshDrawList;
        auto& registry = scene::EntityRegistry::getRegistry();

        bool staticNeedsRebuild = ctx.bvhManager->isStaticDirty() && frustumReady;

        static int dynamicBvhCooldown = 0;

        if (dynamicBvhCooldown > 0)
        {
            --dynamicBvhCooldown;
        }
        else if (!ctx.bvhManager->needsDynamicRebuild())
        {
            auto dynamicView = registry.view<components::TransformComponent, components::MeshComponent>();
            std::vector<uint32_t> dirtyIds;
            for (auto entity : dynamicView)
            {
                const auto& transform = dynamicView.get<components::TransformComponent>(entity);
                if (!transform.isStatic && transform.isDirty)
                {
                    dirtyIds.push_back(static_cast<uint32_t>(entity));
                }
            }
            for (uint32_t id : dirtyIds)
            {
                ctx.bvhManager->markDynamicEntityDirty(id);
            }
        }

        bool dynamicNeedsUpdate = ctx.bvhManager->isDynamicDirty() && frustumReady;

        if (staticNeedsRebuild)
        {
            ctx.bvhManager->rebuildStaticBVH();
        }
        if (dynamicNeedsUpdate)
        {
            ctx.bvhManager->updateDynamicBVH();
            dynamicBvhCooldown = 5;
        }

        auto* gpuDrivenRenderer = renderHandler->getGPUDrivenRenderer();
        bool useGPUDrivenCulling = renderHandler->isGPUDrivenRendererInitialized()
            && gpuDrivenRenderer
            && gpuDrivenRenderer->isEnabled();

        auto buildRenderData = [&](entt::entity entity, const components::MeshComponent& meshComp,
                                   const components::WorldTransformComponent& worldTransform) ->
            render::mesh::MeshRenderData
        {
            render::mesh::MeshRenderData renderData;
            renderData.entity = entity;
            renderData.meshPath = meshComp.meshPath;
            renderData.modelMatrix = worldTransform.worldMatrix;

            renderData.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            renderData.metallic = 0.0f;
            renderData.roughness = 0.5f;
            renderData.ao = 1.0f;
            renderData.emission = 0.0f;
            renderData.showBoundingBox = (!ctx.playModeActive && ctx.showDebugRendering)
                                             ? meshComp.showBoundingBox
                                             : false;
            renderData.maxDrawDistance = meshComp.maxDrawDistance;

            if (registry.all_of<components::LightmapComponent>(entity))
            {
                const auto& lmComp = registry.get<components::LightmapComponent>(entity);
                renderData.lightmapPath = lmComp.lightmapPath;
                renderData.lightmapScaleOffset = lmComp.atlasScaleOffset;
            }

            if (registry.all_of<components::MaterialComponent>(entity))
            {
                const auto& materialComp = registry.get<components::MaterialComponent>(entity);
                renderData.defaultMaterialPath = materialComp.defaultMaterial;

                const auto* pbrValues = getCachedPBRValues(materialComp.defaultMaterial);
                if (pbrValues)
                {
                    renderData.albedo = pbrValues->albedo;
                    renderData.metallic = pbrValues->metallic;
                    renderData.roughness = pbrValues->roughness;
                    renderData.ao = pbrValues->ao;
                    renderData.emission = pbrValues->emission;
                }

                for (const auto& [submeshName, materialPath] : materialComp.subMeshMaterials)
                {
                    render::mesh::SubMeshMaterialInfo matInfo;
                    populateMaterialInfo(matInfo, materialPath);
                    renderData.submeshMaterials[submeshName] = matInfo;
                }
            }

            return renderData;
        };

        // Check if an entity can be instanced-batched with others sharing the same mesh+material.
        // Entities with per-instance unique data (lightmaps, bounding box debug, animations) cannot be batched.
        auto canBatch = [&](entt::entity entity, const render::mesh::MeshRenderData& rd) -> bool
        {
            if (rd.showBoundingBox) return false;
            if (!rd.lightmapPath.empty()) return false;
            if (rd.maxDrawDistance > 0.0f) return false;
            if (registry.all_of<components::AnimatorComponent>(entity)) return false;
            return true;
        };

        // Batch key: meshPath + defaultMaterialPath + submesh material fingerprint
        struct BatchKey
        {
            std::string meshPath;
            std::string materialPath;
            size_t submeshMaterialHash;
            bool operator==(const BatchKey& o) const
            {
                return meshPath == o.meshPath && materialPath == o.materialPath
                    && submeshMaterialHash == o.submeshMaterialHash;
            }
        };
        struct BatchKeyHash
        {
            size_t operator()(const BatchKey& k) const
            {
                size_t h = std::hash<std::string>{}(k.meshPath);
                h ^= std::hash<std::string>{}(k.materialPath) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= k.submeshMaterialHash + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        auto hashSubmeshMaterials = [](const std::unordered_map<std::string, render::mesh::SubMeshMaterialInfo>& mats) -> size_t
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

        // Map from batch key to index in meshDrawList (for the template MeshRenderData)
        std::unordered_map<BatchKey, size_t, BatchKeyHash> batchMap;

        auto collectEntity = [&](entt::entity entity, const components::MeshComponent& meshComp,
                                  const components::WorldTransformComponent& worldTransform)
        {
            auto renderData = buildRenderData(entity, meshComp, worldTransform);

            if (canBatch(entity, renderData))
            {
                BatchKey key{renderData.meshPath, renderData.defaultMaterialPath,
                             hashSubmeshMaterials(renderData.submeshMaterials)};
                auto it = batchMap.find(key);
                if (it != batchMap.end())
                {
                    // Append transform to existing batch
                    meshDrawList[it->second].instanceTransforms.push_back(worldTransform.worldMatrix);
                    return;
                }

                // Start new batch: first instance goes into instanceTransforms
                size_t idx = meshDrawList.size();
                renderData.instanceTransforms.push_back(worldTransform.worldMatrix);
                meshDrawList.push_back(std::move(renderData));
                batchMap[key] = idx;
                return;
            }

            meshDrawList.push_back(std::move(renderData));
        };

        if (useGPUDrivenCulling)
        {
            auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

            for (auto entity : view)
            {
                if (registry.all_of<components::NameComponent>(entity))
                {
                    const auto& nameComp = registry.get<components::NameComponent>(entity);
                    if (!nameComp.isActive)
                    {
                        continue;
                    }
                }

                const auto& meshComp = view.get<components::MeshComponent>(entity);
                const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty() || !meshPipeline->isMeshLoaded(meshComp.meshPath))
                {
                    continue;
                }

                collectEntity(entity, meshComp, worldTransform);
            }
        }
        else if (ctx.bvhManager->isBuilt() && frustumReady)
        {
            std::vector<uint32_t> visibleEntities;
            ctx.bvhManager->queryFrustum(*activeFrustum, visibleEntities);

            for (uint32_t entityId : visibleEntities)
            {
                auto entity = static_cast<entt::entity>(entityId);

                if (!registry.valid(entity))
                {
                    continue;
                }

                if (registry.all_of<components::NameComponent>(entity))
                {
                    const auto& nameComp = registry.get<components::NameComponent>(entity);
                    if (!nameComp.isActive)
                    {
                        continue;
                    }
                }

                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty() || !meshPipeline->isMeshLoaded(meshComp.meshPath))
                {
                    continue;
                }

                collectEntity(entity, meshComp, worldTransform);
            }
        }
        else
        {
            auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

            for (auto entity : view)
            {
                if (registry.all_of<components::NameComponent>(entity))
                {
                    const auto& nameComp = registry.get<components::NameComponent>(entity);
                    if (!nameComp.isActive)
                    {
                        continue;
                    }
                }

                const auto& meshComp = view.get<components::MeshComponent>(entity);
                const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty() || !meshPipeline->isMeshLoaded(meshComp.meshPath))
                {
                    continue;
                }

                if (frustumReady)
                {
                    const math::AABB* boundingBox = meshPipeline->getMeshBoundingBox(meshComp.meshPath);
                    if (boundingBox && !activeFrustum->intersectsAABB(*boundingBox, worldTransform.worldMatrix))
                    {
                        continue;
                    }
                }

                collectEntity(entity, meshComp, worldTransform);
            }
        }

        if (useGPUDrivenCulling && ctx.lightBvhManager && frustumReady)
        {
            ctx.lightBvhManager->update();

            std::vector<uint32_t> visibleLights;
            ctx.lightBvhManager->queryFrustum(*activeFrustum, visibleLights);
            renderHandler->setVisibleLightsFromBVH(visibleLights);
        }
        else if (useGPUDrivenCulling)
        {
            renderHandler->clearVisibleLights();
        }

        // Log batching stats
        {
            uint32_t totalEntities = 0;
            uint32_t batchedGroups = 0;
            uint32_t unbatchedCount = 0;
            for (const auto& rd : meshDrawList)
            {
                if (!rd.instanceTransforms.empty())
                {
                    batchedGroups++;
                    totalEntities += static_cast<uint32_t>(rd.instanceTransforms.size());
                }
                else
                {
                    unbatchedCount++;
                    totalEntities++;
                }
            }
            static int logCooldown = 0;
            if (logCooldown <= 0)
            {
                vfLogInfo("Instancing: {} entities -> {} draw items ({} batched groups, {} unbatched), batchMap size={}",
                          totalEntities, meshDrawList.size(), batchedGroups, unbatchedCount, batchMap.size());
                logCooldown = 300; // ~5 seconds at 60fps
            }
            logCooldown--;
        }

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&ctx.cameraController->getCurrentFrustum());
        ctx.bvhManager->updateOcclusionCullingData(renderHandler);
    }

    std::vector<render::billboard::BillboardRenderData> FramePreparationSystem::gatherBillboardData(
        const FrameContext& ctx)
    {
        bool showEditorIcons = !ctx.playModeActive && ctx.showBillboardIcons;
        std::vector<render::billboard::BillboardRenderData> billboardDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BillboardComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& billboard = view.get<components::BillboardComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (billboard.editorOnly && !showEditorIcons)
            {
                continue;
            }

            render::billboard::BillboardRenderData renderData;
            renderData.worldPosition = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.atlasIndex = billboard.getEffectiveAtlasIndex();
            renderData.size = billboard.size;
            renderData.sizeMode = static_cast<uint32_t>(billboard.sizeMode);
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.colorTint = billboard.colorTint;
            renderData.texturePath = billboard.texturePath;

            if (billboard.renderTextureSource != entt::null
                && registry.valid(billboard.renderTextureSource)
                && registry.all_of<components::RenderTextureComponent>(billboard.renderTextureSource))
            {
                const auto& rtt = registry.get<components::RenderTextureComponent>(billboard.renderTextureSource);
                if (rtt.textureId != rendertexture::INVALID_RENDER_TEXTURE_ID)
                {
                    renderData.texturePath = "__rtt_" + std::to_string(rtt.textureId) + "__";
                }
            }

            billboardDrawList.push_back(renderData);
        }

        return billboardDrawList;
    }

    std::vector<render::text::TextRenderData> FramePreparationSystem::gatherTextData(const FrameContext& ctx)
    {
        std::vector<render::text::TextRenderData> textDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TextComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& textComp = view.get<components::TextComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (textComp.fontPath.empty() || textComp.text.empty())
            {
                continue;
            }

            render::text::TextRenderData renderData;
            renderData.fontPath = textComp.fontPath;
            renderData.text = textComp.text;
            renderData.worldPosition = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.fontSize = textComp.fontSize;
            renderData.color = textComp.color;
            renderData.renderMode = 1; // WorldSpace only
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.lineSpacing = textComp.lineSpacing;
            renderData.letterSpacing = textComp.letterSpacing;
            renderData.maxWidth = textComp.maxWidth;

            textDrawList.push_back(std::move(renderData));
        }

        return textDrawList;
    }

    void FramePreparationSystem::prepareBillboards(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initBillboardPipeline();

        if (!renderHandler->isBillboardPipelineInitialized())
        {
            renderHandler->setBillboardDrawList({});
            return;
        }

        renderHandler->setBillboardDrawList(gatherBillboardData(ctx));
    }

    void FramePreparationSystem::prepareText(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initTextPipeline();

        if (!renderHandler->isTextPipelineInitialized())
        {
            renderHandler->setTextDrawList({});
            return;
        }

        renderHandler->setTextDrawList(gatherTextData(ctx));
    }

    void FramePreparationSystem::prepareSceneData(const FrameContext& ctx)
    {
        // Mesh preparation first - mutates ECS (animator, BVH), initializes mesh pipeline
        prepareMeshes(ctx);

        // Billboard and text: init pipelines + gather data sequentially
        // (parallel submit/future overhead exceeds the cost of these lightweight gathers)
        prepareBillboards(ctx);
        prepareText(ctx);
    }
}
