#include "GPUDrivenRenderer.hpp"
#include "../occlusion/HiZBuffer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../mesh/ClusterStreamManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "../../animation/AnimatorStateMachine.hpp"
#include "resource/ResourceManager.hpp"
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

            clusterBuffer = std::make_unique<ClusterBuffer>(device);
            clusterBuffer->init();
            loggerInfo("GPUDrivenRenderer: ClusterBuffer initialized for DAG cluster rendering");

            clusterTraversalPipeline = std::make_unique<ClusterDAGTraversalPipeline>(device);
            clusterTraversalPipeline->init();
            loggerInfo("GPUDrivenRenderer: ClusterDAGTraversalPipeline initialized for DAG traversal");

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

            // Initialize cluster streaming manager for DAG data
            if (clusterBuffer)
            {
                clusterStreamManager = std::make_unique<mesh::ClusterStreamManager>(device, *clusterBuffer);
                clusterStreamManager->setMeshletBuffer(meshletBuffer.get());
                clusterStreamManager->setMergedMeshBuffer(mergedBuffer.get());  // VK-300: For marking cluster availability
                loggerInfo("GPUDrivenRenderer: ClusterStreamManager initialized for DAG streaming");
            }
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

        if (lightOcclusionCulling) lightOcclusionCulling->cleanup();
        if (meshShaderPipeline) meshShaderPipeline->cleanup();
        if (shadowSystem) shadowSystem->cleanup();
        if (lightCullingPipeline) lightCullingPipeline->cleanup();
        if (clusterGridManager) clusterGridManager->cleanup();
        if (lightBufferManager) lightBufferManager->cleanup();
        if (boneMatrixManager) boneMatrixManager->cleanup();
        if (clusterTraversalPipeline) clusterTraversalPipeline->cleanup();
        if (clusterBuffer) clusterBuffer->cleanup();
        if (meshletBuffer) meshletBuffer->cleanup();
        if (cameraBuffer) cameraBuffer->cleanup();
        if (cullPipeline) cullPipeline->cleanup();
        if (bindlessTextures) bindlessTextures->cleanup();
        if (batchManager) batchManager->cleanup();
        if (mergedBuffer) mergedBuffer->cleanup();

        clusterStreamManager.reset();
        meshStreamManager.reset();
        lightOcclusionCulling.reset();
        meshShaderPipeline.reset();
        shadowSystem.reset();
        lightCullingPipeline.reset();
        clusterGridManager.reset();
        lightBufferManager.reset();
        boneMatrixManager.reset();
        clusterTraversalPipeline.reset();
        clusterBuffer.reset();
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

        // VK-300: Create cluster DAG resolver for Nanite-style rendering
        ClusterDAGResolver clusterResolver = nullptr;
        if (clusterBuffer)
        {
            clusterResolver = [this](const std::string& meshPath,
                                     const std::string& submeshName,
                                     uint32_t submeshIndex) -> ClusterDAGResolverResult
            {
                ClusterDAGResolverResult result{};
                const auto* alloc = clusterBuffer->getAllocation(meshPath, submeshName, submeshIndex);
                if (alloc && alloc->isAllocated)
                {
                    ClusterDAGInfo info = clusterBuffer->getClusterDAGInfo(*alloc);
                    result.clusterOffset = info.clusterOffset;
                    result.clusterCount = info.clusterCount;
                    result.dagHeaderIndex = info.dagHeaderIndex;
                    result.hasClusterData = info.clusterCount > 0;
                }
                return result;
            };
        }

        mergedBuffer->updateObjects(opaqueObjects, textureResolver, shaderGroupResolver, boneOffsetResolver, clusterResolver, time);

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

        updateClusterGrid(projection, nearPlane, farPlane);
        updatePipelineDescriptors();

        stats.totalObjects = mergedBuffer->getObjectCount();

        // VK-300: Debug logging for rendering issues
        static bool loggedObjectCount = false;
        uint32_t registeredCount = static_cast<uint32_t>(mergedBuffer->getRegisteredMeshes().size());
        if (!loggedObjectCount && stats.totalObjects == 0 && registeredCount > 0)
        {
            loggerWarning("GPUDrivenRenderer: 0 objects to render despite {} registered meshes - "
                          "meshes may lack meshlet data (re-import with version >= 0.0.4)",
                          registeredCount);
            loggedObjectCount = true;
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

            // VK-300: Also request cluster DAG data for Nanite-style rendering
            if (clusterStreamManager)
            {
                clusterStreamManager->requestMesh(meshRender.meshPath);
            }
        }

        meshStreamManager->update(cameraPosition);

        // VK-300: Update cluster streaming for DAG data
        if (clusterStreamManager)
        {
            static uint64_t clusterStreamFrameIndex = 0;
            clusterStreamManager->update(cameraPosition, clusterStreamFrameIndex++);
        }
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
            batchManager->getCombinedDrawCountBuffer(),
            batchManager->getObjectDrawIndexBuffer()
        );

        // VK-300: Update DAG traversal pipeline descriptors
        if (clusterTraversalPipeline && clusterBuffer)
        {
            clusterTraversalPipeline->updateDescriptors(
                mergedBuffer->getObjectBuffer(),
                cameraBuffer->getBuffer(),
                batchManager->getObjectDrawIndexBuffer(),
                *clusterBuffer
            );
        }

        if (meshShaderPipeline && mergedBuffer->getObjectCount() > 0)
        {
            meshShaderPipeline->updatePerDrawDescriptor(batchManager->getCombinedPerDrawDataBuffer());
            meshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            meshShaderPipeline->updateVertexDescriptors(*mergedBuffer);

            if (clusterBuffer)
            {
                meshShaderPipeline->updateClusterDescriptors(*clusterBuffer);
            }

            if (lightBufferManager && clusterGridManager && lightCullingPipeline)
            {
                meshShaderPipeline->updateLightingDescriptors(
                    lightBufferManager->getDescriptorSet(),
                    clusterGridManager->getDescriptorSet(),
                    lightCullingPipeline->getDescriptorSet());
            }

            if (shadowSystem && shadowSystem->isInitialized())
            {
                meshShaderPipeline->updateShadowDescriptors(
                    shadowSystem->getShadowDataDescSet(),
                    shadowSystem->getShadowTextureDescSet());
            }
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

        if (stats.totalObjects == 0)
        {
            return;
        }
        mergedBuffer->uploadObjects(cmd);

        if (boneMatrixManager)
        {
            boneMatrixManager->uploadToGPU(cmd);
        }

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

        // VK-300: DAG cluster traversal for hierarchical LOD selection
        if (clusterTraversalPipeline && clusterBuffer && clusterBuffer->getCurrentClusterCount() > 0)
        {
            // Barrier after cull pipeline - objectDrawIndexMap must be written before traversal reads it
            vk::MemoryBarrier cullToTraversalBarrier{
                vk::AccessFlagBits::eShaderWrite,
                vk::AccessFlagBits::eShaderRead
            };
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                1, &cullToTraversalBarrier,
                0, nullptr,
                0, nullptr
            );

            // Calculate projection factor: screenHeight / (2 * tan(fovY/2))
            auto extent = swapChain.getSwapchainExtent();
            float projectionFactor = cachedCameraProjection[1][1] * static_cast<float>(extent.height) * 0.5f;

            // Set traversal parameters
            static uint32_t frameIndex = 0;
            clusterTraversalPipeline->setTraversalParams(
                projectionFactor,
                1.0f,  // screenErrorThreshold (pixels)
                1.0f,  // errorMultiplier
                frameIndex++,
                frustumCullingEnabled,
                occlusionCullingEnabled
            );

            // Dispatch DAG traversal
            clusterTraversalPipeline->dispatch(cmd, stats.totalObjects, *clusterBuffer);
        }

        batchManager->insertBarriersAfterCompute(cmd);

        if (shadowSystem && shadowSystem->isShadowsEnabled() &&
            meshShaderPipeline && boneMatrixManager && batchManager)
        {
            shadowSystem->uploadToGPU(cmd);

            shadow::ShadowPassParams shadowParams{};
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

            shadowSystem->recordShadowPass(cmd, shadowParams);
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

        // VK-300: Render BOTH paths - DAG objects AND fallback objects
        // This ensures meshes with cluster data use DAG, and meshes without use direct meshlet

        // Path 1: DAG cluster rendering for objects with cluster data
        if (clusterBuffer && clusterBuffer->getCurrentClusterCount() > 0)
        {
            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = 0;  // Not used in DAG mode - drawIndex comes from selection

            pushConstants.viewMode = currentViewMode;
            if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = static_cast<float>(extent.width);
            pushConstants.screenHeight = static_cast<float>(extent.height);

            // Set cluster selection range - clusterCount used for bounds checking in shader
            pushConstants.clusterBaseIndex = 0;
            pushConstants.clusterCount = MAX_CLUSTER_SELECTIONS_PER_FRAME;

            cmd.pushConstants(
                layout,
                vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(MeshShaderPushConstants),
                &pushConstants);

            // Use indirect dispatch with actual selectedCount from DAG traversal
            cmd.drawMeshTasksIndirectEXT(
                clusterBuffer->getIndirectDrawCommandBuffer(),
                0,  // offset
                1,  // drawCount (single indirect command)
                12  // stride (sizeof MeshTasksIndirectCommand)
            );
        }

        // Path 2: Fallback direct meshlet rendering for objects WITHOUT cluster data
        // This renders all objects - the gpu_cull_lod shader generates commands for all objects
        // Objects with DAG data will have been rendered above, but rendering them again
        // with the fallback path is harmless (they have valid meshlet data too)
        {
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(0, 0);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(0, 0);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = 0;

            pushConstants.viewMode = currentViewMode;
            if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = static_cast<float>(extent.width);
            pushConstants.screenHeight = static_cast<float>(extent.height);
            pushConstants.clusterBaseIndex = 0;
            pushConstants.clusterCount = 0;  // 0 triggers direct meshlet mode in task shader

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

        // VK-300: Also update Hi-Z for DAG traversal pipeline
        if (clusterTraversalPipeline && hiZView && hiZSampler)
        {
            clusterTraversalPipeline->updateHiZDescriptor(hiZView, hiZSampler);
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
}
