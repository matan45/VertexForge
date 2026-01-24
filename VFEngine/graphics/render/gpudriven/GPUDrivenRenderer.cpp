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

            boneMatrixManager = std::make_unique<BoneMatrixManager>(device);
            boneMatrixManager->init();

            lightBufferManager = std::make_unique<lighting::GPULightBufferManager>(device);
            lightBufferManager->init();

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

        if (meshShaderPipeline) meshShaderPipeline->cleanup();
        if (lightBufferManager) lightBufferManager->cleanup();
        if (boneMatrixManager) boneMatrixManager->cleanup();
        if (meshletBuffer) meshletBuffer->cleanup();
        if (cameraBuffer) cameraBuffer->cleanup();
        if (cullPipeline) cullPipeline->cleanup();
        if (bindlessTextures) bindlessTextures->cleanup();
        if (batchManager) batchManager->cleanup();
        if (mergedBuffer) mergedBuffer->cleanup();

        meshStreamManager.reset();
        meshShaderPipeline.reset();
        lightBufferManager.reset();
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

        ShaderGroupResolver shaderGroupResolver = [](const std::string& /*materialPath*/) -> uint32_t
        {
            return 0;
        };

        BoneOffsetResolver boneOffsetResolver = nullptr;
        if (boneMatrixManager)
        {
            auto& animatorSystem = animation::RuntimeAnimatorSystem::instance();
            auto& registry = scene::EntityRegistry::getRegistry();

            auto view = registry.view<components::MeshComponent>();
            for (auto entity : view)
            {
                const auto& meshComp = view.get<components::MeshComponent>(entity);

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

            boneOffsetResolver = [this](entt::entity entity) -> uint32_t
            {
                return boneMatrixManager->getBoneOffset(entity);
            };
        }

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
            .batchManager = batchManager.get()
        };
        cameraBuffer->update(cameraParams);

        cullPipeline->updateDescriptors(
            mergedBuffer->getObjectBuffer(),
            cameraBuffer->getBuffer(),
            batchManager->getCombinedDrawCommandBuffer(),
            batchManager->getCombinedPerDrawDataBuffer(),
            batchManager->getCombinedDrawCountBuffer()
        );

        if (meshShaderPipeline && mergedBuffer->getObjectCount() > 0)
        {
            meshShaderPipeline->updatePerDrawDescriptor(batchManager->getCombinedPerDrawDataBuffer());
            meshShaderPipeline->updateMeshletDescriptors(*meshletBuffer);
            meshShaderPipeline->updateVertexDescriptors(*mergedBuffer);
        }

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

        if (boneMatrixManager)
        {
            boneMatrixManager->uploadToGPU(cmd);
        }

        if (lightBufferManager)
        {
            lightBufferManager->updateFromScene();
            lightBufferManager->uploadToGPU(cmd);
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
