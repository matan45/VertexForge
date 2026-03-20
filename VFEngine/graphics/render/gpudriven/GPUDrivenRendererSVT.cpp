#include "GPUDrivenRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/BufferUtilities.hpp"
#include "resource/ResourceManager.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "print/Log.hpp"
#include <cstring>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven
{
    void GPUDrivenRenderer::initSVT(const svt::SVTConfig& config)
    {
        if (svt.initialized) return;
        svt.config = config;

        // Create physical tile cache
        svt.tileCache = std::make_unique<svt::PhysicalTileCache>(device);
        svt.tileCache->init(config);

        // Create page table
        svt.pageTable = std::make_unique<svt::SVTPageTable>(device);
        svt.pageTable->init(config);

        // Create SVT params UBO
        auto dev = device.getLogicalDevice();
        auto physDev = device.getPhysicalDevice();
        core::BufferInfoRequest bufReq(dev, physDev,
            sizeof(svt::SVTParamsGPU),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(bufReq, svt.paramsBuffer, svt.paramsMemory);
        svt.paramsMapped = dev.mapMemory(svt.paramsMemory, 0, sizeof(svt::SVTParamsGPU));
        std::memset(svt.paramsMapped, 0, sizeof(svt::SVTParamsGPU));

        // Create stream manager
        svt.streamManager = std::make_unique<svt::SVTStreamManager>(*svt.tileCache, *svt.pageTable);
        svt.streamManager->init(config);

        // Create terrain compositor
        svt.compositor = std::make_unique<svt::TerrainSVTCompositor>();
        svt.streamManager->setTileProvider(svt.compositor.get());

        // Create feedback pipeline (deferred until we have a depth image)
        svt.feedbackPipeline = std::make_unique<svt::SVTFeedbackPipeline>(device);

        // Wire SVT to terrain pipeline and recompile shaders with SVT_ENABLED
        if (terrain.pipeline)
        {
            TerrainMeshShaderPipeline::SVTCacheViews caches[5] = {
                {svt.tileCache->getAlbedoView(),   svt.tileCache->getAlbedoSampler()},
                {svt.tileCache->getNormalView(),    svt.tileCache->getNormalSampler()},
                {svt.tileCache->getORMView(),       svt.tileCache->getORMSampler()},
                {svt.tileCache->getEmissionView(),  svt.tileCache->getEmissionSampler()},
                {svt.tileCache->getHeightView(),    svt.tileCache->getHeightSampler()}
            };
            terrain.pipeline->initSVTDescriptorSet(
                svt.pageTable->getBuffer(), svt.paramsBuffer, caches);

            // Recreate pipeline to recompile shaders with SVT_ENABLED macro
            terrain.pipeline->recreate(
                cachedIBLLayout,
                bindlessTextures->getDescriptorSetLayout(),
                meshShaderPipeline->getMeshletDataLayout(),
                meshShaderPipeline->getVertexDataLayout(),
                lightBufferManager->getDescriptorSetLayout(),
                clusterGridManager->getDescriptorSetLayout(),
                lightCullingPipeline->getDescriptorSetLayout(),
                shadowSystem->getShadowDataLayout(),
                shadowSystem->getShadowTextureLayout(),
                cachedRenderPass);
        }

        // Wire SVT to scene mesh pipelines and recompile with SVT_ENABLED
        auto wireSVTToMeshPipeline = [&](MeshShaderPipeline* pipeline, const char* name)
        {
            if (!pipeline) return;
            pipeline->updateSVTDescriptor(terrain.pipeline->getSVTDescriptorSet());

            MeshPipelineInitInfo info;
            info.iblLayout = cachedIBLLayout;
            info.bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout();
            info.boneMatrixLayout = boneMatrixManager ? boneMatrixManager->getDescriptorSetLayout()
                                                       : meshShaderPipeline->getVertexDataLayout();
            info.lightDataLayout = lightBufferManager->getDescriptorSetLayout();
            info.clusterGridLayout = clusterGridManager->getDescriptorSetLayout();
            info.cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout();
            info.shadowDataLayout = shadowSystem->getShadowDataLayout();
            info.shadowTextureLayout = shadowSystem->getShadowTextureLayout();
            info.giProbeDataLayout = giCascadeManager ? giCascadeManager->getProbeDataLayout()
                                                       : vk::DescriptorSetLayout{};
            info.svtLayout = terrain.pipeline->getSVTLayout();
            info.renderPass = cachedRenderPass;
            pipeline->recreate(info);
            vfLogInfo("SVT: Recreated {} pipeline with SVT_ENABLED", name);
        };

        wireSVTToMeshPipeline(meshShaderPipeline.get(), "opaque");
        wireSVTToMeshPipeline(transparentMeshShaderPipeline.get(), "transparent");

        svt.initialized = true;
        svt.enabled = true;
        vfLogInfo("SVT subsystem initialized: {} physical tiles, {} page table entries",
                  config.physicalTileCount,
                  svt::computeTotalPageTableEntries(config.virtualTextureSizeLog2, config.tileSizeLog2));
    }

    void GPUDrivenRenderer::cleanupSVT()
    {
        if (!svt.initialized) return;

        auto dev = device.getLogicalDevice();
        dev.waitIdle();

        svt.feedbackPipeline.reset();
        svt.streamManager.reset();
        svt.compositor.reset();
        svt.pageTable.reset();
        svt.tileCache.reset();

        if (svt.paramsMapped)
        {
            dev.unmapMemory(svt.paramsMemory);
            svt.paramsMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(dev, svt.paramsBuffer, svt.paramsMemory);

        if (terrain.pipeline)
        {
            terrain.pipeline->setSVTEnabled(false);
        }

        // Clear SVT descriptor from scene pipelines
        if (meshShaderPipeline) meshShaderPipeline->updateSVTDescriptor(nullptr);
        if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->updateSVTDescriptor(nullptr);

        svt.initialized = false;
        svt.enabled = false;
    }

    void GPUDrivenRenderer::setSVTEnabled(bool enabled)
    {
        svt.enabled = enabled && svt.initialized;
        if (terrain.pipeline)
        {
            terrain.pipeline->setSVTEnabled(svt.enabled);
        }
    }

    const svt::SVTStreamStats* GPUDrivenRenderer::getSVTStreamStats() const
    {
        if (!svt.initialized || !svt.streamManager) return nullptr;
        return &svt.streamManager->getStats();
    }

    void GPUDrivenRenderer::dispatchSVTFeedback(vk::CommandBuffer cmd)
    {
        if (!svt.initialized || !svt.enabled || !svt.feedbackPipeline) return;
        if (!svt.feedbackPipeline->isInitialized()) return;

        uint64_t frameIndex = cachedCamera.time > 0 ? static_cast<uint64_t>(cachedCamera.time * 60.0f) : 0;

        // Clear feedback buffer for this frame
        svt.feedbackPipeline->clearFeedbackBuffer(cmd, frameIndex);

        // Memory barrier: clear → compute write
        vk::MemoryBarrier memBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderWrite
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eComputeShader,
                            vk::DependencyFlags{}, 1, &memBarrier, 0, nullptr, 0, nullptr);

        glm::vec2 scale = svt.compositor ? svt.compositor->getSVTScale() : glm::vec2(1.0f);
        glm::vec2 offset = svt.compositor ? svt.compositor->getSVTOffset() : glm::vec2(0.0f);

        auto extent = swapChain.getSwapchainExtent();
        glm::vec4 screenParams(
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            1.0f / extent.width,
            1.0f / extent.height);

        glm::mat4 invVP = glm::inverse(cachedCamera.projection * cachedCamera.view);

        svt.feedbackPipeline->dispatch(cmd, frameIndex, invVP, screenParams,
                                        cachedCamera.position, scale, offset);
    }

    void GPUDrivenRenderer::updateSVTStreaming()
    {
        if (!svt.initialized || !svt.enabled || !svt.feedbackPipeline) return;
        if (!svt.feedbackPipeline->isInitialized()) return;

        uint64_t frameIndex = cachedCamera.time > 0 ? static_cast<uint64_t>(cachedCamera.time * 60.0f) : 0;

        // Read back feedback from N frames ago
        const uint32_t* feedback = svt.feedbackPipeline->readFeedback(frameIndex);
        if (!feedback) return;

        // Update streaming: process feedback, upload tiles, evict
        svt.streamManager->update(feedback, svt.feedbackPipeline->getTotalEntries(),
                                   frameIndex, cachedCamera.position);
    }

    void GPUDrivenRenderer::updateSVTParams(const glm::vec2& terrainWorldMin,
                                             const glm::vec2& terrainWorldMax)
    {
        if (!svt.initialized || !svt.paramsMapped) return;

        // Initialize compositor with terrain bounds
        if (svt.compositor)
        {
            svt.compositor->init(svt.config, terrainWorldMin, terrainWorldMax);

            // Load terrain layer textures for CPU-side compositing
            // These are separate from the GPU textures — we keep CPU pixel data alive
            if (!terrain.currentMaterialPath.empty())
            {
                auto materialData = resource::ResourceManager::loadTerrainMaterial(
                    asset::AssetRef::fromPath(terrain.currentMaterialPath));
                if (materialData)
                {
                    std::vector<svt::TerrainLayerSource> layerSources;
                    layerSources.resize(materialData->activeLayerCount);

                    for (uint8_t i = 0; i < materialData->activeLayerCount; ++i)
                    {
                        const auto& layer = materialData->layers[i];
                        auto& src = layerSources[i];

                        auto loadTex = [](const std::string& path) -> std::shared_ptr<resource::TextureData>
                        {
                            if (path.empty()) return nullptr;
                            try
                            {
                                return resource::ResourceManager::loadTextureAsync(
                                    asset::AssetRef::fromPath(path)).get();
                            }
                            catch (...)
                            {
                                return nullptr;
                            }
                        };

                        src.albedoTexture = loadTex(layer.albedoTextureRef.resolve());
                        src.normalTexture = loadTex(layer.normalTextureRef.resolve());
                        src.ormTexture = loadTex(layer.ormTextureRef.resolve());
                        src.tilingScale = layer.tilingScale;
                        src.roughness = layer.roughness;
                        src.metallic = layer.metallic;
                        src.ao = layer.ao;
                    }

                    svt.compositor->setLayers(std::move(layerSources));
                    vfLogInfo("SVT: Loaded {} terrain layer textures for compositing",
                              materialData->activeLayerCount);
                }
            }
        }

        // Update SVT params UBO
        svt::SVTParamsGPU params{};
        glm::vec2 scale = svt.compositor ? svt.compositor->getSVTScale() : glm::vec2(1.0f);
        glm::vec2 offset = svt.compositor ? svt.compositor->getSVTOffset() : glm::vec2(0.0f);

        params.svtScaleOffset = glm::vec4(scale, offset);
        params.svtInfo = glm::uvec4(
            svt.config.virtualTextureSizeLog2,
            svt.config.tileSizeLog2,
            svt.config.physicalTileCount,
            svt::computeMipLevelCount(svt.config.virtualTextureSizeLog2, svt.config.tileSizeLog2));
        params.svtCacheIndices0 = glm::uvec4(
            svt.tileCache->getAlbedoBindlessIndex(),
            svt.tileCache->getNormalBindlessIndex(),
            svt.tileCache->getORMBindlessIndex(),
            svt.tileCache->getEmissionBindlessIndex());
        params.svtCacheIndices1 = glm::uvec4(
            svt.tileCache->getHeightBindlessIndex(), 0, 0, 0);
        params.svtTileInfo = glm::uvec4(
            svt::SVT_BORDER_SIZE,
            svt::SVT_PHYSICAL_TILE_SIZE,
            svt.pageTable->getTotalEntries(),
            svt.enabled ? 1 : 0);

        std::memcpy(svt.paramsMapped, &params, sizeof(params));
    }
}
