#include "GPUDrivenRenderer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "resource/ResourceManager.hpp"
#include "material/MaterialInstanceTypes.hpp"
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


namespace render::gpudriven {

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
        if (initialized) {
            return;
        }

        loggerInfo("GPUDrivenRenderer: Initializing...");

        cachedIBLLayout = iblDescriptorSetLayout;
        cachedRenderPass = renderPass;

        // Initialize sub-components
        mergedBuffer = std::make_unique<MergedMeshBuffer>(device);
        mergedBuffer->init();

        // Create mesh stream manager (streaming enabled by default)
        if (meshStreamingEnabled) {
            meshStreamManager = std::make_unique<mesh::MeshStreamManager>(device, *mergedBuffer);
            loggerInfo("GPUDrivenRenderer: Mesh streaming enabled by default");
        }

        batchManager = std::make_unique<IndirectBatchManager>(device);
        if (!batchManager->initWithAutoConfig()) {
            loggerError("GPUDrivenRenderer: Failed to initialize batch manager - GPU memory allocation failed");
            // Fall back to minimal configuration
            if (!batchManager->init(2, 50000, 8)) {
                loggerError("GPUDrivenRenderer: Even minimal batch configuration failed - GPU-driven rendering unavailable");
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

        // Check mesh shader support
        const auto& meshCaps = device.getMeshShaderCapabilities();
        meshShaderSupported = meshCaps.meshShaderSupported && meshCaps.taskShaderSupported;

        if (meshShaderSupported)
        {
            loggerInfo("GPUDrivenRenderer: Mesh shader supported - using Task+Mesh shader pipeline");

            // Initialize meshlet buffer
            meshletBuffer = std::make_unique<MeshletBuffer>(device);
            meshletBuffer->init();

            // Create mesh shader pipeline for GPU-driven rendering
            meshShaderPipeline = std::make_unique<MeshShaderPipeline>(device, swapChain);
            meshShaderPipeline->init(iblDescriptorSetLayout, bindlessTextures->getDescriptorSetLayout(), renderPass);

            // Connect meshlet buffer to stream manager for mesh shader data streaming
            if (meshStreamManager)
            {
                meshStreamManager->setMeshletBuffer(meshletBuffer.get());
                loggerInfo("GPUDrivenRenderer: Meshlet streaming enabled");
            }
        }
        else
        {
            loggerError("GPUDrivenRenderer: Mesh shaders not supported - GPU-driven rendering requires mesh shader support");
            loggerError("GPUDrivenRenderer: The VK_EXT_mesh_shader extension with task shader support is required");
            // GPU-driven rendering will be unavailable
            // The renderer will remain initialized but disabled
            return;
        }

        initialized = true;
        loggerInfo("GPUDrivenRenderer: Initialized successfully");
    }

    void GPUDrivenRenderer::cleanup()
    {
        if (!initialized) {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // Cleanup sub-components
        if (meshShaderPipeline) meshShaderPipeline->cleanup();
        if (meshletBuffer) meshletBuffer->cleanup();
        if (cameraBuffer) cameraBuffer->cleanup();
        if (cullPipeline) cullPipeline->cleanup();
        if (bindlessTextures) bindlessTextures->cleanup();
        if (batchManager) batchManager->cleanup();
        if (mergedBuffer) mergedBuffer->cleanup();

        meshStreamManager.reset();
        meshShaderPipeline.reset();
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
        if (!initialized || !bindlessTextures) {
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
        if (!initialized || !enabled) {
            return;
        }

        // Update mesh streaming - request all meshes in the scene
        if (meshStreamManager) {
            for (const auto& meshRender : opaqueObjects) {
                meshStreamManager->requestMesh(meshRender.meshPath);
            }
            
            meshStreamManager->update(cameraPosition); // Assume ~60fps delta
        }

        // Register textures for all materials in the scene (done once per material)
        if (materialTextureCache && bindlessTextures) {
            for (const auto& meshRender : opaqueObjects) {
                // Register default material textures
                if (!meshRender.defaultMaterialPath.empty()) {
                    registerMaterialTextures(meshRender.defaultMaterialPath);
                }
                // Register submesh material textures
                for (const auto& [submeshName, subMat] : meshRender.submeshMaterials) {
                    if (!subMat.materialPath.empty()) {
                        registerMaterialTextures(subMat.materialPath);
                    }
                }
            }
        }

        // Create texture resolver callback that uses BindlessTextureManager
        // Use frame-local cache to avoid re-extracting PBR values for same material multiple times
        // Handles both materials (.vfMat) and material instances (.vfMatInstance)
        TextureIndexResolver textureResolver = nullptr;
        if (bindlessTextures) {
            // Cache extracted PBR values per material/instance path (avoids 8x extraction per material)
            auto pbrCache = std::make_shared<std::unordered_map<std::string, mesh::ExtractedPBRValues>>();

            textureResolver = [this, pbrCache](const std::string& materialPath, TextureSlotType slot) -> uint32_t {
                if (materialPath.empty()) {
                    return INVALID_TEXTURE_INDEX;
                }

                // Check cache first
                auto it = pbrCache->find(materialPath);
                if (it == pbrCache->end()) {
                    // Use unified extraction method that handles both materials and instances
                    it = pbrCache->emplace(materialPath,
                        mesh::MaterialPBRExtractor::extractPBRFromPath(materialPath)).first;
                }

                const auto& pbrValues = it->second;

                // Get the appropriate texture path based on slot
                std::string texPath;
                switch (slot) {
                    case TextureSlotType::Albedo:    texPath = pbrValues.albedoTexturePath; break;
                    case TextureSlotType::Normal:    texPath = pbrValues.normalTexturePath; break;
                    case TextureSlotType::ORM:       texPath = pbrValues.ormTexturePath; break;
                    case TextureSlotType::Metallic:  texPath = pbrValues.metallicTexturePath; break;
                    case TextureSlotType::Roughness: texPath = pbrValues.roughnessTexturePath; break;
                    case TextureSlotType::AO:        texPath = pbrValues.aoTexturePath; break;
                    case TextureSlotType::Emission:  texPath = pbrValues.emissionTexturePath; break;
                    case TextureSlotType::Height:    texPath = pbrValues.heightTexturePath; break;
                    default: return INVALID_TEXTURE_INDEX;
                }

                if (texPath.empty()) {
                    return INVALID_TEXTURE_INDEX;
                }

                // Look up the registered texture index
                return bindlessTextures->getTextureIndex(texPath);
            };
        }

        // Shader group resolver: materials with Time nodes use group 1 for UV animation
        // Shader group indices:
        // 0 = default (no Time node)
        // 1 = UV animation (Time connected to texture UV)
        // 2 = Emission animation (Time connected to EmissionStrength)
        ShaderGroupResolver shaderGroupResolver = [](const std::string& materialPath) -> uint32_t {
            if (materialPath.empty()) {
                return 0;
            }

            // Load material and check for Time nodes
            std::string parentPath = materialPath;

            // Handle material instances - resolve to parent
            if (material::isInstanceFile(materialPath)) {
                auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
                if (instanceData && !instanceData->parentMaterialPath.empty()) {
                    parentPath = instanceData->parentMaterialPath;
                } else {
                    return 0;
                }
            }

            auto matData = resource::ResourceManager::loadMaterial(parentPath);
            if (!matData) {
                return 0;
            }

            // Find Time node ID
            uint32_t timeNodeId = 0;
            bool hasTimeNode = false;
            for (const auto& node : matData->graph.nodes) {
                if (node.type == material::NodeType::Time) {
                    timeNodeId = node.id;
                    hasTimeNode = true;
                    break;
                }
            }

            if (!hasTimeNode) {
                return 0;
            }

            // Trace connections from Time node to determine animation type
            // Use BFS to find what the Time node ultimately connects to
            std::unordered_set<uint32_t> visitedNodes;
            std::queue<uint32_t> nodesToVisit;
            nodesToVisit.push(timeNodeId);

            while (!nodesToVisit.empty()) {
                uint32_t currentNodeId = nodesToVisit.front();
                nodesToVisit.pop();

                if (visitedNodes.contains(currentNodeId)) {
                    continue;
                }
                visitedNodes.insert(currentNodeId);

                // Find all links where this node is the source
                for (const auto& link : matData->graph.links) {
                    if (link.sourceNodeId == currentNodeId) {
                        // Check if target is PBROutput with EmissionStrength pin
                        for (const auto& node : matData->graph.nodes) {
                            if (node.id == link.targetNodeId) {
                                if (node.type == material::NodeType::PBROutput &&
                                    link.targetPin == "EmissionStrength") {
                                    return 2;  // Emission animation
                                }
                                // Check if target is TextureSample with UV pin
                                if (node.type == material::NodeType::TextureSample &&
                                    link.targetPin == "UV") {
                                    return 1;  // UV animation
                                }
                                // Continue tracing through intermediate nodes
                                nodesToVisit.push(link.targetNodeId);
                                break;
                            }
                        }
                    }
                }
            }

            // Default to UV animation if Time node exists but connection unclear
            return 1;
        };

        // Update object buffer with current frame's render data (pass time for Time node evaluation)
        mergedBuffer->updateObjects(opaqueObjects, textureResolver, shaderGroupResolver, time);

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

        // Ensure all async buffer transfers are complete before rendering
        // This prevents flickering from incomplete mesh/meshlet data
        if (mergedBuffer)
        {
            mergedBuffer->flushPendingTransfers();
        }
        if (meshletBuffer)
        {
            meshletBuffer->flushPendingTransfers();
        }

        // Always reset all batch draw counts (clears stale data when no objects)
        batchManager->resetAllBatches(cmd);

        // Reset meshlet culling stats (must be outside render pass)
        if (meshShaderPipeline)
        {
            meshShaderPipeline->resetStats(cmd);
        }

        if (stats.totalObjects == 0)
        {
            return;
        }
        mergedBuffer->uploadObjects(cmd);

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

        // Use Task+Mesh shader pipeline with indirect count dispatch
        // No vertex/index buffer binding needed - mesh shader fetches from SSBOs

        vk::Pipeline activePipeline = meshShaderPipeline->getPipeline();
        vk::PipelineLayout layout = meshShaderPipeline->getPipelineLayout();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);

        // Bind all 5 descriptor sets:
        // Set 0: IBL (camera UBO + IBL textures)
        // Set 1: Per-draw data
        // Set 2: Bindless textures
        // Set 3: Meshlet data (meshlet buffer, vertex indices, primitive indices)
        // Set 4: Vertex data (merged vertex buffer)
        std::array<vk::DescriptorSet, 5> descriptorSets = {
            iblDescriptorSet,
            meshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            meshShaderPipeline->getMeshletDataDescriptorSet(),
            meshShaderPipeline->getVertexDataDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        // Get screen dimensions for push constants
        auto extent = swapChain.getSwapchainExtent();

        // Render shader groups: 0 (default), 1 (UV animation), 2 (emission animation)
        for (uint32_t shaderGroup = 0; shaderGroup <= 2; ++shaderGroup)
        {
            // Draw all batches for this shader group using mesh shader dispatch
            for (uint32_t batch = 0; batch < batchCount; ++batch)
            {
                vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, shaderGroup);
                vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, shaderGroup);

                // Set push constant with base draw index for this section
                // The task shader uses: drawIndex = pc.baseDrawIndex + gl_DrawID
                MeshShaderPushConstants pushConstants{};
                pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, shaderGroup) * commandsPerSection;
                // Pack viewMode and culling flags: bits 0-7 = viewMode, bit 8 = frustum, bit 9 = backface
                pushConstants.viewMode = currentViewMode;
                if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
                if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
                pushConstants.screenWidth = static_cast<float>(extent.width);
                pushConstants.screenHeight = static_cast<float>(extent.height);

                cmd.pushConstants(
                    layout,
                    vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment,
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
        if (!initialized || !bindlessTextures || !materialTextureCache) {
            return false;
        }
        
        if (registeredMaterialPaths.contains(materialPath)) {
            return true;
        }

        mesh::ExtractedPBRValues pbrValues;

        // Handle material instances
        if (material::isInstanceFile(materialPath)) {
            auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
            if (!instanceData || instanceData->parentMaterialPath.empty()) {
                loggerWarning("GPUDrivenRenderer: Failed to load material instance: {}", materialPath);
                return false;
            }

            // Load and cache parent material
            auto parentMatData = resource::ResourceManager::loadMaterial(instanceData->parentMaterialPath);
            if (!parentMatData) {
                loggerWarning("GPUDrivenRenderer: Failed to load parent material: {}", instanceData->parentMaterialPath);
                return false;
            }
            loadedMaterials[instanceData->parentMaterialPath] = parentMatData;
            
            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromInstance(*instanceData, *parentMatData);
        }
        else {
            // Regular material
            auto matData = resource::ResourceManager::loadMaterial(materialPath);
            if (!matData) {
                loggerWarning("GPUDrivenRenderer: Failed to load material: {}", materialPath);
                return false;
            }
            // Keep material alive by storing in our cache
            loadedMaterials[materialPath] = matData;
            
            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData);
        }

        bool registered = false;

        // Helper lambda to register a texture
        auto tryRegister = [&](const std::string& texPath) {
            if (texPath.empty()) return;

            // Load texture via MaterialTextureCache
            if (!materialTextureCache->loadTexture(texPath)) {
                return;
            }

            // Get view and sampler
            vk::ImageView view = materialTextureCache->getViewForPath(texPath);
            vk::Sampler sampler = materialTextureCache->getSamplerForPath(texPath);

            if (view && sampler) {
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

        // Only mark as registered if at least one texture was successfully registered
        if (registered) {
            registeredMaterialPaths.insert(materialPath);
        }

        return registered;
    }

    void GPUDrivenRenderer::updateHiZPyramid(vk::ImageView hiZView, vk::Sampler hiZSampler, uint32_t mipLevels)
    {
        if (!initialized) {
            return;
        }

        bool wasAvailable = (hiZMipLevels > 0);

        cachedHiZView = hiZView;
        cachedHiZSampler = hiZSampler;
        hiZMipLevels = mipLevels;

        // Update the cull pipeline descriptor with Hi-Z texture
        if (cullPipeline && hiZView && hiZSampler) {
            cullPipeline->updateHiZDescriptor(hiZView, hiZSampler);

            // Auto-enable occlusion culling when Hi-Z first becomes available
            // DISABLED for debugging - uncomment to re-enable
            // if (!wasAvailable && mipLevels > 0 && !occlusionCullingEnabled) {
            //     occlusionCullingEnabled = true;
            //     loggerInfo("GPUDrivenRenderer: Hi-Z occlusion culling enabled ({} mip levels)", mipLevels);
            // }
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
        if (!initialized || !enabled || !batchManager) {
            return;
        }

        // Read back and aggregate stats from all batches (expensive - causes sync)
        GPUDrivenStats aggregated = batchManager->readBackAggregatedStats();

        stats.visibleObjects = aggregated.visibleObjects;
        stats.drawCalls = aggregated.drawCalls;  // One draw call per batch

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
        if (!meshShaderPipeline) {
            return MeshletCullingStats{};
        }
        return meshShaderPipeline->readStats();
    }

    void GPUDrivenRenderer::updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout)
    {
        if (!initialized) return;

        // Check if anything actually changed
        bool renderPassChanged = (cachedRenderPass != newRenderPass);
        bool iblLayoutChanged = (newIBLLayout && cachedIBLLayout != newIBLLayout);

        if (!renderPassChanged && !iblLayoutChanged) return;

        loggerInfo("GPUDrivenRenderer: Updating render pass/IBL layout, recreating pipelines");

        // Update cached values
        cachedRenderPass = newRenderPass;
        if (newIBLLayout) {
            cachedIBLLayout = newIBLLayout;
        }

        // Recreate mesh shader pipeline with new render pass and IBL layout
        if (meshShaderPipeline)
        {
            meshShaderPipeline->recreate(cachedIBLLayout, bindlessTextures->getDescriptorSetLayout(), cachedRenderPass);
        }
    }

}
