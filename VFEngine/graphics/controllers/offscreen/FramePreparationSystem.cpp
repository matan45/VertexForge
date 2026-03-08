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

        // Check if entity should be replaced by billboard impostor at current distance
        glm::mat4 invViewMat = glm::inverse(ctx.cameraController->getCurrentViewMatrix());
        glm::vec3 meshCameraPos = glm::vec3(invViewMat[3]);

        auto shouldUseBillboardInstead = [&](entt::entity entity, const components::WorldTransformComponent& wt) -> bool {
            if (!registry.all_of<components::BillboardComponent>(entity)) return false;
            const auto& bb = registry.get<components::BillboardComponent>(entity);
            if (bb.imposterPath.empty()) return false;
            if (bb.sizeMode != components::BillboardSizeMode::WorldSpace) return false;

            glm::vec3 pos = glm::vec3(wt.worldMatrix[3]);
            float distSq = glm::dot(pos - meshCameraPos, pos - meshCameraPos);
            float bbDistSq = bb.billboardDistance * bb.billboardDistance;
            return distSq > bbDistSq;
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

                if (shouldUseBillboardInstead(entity, worldTransform)) continue;

                meshDrawList.push_back(buildRenderData(entity, meshComp, worldTransform));
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

                if (shouldUseBillboardInstead(entity, worldTransform)) continue;

                meshDrawList.push_back(buildRenderData(entity, meshComp, worldTransform));
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

                if (shouldUseBillboardInstead(entity, worldTransform)) continue;

                if (frustumReady)
                {
                    const math::AABB* boundingBox = meshPipeline->getMeshBoundingBox(meshComp.meshPath);
                    if (boundingBox && !activeFrustum->intersectsAABB(*boundingBox, worldTransform.worldMatrix))
                    {
                        continue;
                    }
                }

                meshDrawList.push_back(buildRenderData(entity, meshComp, worldTransform));
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

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&ctx.cameraController->getCurrentFrustum());
        ctx.bvhManager->updateOcclusionCullingData(renderHandler);
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

            // Editor-only billboards (debug icons) only show in editor mode
            if (billboard.editorOnly && !showEditorIcons)
            {
                continue;
            }

            // Check if this entity should show impostor billboard instead of editor icon
            if (!billboard.editorOnly && billboard.sizeMode == components::BillboardSizeMode::WorldSpace
                && !billboard.imposterPath.empty())
            {
                // Impostor billboard — only show when beyond billboardDistance
                glm::vec3 pos = glm::vec3(worldTransform.worldMatrix[3]);
                glm::mat4 bbInvView = glm::inverse(ctx.cameraController->getCurrentViewMatrix());
                glm::vec3 camPos = glm::vec3(bbInvView[3]);
                float distSq = glm::dot(pos - camPos, pos - camPos);
                float bbDistSq = billboard.billboardDistance * billboard.billboardDistance;
                if (distSq > bbDistSq && distSq < billboard.maxRenderDistance * billboard.maxRenderDistance)
                {
                    // Load impostor atlas via GPU-driven renderer (creates VkImage/View/Sampler)
                    auto* gpuRenderer = renderHandler->getGPUDrivenRenderer();
                    if (gpuRenderer)
                    {
                        gpuRenderer->loadImposterAtlas(billboard.imposterPath);
                        const auto* impTex = gpuRenderer->getImposterTexture(billboard.imposterPath);
                        if (impTex && !impTex->views.empty())
                        {
                            // Register impostor texture with billboard pipeline as external texture
                            auto* bbPipeline = renderHandler->getBillboardPipeline();
                            if (bbPipeline)
                            {
                                bbPipeline->registerExternalTexture(billboard.imposterPath,
                                    impTex->imageView, impTex->sampler);
                            }

                            // Select best view based on camera angle
                            glm::vec3 toCamera = camPos - pos;
                            float hAngle = std::atan2(toCamera.x, toCamera.z); // horizontal angle around Y
                            if (hAngle < 0.0f) hAngle += glm::two_pi<float>();

                            // Find closest horizontal view
                            uint32_t bestView = 0;
                            float bestDot = -1.0f;
                            for (uint32_t i = 0; i < impTex->views.size(); ++i)
                            {
                                float viewAngle = impTex->views[i].horizontalAngle;
                                float diff = std::abs(hAngle - viewAngle);
                                if (diff > glm::pi<float>()) diff = glm::two_pi<float>() - diff;
                                float score = 1.0f - diff; // higher = closer match
                                if (score > bestDot)
                                {
                                    bestDot = score;
                                    bestView = i;
                                }
                            }

                            render::billboard::BillboardRenderData renderData;
                            renderData.worldPosition = pos;
                            renderData.atlasIndex = bestView;
                            renderData.size = billboard.size;
                            renderData.sizeMode = static_cast<uint32_t>(billboard.sizeMode);
                            renderData.entityId = static_cast<uint32_t>(entity);
                            renderData.colorTint = billboard.colorTint;
                            renderData.texturePath = billboard.imposterPath;
                            renderData.atlasGridSize = static_cast<float>(impTex->atlasCols);
                            billboardDrawList.push_back(renderData);
                        }
                    }
                }
                continue; // Skip normal billboard processing for impostor entities
            }

            // Non-editor billboards (custom textured) always render
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

        renderHandler->setBillboardDrawList(std::move(billboardDrawList));
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

        renderHandler->setTextDrawList(std::move(textDrawList));
    }
}
