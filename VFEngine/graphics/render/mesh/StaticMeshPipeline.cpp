#include "StaticMeshPipeline.hpp"
#include "MeshGPUCache.hpp"
#include "MaterialCacheManager.hpp"
#include "MaterialPipelineWarmup.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialShaderCache.hpp"
#include "../material/MaterialParameterBufferCache.hpp"
#include "../ibl/DefaultIBLTextureFactory.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "material/MaterialManager.hpp"
#include "material/MaterialTypes.hpp"
#include "math/Frustum.hpp"

namespace render::mesh
{
    StaticMeshPipeline::StaticMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                           core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
        meshCache = std::make_unique<MeshGPUCache>(device);

        textureCache = std::make_unique<MaterialTextureCache>(device);
        textureCache->init(device.getStagingCommandPool());

        materialShaderCache = std::make_unique<MaterialShaderCache>(device);
        parameterBufferCache = std::make_unique<MaterialParameterBufferCache>(device);

        materialCacheManager = std::make_unique<MaterialCacheManager>();
        materialCacheManager->setShaderCache(materialShaderCache.get());
        materialCacheManager->setTextureCache(textureCache.get());
        materialCacheManager->setParameterBufferCache(parameterBufferCache.get());

        pipelineWarmup = std::make_unique<MaterialPipelineWarmup>(*materialShaderCache, *materialCacheManager);
    }

    StaticMeshPipeline::~StaticMeshPipeline()
    {
        if (materialChangeCallbackId) {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
        }
    }

    void StaticMeshPipeline::beginPipelineWarmup(std::vector<std::string> extraPaths)
    {
        if (pipelineWarmup)
        {
            pipelineWarmup->begin(std::move(extraPaths));
        }
    }

    services::PipelineWarmupStats StaticMeshPipeline::getPipelineWarmupStats() const
    {
        return pipelineWarmup ? pipelineWarmup->getStats() : services::PipelineWarmupStats{};
    }

    void StaticMeshPipeline::setFrameRecording(bool recording) const
    {
        if (materialShaderCache)
        {
            materialShaderCache->setFrameRecording(recording);
        }
    }

    void StaticMeshPipeline::registerMaterialChangeCallback()
    {
        if (!materialChangeCallbackId) {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath) {
                    if (materialCacheManager) {
                        materialCacheManager->invalidate(materialPath);
                    }
                });
        }
    }

    void StaticMeshPipeline::init(const ibl::ImageData& irradianceMap,
                                  const ibl::ImageData& prefilterMap,
                                  const ibl::ImageData& brdfLUT)
    {
        loadShaders();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout();
        parameterBufferCache->initLayout();
        parameterBufferCache->setImageCount(static_cast<uint32_t>(swapChain.getImageCount()));
        createDescriptorPool();
        createTextureDescriptorPool();
        createCameraUBO();
        createDescriptorSet(irradianceMap, prefilterMap, brdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(pipelineLayout, swapChain.getSwapchainExtent(),
                                  swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
        initializeDefaultTextureDescriptors();
        registerMaterialChangeCallback();
    }

    void StaticMeshPipeline::initWithDefaults()
    {
        loadShaders();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout();
        parameterBufferCache->initLayout();
        parameterBufferCache->setImageCount(static_cast<uint32_t>(swapChain.getImageCount()));
        createDescriptorPool();
        createTextureDescriptorPool();
        createCameraUBO();

        defaultIBLFactory = std::make_unique<ibl::DefaultIBLTextureFactory>(device);
        defaultIBLFactory->createDefaultTextures(device.getStagingCommandPool());

        createDescriptorSet(defaultIBLFactory->getIrradiance(),
                            defaultIBLFactory->getPrefilter(),
                            defaultIBLFactory->getBrdfLUT());
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(pipelineLayout, swapChain.getSwapchainExtent(),
                                  swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
        initializeDefaultTextureDescriptors();
        usingDefaultTextures = true;
        registerMaterialChangeCallback();
    }

    void StaticMeshPipeline::loadShaders()
    {
        meshShader = std::make_shared<core::Shader>(device);
        meshShader->readShader("../../resources/shaders/mesh/mesh.glsl");
    }

    void StaticMeshPipeline::recreate()
    {
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);

        createGraphicsPipeline();
    }

    void StaticMeshPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos, float time) const
    {
        currentTime = time;
        if (externalCameraBuffer) return;

        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        math::extractFrustumPlanes(projection * view, ubo.frustumPlanes);

        void* data = cameraUBOAllocation.mappedPtr;
        if (data)
        {
            memcpy(data, &ubo, sizeof(ubo));
        }
    }

    void StaticMeshPipeline::cleanUpForReinit()
    {
        // VK-1532: drain warm-up jobs before we destroy/recreate the pipeline layout + formats
        // they read. waitIdle() (already called by the reinit callers) only waits on the GPU.
        if (pipelineWarmup)
        {
            pipelineWarmup->stop();
        }

        if (cameraUBO && !externalCameraBuffer)
        {
            device.getLogicalDevice().destroyBuffer(cameraUBO);
            cameraUBO = nullptr;
            device.getMemoryManager().free(cameraUBOAllocation);
            cameraUBOAllocation = {};
        }

        // VK-1577: torn down alongside the descriptor sets that reference it, and recreated by
        // createDescriptorSet() on the next init. Also drop the cached probe handles — the manager
        // re-publishes them after a reinit, and holding stale views here would make the next
        // writeProbeBindings() bind destroyed images.
        if (emptyProbeBuffer)
        {
            device.getLogicalDevice().destroyBuffer(emptyProbeBuffer);
            emptyProbeBuffer = nullptr;
            device.getMemoryManager().free(emptyProbeAllocation);
            emptyProbeAllocation = {};
        }
        cachedProbeBuffer = nullptr;
        cachedProbeCubes = {};

        if (graphicsPipeline)
            device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        if (pipelineLayout)
            device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        if (descriptorPool)
        {
            if (descriptorSet)
                device.getLogicalDevice().freeDescriptorSets(descriptorPool, descriptorSet);
            device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        }
        if (descriptorSetLayout)
            device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);

        if (textureCache)
        {
            textureCache->resetDescriptorResources();
        }

        if (textureDescriptorPool)
        {
            if (!textureDescriptorSets.empty())
                device.getLogicalDevice().freeDescriptorSets(textureDescriptorPool, textureDescriptorSets);
            device.getLogicalDevice().destroyDescriptorPool(textureDescriptorPool);
            textureDescriptorPool = nullptr;
            textureDescriptorSets.clear();
        }
        if (textureDescriptorSetLayout)
        {
            device.getLogicalDevice().destroyDescriptorSetLayout(textureDescriptorSetLayout);
            textureDescriptorSetLayout = nullptr;
        }
        textureDescriptorsInitialized = false;

        if (parameterBufferCache)
        {
            parameterBufferCache->cleanUp();
        }

        graphicsPipeline = nullptr;
        pipelineLayout = nullptr;
        descriptorSet = nullptr;
        descriptorPool = nullptr;
        descriptorSetLayout = nullptr;

        if (usingDefaultTextures && defaultIBLFactory)
        {
            defaultIBLFactory->cleanup();
            defaultIBLFactory.reset();
            usingDefaultTextures = false;
        }
    }

    vk::DescriptorSet StaticMeshPipeline::getTextureDescriptorSet(uint32_t imageIndex) const
    {
        if (textureDescriptorSets.empty())
        {
            return nullptr;
        }
        return textureDescriptorSets[imageIndex % textureDescriptorSets.size()];
    }

    void StaticMeshPipeline::cleanUp()
    {
        // VK-1532: drain warm-up jobs before tearing down the caches they write into.
        if (pipelineWarmup)
        {
            pipelineWarmup->stop();
        }

        if (materialCacheManager)
        {
            materialCacheManager->clear();
        }

        if (materialShaderCache)
        {
            materialShaderCache->cleanUp();
        }

        if (textureCache)
        {
            textureCache->cleanUp();
        }

        unloadAllMeshes();

        cleanUpForReinit();

        textureCache.reset();
        meshCache.reset();
        materialCacheManager.reset();
    }

    void StaticMeshPipeline::cleanUpShader()
    {
        meshShader->cleanUp();
    }

    void StaticMeshPipeline::injectMaterialForPreview(const std::string& materialPath,
                                                      std::shared_ptr<material::MaterialData> materialData)
    {
        if (materialCacheManager)
        {
            materialCacheManager->injectForPreview(materialPath, materialData);
        }
    }

    std::string StaticMeshPipeline::getLastShaderCompilationError() const
    {
        if (materialShaderCache)
        {
            return materialShaderCache->getLastCompilationError();
        }
        return "";
    }

    std::string StaticMeshPipeline::loadMesh(std::string_view meshPath)
    {
        return meshCache->loadMesh(meshPath);
    }

    std::string StaticMeshPipeline::uploadMesh(const std::string& meshId, const resource::MeshesData& meshesData)
    {
        return meshCache->uploadMesh(meshId, meshesData);
    }

    void StaticMeshPipeline::unloadMesh(const std::string& meshId)
    {
        meshCache->unloadMesh(meshId);
    }

    void StaticMeshPipeline::unloadAllMeshes()
    {
        meshCache->unloadAllMeshes();
    }

    const MeshGPUData* StaticMeshPipeline::getMesh(const std::string& meshId) const
    {
        return meshCache->getMesh(meshId);
    }

    bool StaticMeshPipeline::isMeshLoaded(const std::string& meshId) const
    {
        return meshCache->isMeshLoaded(meshId);
    }

    const math::AABB* StaticMeshPipeline::getMeshBoundingBox(const std::string& meshId) const
    {
        return meshCache->getMeshBoundingBox(meshId);
    }

    material::BlendMode StaticMeshPipeline::getMaterialBlendMode(const std::string& materialPath) const
    {
        if (materialPath.empty())
        {
            return material::BlendMode::Opaque;
        }

        auto materialData = materialCacheManager->getMaterial(materialPath);
        if (materialData)
        {
            return materialData->blendMode;
        }

        return material::BlendMode::Opaque;
    }

    std::vector<std::string> StaticMeshPipeline::getLoadedMeshIds() const
    {
        return meshCache->getLoadedMeshIds();
    }
}
