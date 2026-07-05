#include "GPUDrivenRenderer.hpp"
#include "../virtualtexture/svt/SVTManager.hpp"
#include "../occlusion/HiZBuffer.hpp"
#include "../occlusion/DepthPrepass.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "../../animation/AnimatorStateMachine.hpp"
#include "../../animation/AnimationLayerStack.hpp"
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
    // VK-1334: per-thread RTT state, so concurrent recording of the main pass (Render task)
    // and an RTT pre-pass (RenderTexture task) cannot stomp on each other's overrides. Each
    // task runs to completion on one worker thread, so begin/record/end happen on the same TLS.
    namespace
    {
        thread_local vk::DescriptorSet tlsActiveCullDescriptorSet = nullptr;
        thread_local bool tlsHasTerrainViewProjectionOverride = false;
        thread_local glm::mat4 tlsTerrainViewProjectionOverride{1.0f};
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

        // Advance staging frame indices for double-buffered CPU→GPU uploads
        if (mergedBuffer)
            mergedBuffer->advanceStagingFrame();
        if (boneMatrixManager)
            boneMatrixManager->advanceStagingFrame();

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

        // VK-1415: the main viewport always renders ALL layers. The per-camera render-layer
        // cullingMask is applied only to RTT/secondary camera cull passes (beginRTTContext),
        // because this single object-cull also feeds the shared shadow (VSM) pass — masking the
        // main view here would drop shadow casters and affect other views. So the main camera's
        // CameraComponent.cullingMask is intentionally ignored; cameraParams.cullingMask keeps its
        // 0xFFFFFFFF default below.
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
            // .cullingMask intentionally omitted -> 0xFFFFFFFF default (main view = all layers).
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

        // Compute per-(batch, shaderGroup) occupancy so empty sections can skip their
        // indirect draw. Identity-mapped in edit mode; in play mode slot indices over-mark
        // inactive slots (safe superset, never under-marks). The RTT/minimap view renders the
        // same object set via the same batchManager, so this single pass covers it too.
        if (batchManager && mergedBuffer)
        {
            const uint32_t occCount = mergedBuffer->isPersistentMode()
                ? static_cast<uint32_t>(mergedBuffer->getCPUObjectData().size())
                : mergedBuffer->getObjectCount();
            batchManager->recomputeOccupancy(mergedBuffer->getCPUObjectData(), occCount);
        }
    }

    void GPUDrivenRenderer::beginRTTContext(const RTTRenderContext& ctx, const RTTCameraParams& params)
    {
        if (!initialized || !enabled || !ctx.cameraBuffer)
        {
            return;
        }

        // Fill the per-RTT GPU-cull camera buffer with the RTT camera params. This writes only
        // to the caller-owned buffer; the shared main cameraBuffer is untouched.
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
            .screenHeight = params.screenHeight,
            .cullingMask = params.cullingMask
        };
        ctx.cameraBuffer->update(cameraParams);

        // Make subsequent cull dispatches on THIS THREAD use the per-RTT descriptor set
        // (binding 1 -> RTT camera buffer). Thread-local so a concurrently-recording main pass
        // on another worker thread is not affected. Cleared by endRTTContext().
        tlsActiveCullDescriptorSet = ctx.cullDescriptorSet;

        // Per-thread terrain view-projection override. The terrain pipeline's
        // `viewProjection` member is shared; mutating it would race with a concurrent main
        // recording. Instead we publish the RTT VP through TLS and read it back at terrain
        // push-constant build time on this thread only.
        tlsHasTerrainViewProjectionOverride = true;
        tlsTerrainViewProjectionOverride = params.projection * params.view;
    }

    void GPUDrivenRenderer::endRTTContext()
    {
        if (!initialized || !enabled)
        {
            return;
        }

        tlsActiveCullDescriptorSet = nullptr;
        tlsHasTerrainViewProjectionOverride = false;
    }

    vk::DescriptorSet GPUDrivenRenderer::getThreadLocalCullDescriptorSet()
    {
        return tlsActiveCullDescriptorSet;
    }

    bool GPUDrivenRenderer::tryGetThreadLocalTerrainViewProjection(glm::mat4& outVP)
    {
        if (!tlsHasTerrainViewProjectionOverride)
        {
            return false;
        }
        outVP = tlsTerrainViewProjectionOverride;
        return true;
    }

    vk::DescriptorSet GPUDrivenRenderer::allocateRTTCullDescriptorSet(vk::DescriptorPool externalPool,
                                                                       vk::Buffer externalCameraBuffer)
    {
        if (!cullPipeline)
        {
            return nullptr;
        }
        return cullPipeline->allocateExternalDescriptorSet(externalPool, externalCameraBuffer);
    }

    void GPUDrivenRenderer::releaseRTTCullDescriptorSet(vk::DescriptorSet rttSet)
    {
        if (cullPipeline)
        {
            cullPipeline->releaseExternalDescriptorSet(rttSet);
        }
    }

    vk::DescriptorSetLayout GPUDrivenRenderer::getCullDescriptorSetLayout() const
    {
        if (!cullPipeline)
        {
            return nullptr;
        }
        return cullPipeline->getDescriptorSetLayout();
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
        // Streamed textures own their bindless slot and GPU image — release them
        // through the stream manager so re-registering after release works
        // (a stale stream entry would return a freed bindless index).
        bool wasStreamed = textureStreamManager && textureStreamManager->unregisterTexture(texturePath);
        if (!wasStreamed && bindlessTextures)
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

            // VK-1209: SVT-backed textures resolve to their bit-31-tagged index so the mesh shader
            // pages them through the SVT page table instead of sampling the bindless slot directly.
            if (svtManager)
            {
                auto sit = svtTaggedIndices.find(texPath);
                if (sit != svtTaggedIndices.end())
                    return sit->second;
            }

            return bindlessTextures->getTextureIndex(texPath);
        };
    }

    void GPUDrivenRenderer::ensureSVTManager()
    {
        if (!vtCache.svtEnabled || !bindlessTextures)
            return;

        if (!svtManager)
        {
            svtManager = std::make_unique<SVTManager>(device);
            SVTManager::Config cfg;
            cfg.poolBudgetMB = vtCache.svtPoolBudgetMB;
            cfg.pagesPerFrame = vtCache.pagesPerFrame;
            cfg.evictionAgeFrames = vtCache.evictionAgeFrames;
            cfg.srgb = true;
            svtManager->init(cfg);

            // UE5-style: the BC7 atlas lives in the bindless heap; its slot is baked into image info.
            if (const vt::VTPhysicalPool* pool = svtManager->getPool())
            {
                const uint32_t atlasIdx = bindlessTextures->registerTexture(
                    "__svt_atlas__", pool->planeView(0), pool->getSampler());
                svtManager->setAtlasBindlessIndex(atlasIdx);
            }
        }

        // Point the mesh pipelines' set-1 SVT bindings at the manager's buffers.
        auto wire = [&](MeshShaderPipeline* p)
        {
            if (p && p->isSVTSampleEnabled())
                p->updateSVTResources(svtManager->getPageTableBuffer(), svtManager->getFeedbackBuffer(),
                                      svtManager->getImageInfoBuffer());
        };
        wire(meshShaderPipeline.get());
        wire(transparentMeshShaderPipeline.get());
        wire(wboitMeshShaderPipeline.get());
    }

    bool GPUDrivenRenderer::isSVTActive() const
    {
        return svtManager != nullptr && svtManager->isInitialized();
    }

    void GPUDrivenRenderer::beginSVTFrame()
    {
        if (!svtManager) return;
        svtManager->markFeedbackReady();
        svtManager->beginFrameReadback();
    }

    void GPUDrivenRenderer::updateAndUploadSVT(vk::CommandBuffer cmd)
    {
        if (!svtManager) return;
        svtManager->clearFeedback(cmd); // fresh feedback for this frame's mesh draws
        svtManager->updateAndUpload(cmd, svtFrameCounter++);
    }

    void GPUDrivenRenderer::copySVTFeedback(vk::CommandBuffer cmd)
    {
        if (!svtManager) return;
        svtManager->copyFeedbackToStaging(cmd);
    }

    void GPUDrivenRenderer::applySVTToggle()
    {
        if (!initialized || !meshShaderPipeline)
            return;

        const bool enable = vtCache.svtEnabled;
        meshShaderPipeline->setSVTSampleEnabled(enable);
        if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->setSVTSampleEnabled(enable);
        if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->setSVTSampleEnabled(enable);

        recreateScenePipelinesForSVT(); // rebuild with set-1 SVT bindings + SVT_ENABLED matching the flag

        if (enable)
            ensureSVTManager();
        else
        {
            svtManager.reset();
            svtTaggedIndices.clear();
        }
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

            // Upload the AnimationLayerStack's composed pose (finalBoneMatrices): it
            // includes overlay-layer blending AND the IK post-process. The base state
            // machine's currentBoneMatrices is only the base-layer pose (pre-IK), so
            // reading it here silently dropped IK / additive layers. Fall back to the
            // state machine only when the layer-stack pose is unavailable.
            const animation::AnimationLayerStack* layerStack = animatorSystem.getLayerStack(entity);
            const std::vector<glm::mat4>& boneMatrices =
                (layerStack && !layerStack->getBoneMatrices().empty())
                    ? layerStack->getBoneMatrices()
                    : animator->getBoneMatrices();
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

        // Defragmentation pass
        if (boneMatrixManager->shouldDefragment() || boneMatrixManager->isDefragInProgress())
        {
            auto moves = boneMatrixManager->defragStep(4);
            if (!moves.empty())
                patchBoneOffsetsInGPUData(moves);
        }

        return [this](entt::entity entity) -> uint32_t
        {
            return boneMatrixManager->getBoneOffset(entity);
        };
    }

    void GPUDrivenRenderer::patchBoneOffsetsInGPUData(const std::vector<BoneDefragResult>& moves)
    {
        if (!mergedBuffer || !mergedBuffer->isPersistentMode())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& move : moves)
        {
            if (!registry.valid(move.entity))
                continue;

            auto* uuidComp = registry.try_get<components::UUIDComponent>(move.entity);
            if (!uuidComp)
                continue;

            uint32_t slot = mergedBuffer->getSlotForEntityUUID(uuidComp->id.getValue());
            if (slot == UINT32_MAX)
                continue;

            auto& obj = mergedBuffer->getMutableObjectData(slot);
            obj.meshletLod3.w = move.newOffset;
            mergedBuffer->markSlotDirty(slot);
        }
    }

    void GPUDrivenRenderer::patchRenderTextureMaterialSlots(uint32_t imageIndex)
    {
        // Runtime (persistent) path only. The editor live-preview (non-persistent rebuild path)
        // re-derives texture indices each rebuild and is OUT OF SCOPE here — follow-up if needed.
        if (!mergedBuffer || !mergedBuffer->isPersistentMode() || !bindlessTextures)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::MaterialComponent, components::UUIDComponent>();
        for (auto e : view)
        {
            const auto& mat = view.get<components::MaterialComponent>(e);
            if (mat.renderTextureSlotBindings.empty())
                continue;

            uint32_t slot = mergedBuffer->getSlotForEntityUUID(view.get<components::UUIDComponent>(e).id.getValue());
            if (slot == UINT32_MAX)
                continue;

            auto& obj = mergedBuffer->getMutableObjectData(slot);
            bool dirty = false;
            for (const auto& [slotKey, binding] : mat.renderTextureSlotBindings)
            {
                if (binding.source == entt::null || !registry.valid(binding.source))
                    continue;
                const auto* rtt = registry.try_get<components::RenderTextureComponent>(binding.source);
                if (!rtt || rtt->textureId == rendertexture::INVALID_RENDER_TEXTURE_ID)
                    continue;

                std::string key = "__rtt_" + std::to_string(rtt->textureId) + "__#" + std::to_string(imageIndex);
                uint32_t idx = bindlessTextures->getTextureIndex(key);
                if (idx == INVALID_TEXTURE_INDEX)
                    continue;

                if (slotKey == "albedo")
                {
                    obj.textureIndices0.x = idx;
                    dirty = true;
                }
                // Only the albedo slot is RTT-drivable. Any other stored key (e.g. a stale
                // "emission" binding from an older scene) is intentionally ignored.
            }
            if (dirty)
                mergedBuffer->markSlotDirty(slot);
        }
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

    std::pair<vk::ImageView, vk::Sampler> GPUDrivenRenderer::getPrepassHiZViewSampler() const
    {
        if (prepassHiZ && prepassHiZ->isInitialized())
            return {prepassHiZ->getHiZImageView(), prepassHiZ->getHiZSampler()};
        return {nullptr, nullptr};
    }

    vk::ImageView GPUDrivenRenderer::getPrepassNormalImageView() const
    {
        if (depthPrepass && depthPrepass->isInitialized())
            return depthPrepass->getNormalImageView();
        return nullptr;
    }

    vk::Image GPUDrivenRenderer::getPrepassNormalImage() const
    {
        if (depthPrepass && depthPrepass->isInitialized())
            return depthPrepass->getNormalImage();
        return nullptr;
    }

    vk::Image GPUDrivenRenderer::getPrepassDiffuseAlbedoImage() const
    {
        if (depthPrepass && depthPrepass->isInitialized() && depthPrepass->isAlbedoEnabled())
            return depthPrepass->getDiffuseAlbedoImage();
        return nullptr;
    }

    vk::ImageView GPUDrivenRenderer::getPrepassDiffuseAlbedoImageView() const
    {
        if (depthPrepass && depthPrepass->isInitialized() && depthPrepass->isAlbedoEnabled())
            return depthPrepass->getDiffuseAlbedoImageView();
        return nullptr;
    }

    vk::Image GPUDrivenRenderer::getPrepassSpecularAlbedoImage() const
    {
        if (depthPrepass && depthPrepass->isInitialized() && depthPrepass->isAlbedoEnabled())
            return depthPrepass->getSpecularAlbedoImage();
        return nullptr;
    }

    vk::ImageView GPUDrivenRenderer::getPrepassSpecularAlbedoImageView() const
    {
        if (depthPrepass && depthPrepass->isInitialized() && depthPrepass->isAlbedoEnabled())
            return depthPrepass->getSpecularAlbedoImageView();
        return nullptr;
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
            terrain.meshBuffer->isInitialized())
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

        // VK-1209: opt a large BC7 .vfImage into SVT. registerTexture gates on format/size and
        // returns a bit-31-tagged index (else VT_INVALID_TILE); the resolver then returns that
        // tagged index. `fallback` (the whole-image bindless slot) is sampled while a page streams.
        auto trySVTRegister = [&](const std::string& texPath, uint32_t fallback)
        {
            if (svtManager && texPath.size() >= 8 && texPath.ends_with(".vfImage") && fallback < 4096u)
            {
                uint32_t tagged = svtManager->registerTexture(texPath, fallback);
                if (tagged != INVALID_TEXTURE_INDEX)
                    svtTaggedIndices[texPath] = tagged;
            }
        };

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
                    trySVTRegister(texPath, idx);
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
                uint32_t idx = bindlessTextures->registerTexture(texPath, view, sampler);
                registered = true;
                trySVTRegister(texPath, idx);
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
