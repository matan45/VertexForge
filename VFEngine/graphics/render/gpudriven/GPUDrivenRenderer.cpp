#include "GPUDrivenRenderer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "../../animation/AnimatorStateMachine.hpp"
#include "resource/ResourceManager.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Logger.hpp"
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <queue>
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

        // Create mesh stream manager (streaming enabled by default)
        if (meshStreamingEnabled)
        {
            meshStreamManager = std::make_unique<mesh::MeshStreamManager>(device, *mergedBuffer);
            loggerInfo("GPUDrivenRenderer: Mesh streaming enabled by default");
        }

        batchManager = std::make_unique<IndirectBatchManager>(device);
        if (!batchManager->initWithAutoConfig())
        {
            loggerError("GPUDrivenRenderer: Failed to initialize batch manager - GPU memory allocation failed");
            // Fall back to minimal configuration
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

        // Create camera UBO for compute shader
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

            // Create bone matrix manager for GPU skinning
            boneMatrixManager = std::make_unique<BoneMatrixManager>(device);
            boneMatrixManager->init();

            meshShaderPipeline = std::make_unique<MeshShaderPipeline>(device, swapChain);
            meshShaderPipeline->init(iblDescriptorSetLayout, bindlessTextures->getDescriptorSetLayout(),
                                     boneMatrixManager->getDescriptorSetLayout(), renderPass);

            if (meshStreamManager)
            {
                meshStreamManager->setMeshletBuffer(meshletBuffer.get());
                loggerInfo("GPUDrivenRenderer: Meshlet streaming enabled");
            }
        }
        else
        {
            loggerError(
                "GPUDrivenRenderer: Mesh shaders not supported - GPU-driven rendering requires mesh shader support");
            loggerError("GPUDrivenRenderer: The VK_EXT_mesh_shader extension with task shader support is required");
            return;
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

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // Cleanup sub-components
        if (meshShaderPipeline) meshShaderPipeline->cleanup();
        if (boneMatrixManager) boneMatrixManager->cleanup();
        if (meshletBuffer) meshletBuffer->cleanup();
        if (cameraBuffer) cameraBuffer->cleanup();
        if (cullPipeline) cullPipeline->cleanup();
        if (bindlessTextures) bindlessTextures->cleanup();
        if (batchManager) batchManager->cleanup();
        if (mergedBuffer) mergedBuffer->cleanup();

        meshStreamManager.reset();
        meshShaderPipeline.reset();
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

        if (meshStreamManager)
        {
            for (const auto& meshRender : opaqueObjects)
            {
                meshStreamManager->requestMesh(meshRender.meshPath);
            }

            meshStreamManager->update(cameraPosition);
        }

        if (materialTextureCache && bindlessTextures)
        {
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

        TextureIndexResolver textureResolver = nullptr;
        if (bindlessTextures)
        {
            auto pbrCache = std::make_shared<std::unordered_map<std::string, mesh::ExtractedPBRValues>>();

            textureResolver = [this, pbrCache](const std::string& materialPath, TextureSlotType slot) -> uint32_t
            {
                if (materialPath.empty())
                {
                    return INVALID_TEXTURE_INDEX;
                }

                auto it = pbrCache->find(materialPath);
                if (it == pbrCache->end())
                {
                    it = pbrCache->emplace(materialPath,
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

        // Shader group indices:
        // 0 = default (no Time node)
        // 1 = UV animation (Time connected to texture UV)
        // 2 = Emission animation (Time connected to EmissionStrength)
        ShaderGroupResolver shaderGroupResolver = [](const std::string& materialPath) -> uint32_t
        {
            if (materialPath.empty())
            {
                return 0;
            }

            std::string parentPath = materialPath;

            if (material::isInstanceFile(materialPath))
            {
                auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
                if (instanceData && !instanceData->parentMaterialPath.empty())
                {
                    parentPath = instanceData->parentMaterialPath;
                }
                else
                {
                    return 0;
                }
            }

            auto matData = resource::ResourceManager::loadMaterial(parentPath);
            if (!matData)
            {
                return 0;
            }

            // Find Time node ID
            uint32_t timeNodeId = 0;
            bool hasTimeNode = false;
            for (const auto& node : matData->graph.nodes)
            {
                if (node.type == material::NodeType::Time)
                {
                    timeNodeId = node.id;
                    hasTimeNode = true;
                    break;
                }
            }

            if (!hasTimeNode)
            {
                return 0;
            }

            // Trace connections from Time node to determine animation type
            // Use BFS to find what the Time node ultimately connects to
            std::unordered_set<uint32_t> visitedNodes;
            std::queue<uint32_t> nodesToVisit;
            nodesToVisit.push(timeNodeId);

            while (!nodesToVisit.empty())
            {
                uint32_t currentNodeId = nodesToVisit.front();
                nodesToVisit.pop();

                if (visitedNodes.contains(currentNodeId))
                {
                    continue;
                }
                visitedNodes.insert(currentNodeId);

                for (const auto& link : matData->graph.links)
                {
                    if (link.sourceNodeId == currentNodeId)
                    {
                        for (const auto& node : matData->graph.nodes)
                        {
                            if (node.id == link.targetNodeId)
                            {
                                if (node.type == material::NodeType::PBROutput &&
                                    link.targetPin == "EmissionStrength")
                                {
                                    return 2; // Emission animation
                                }

                                if (node.type == material::NodeType::TextureSample &&
                                    link.targetPin == "UV")
                                {
                                    return 1; // UV animation
                                }
                                nodesToVisit.push(link.targetNodeId);
                                break;
                            }
                        }
                    }
                }
            }

            return 1;
        };

        // Process ALL animated entities and update bone matrices
        // Important: We must update bone matrices for all animated entities, not just visible ones,
        // because GPU meshlet culling may render meshlets that CPU frustum culling missed
        BoneOffsetResolver boneOffsetResolver = nullptr;
        if (boneMatrixManager)
        {
            auto& animatorSystem = animation::RuntimeAnimatorSystem::instance();
            auto& registry = scene::EntityRegistry::getRegistry();

            // Iterate through ALL entities with MeshComponent that have an animatorPath
            // Don't require AnimatorComponent as it might not be added yet on first frame
            auto view = registry.view<components::MeshComponent>();
            for (auto entity : view)
            {
                const auto& meshComp = view.get<components::MeshComponent>(entity);

                // Skip entities without animator path
                if (meshComp.animatorPath.empty())
                {
                    continue;
                }

                // Get animator for this entity, create if needed
                animation::AnimatorStateMachine* animator = animatorSystem.getAnimator(entity);
                if (!animator)
                {
                    // Animator doesn't exist yet - create it now
                    // This handles the case where syncWithRegistry hasn't run yet
                    animatorSystem.initializeEntityAnimator(entity, meshComp.animatorPath);
                    animator = animatorSystem.getAnimator(entity);
                    if (!animator)
                    {
                        continue;
                    }
                }

                // Ensure animator is updated (in case it wasn't updated earlier this frame)
                if (animator->isInitialized() && animator->isPlaying())
                {
                    // Force an update if bone matrices are empty (first frame issue)
                    if (animator->getBoneMatrices().empty())
                    {
                        loggerInfo("GPUDrivenRenderer: Forcing animator update for entity {} (bone matrices empty)",
                                   static_cast<uint32_t>(entity));
                        animator->update(0.016f); // ~60fps delta
                    }
                }
                else
                {
                    loggerWarning("GPUDrivenRenderer: Animator for entity {} not ready (initialized={}, playing={})",
                                  static_cast<uint32_t>(entity), animator->isInitialized(), animator->isPlaying());
                }

                // Get bone matrices from animator
                const std::vector<glm::mat4>& boneMatrices = animator->getBoneMatrices();
                if (boneMatrices.empty())
                {
                    loggerWarning("GPUDrivenRenderer: Bone matrices still empty for entity {} after update",
                                  static_cast<uint32_t>(entity));
                    continue;
                }

                // Allocate bone space if needed (returns existing allocation if already allocated)
                uint32_t boneCount = static_cast<uint32_t>(boneMatrices.size());
                uint32_t boneOffset = boneMatrixManager->allocate(entity, boneCount);

                if (boneOffset != INVALID_BONE_OFFSET)
                {
                    // Update bone matrices for this entity
                    boneMatrixManager->updateBoneMatrices(entity, boneMatrices);
                }
            }

            // Create resolver that looks up bone offset for each entity
            boneOffsetResolver = [this](entt::entity entity) -> uint32_t
            {
                return boneMatrixManager->getBoneOffset(entity);
            };
        }

        // Update object buffer with current frame's render data (pass time for Time node evaluation)
        mergedBuffer->updateObjects(opaqueObjects, textureResolver, shaderGroupResolver, boneOffsetResolver, time);

        // Update camera data for compute shader
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

        // Update cull pipeline descriptors with combined batch buffers
        cullPipeline->updateDescriptors(
            mergedBuffer->getObjectBuffer(),
            cameraBuffer->getBuffer(),
            batchManager->getCombinedDrawCommandBuffer(),
            batchManager->getCombinedPerDrawDataBuffer(),
            batchManager->getCombinedDrawCountBuffer()
        );

        // Update per-draw data descriptor for mesh shader pipeline (uses combined buffer)
        // Only update when there's actual data to render
        if (meshShaderPipeline && mergedBuffer->getObjectCount() > 0)
        {
            meshShaderPipeline->updatePerDrawDescriptor(batchManager->getCombinedPerDrawDataBuffer());
            meshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            meshShaderPipeline->updateVertexDescriptors(*mergedBuffer);
        }

        // Update stats
        stats.totalObjects = mergedBuffer->getObjectCount();
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

        if (stats.totalObjects == 0)
        {
            return;
        }
        mergedBuffer->uploadObjects(cmd);

        // Upload bone matrices for animated entities
        if (boneMatrixManager)
        {
            boneMatrixManager->uploadToGPU(cmd);
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

        cullPipeline->dispatch(cmd, stats.totalObjects);
        batchManager->insertBarriersAfterCompute(cmd);
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

        // Bind all 6 descriptor sets:
        // Set 0: IBL (camera UBO + IBL textures)
        // Set 1: Per-draw data
        // Set 2: Bindless textures
        // Set 3: Meshlet data (meshlet buffer, vertex indices, primitive indices)
        // Set 4: Vertex data (merged vertex buffer)
        // Set 5: Bone matrices (for GPU skinning)
        std::array<vk::DescriptorSet, 6> descriptorSets = {
            iblDescriptorSet,
            meshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            meshShaderPipeline->getMeshletDataDescriptorSet(),
            meshShaderPipeline->getVertexDataDescriptorSet(),
            boneMatrixManager->getDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        auto extent = swapChain.getSwapchainExtent();

        // Render shader groups: 0 (default), 1 (UV animation), 2 (emission animation)
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

        // Read back and aggregate stats from all batches (expensive - causes sync)
        GPUDrivenStats aggregated = batchManager->readBackAggregatedStats();

        stats.visibleObjects = aggregated.visibleObjects;
        stats.drawCalls = aggregated.drawCalls; // One draw call per batch

        // LOD distribution from GPU (aggregated across all batches)
        stats.objectsLOD0 = aggregated.objectsLOD0;
        stats.objectsLOD1 = aggregated.objectsLOD1;
        stats.objectsLOD2 = aggregated.objectsLOD2;
        stats.objectsLOD3 = aggregated.objectsLOD3;

        // Culling stats directly from GPU counters (aggregated)
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

        if (meshShaderPipeline && boneMatrixManager)
        {
            meshShaderPipeline->recreate(cachedIBLLayout, bindlessTextures->getDescriptorSetLayout(),
                                         boneMatrixManager->getDescriptorSetLayout(), cachedRenderPass);
        }
    }
}
