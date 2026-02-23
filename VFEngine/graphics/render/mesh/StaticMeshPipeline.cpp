#include "StaticMeshPipeline.hpp"
#include "MeshGPUCache.hpp"
#include "MaterialCacheManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialShaderCache.hpp"
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

        materialCacheManager = std::make_unique<MaterialCacheManager>();
        materialCacheManager->setShaderCache(materialShaderCache.get());
        materialCacheManager->setTextureCache(textureCache.get());
    }

    StaticMeshPipeline::~StaticMeshPipeline()
    {
        if (materialChangeCallbackId) {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
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
        createRenderPass();
        createVFXRenderPass();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout();
        createDescriptorPool();
        createTextureDescriptorPool();
        createCameraUBO();
        createDescriptorSet(irradianceMap, prefilterMap, brdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(renderPass, pipelineLayout, swapChain.getSwapchainExtent());
        initializeDefaultTextureDescriptors();
        createFramebuffers();
        registerMaterialChangeCallback();
    }

    void StaticMeshPipeline::initWithDefaults()
    {
        loadShaders();
        createRenderPass();
        createVFXRenderPass();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout();
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
        materialShaderCache->init(renderPass, pipelineLayout, swapChain.getSwapchainExtent());
        initializeDefaultTextureDescriptors();
        createFramebuffers();
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
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        device.getLogicalDevice().destroyRenderPass(renderPass);
        if (vfxRenderPass)
            device.getLogicalDevice().destroyRenderPass(vfxRenderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);

        createRenderPass();
        createVFXRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
    }

    void StaticMeshPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos, float time) const
    {
        currentTime = time;

        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        math::extractFrustumPlanes(projection * view, ubo.frustumPlanes);

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(cameraUBOMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(cameraUBOMemory);
        }
    }

    void StaticMeshPipeline::cleanUpForReinit()
    {
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        if (cameraUBO)
        {
            device.getLogicalDevice().destroyBuffer(cameraUBO);
            device.getLogicalDevice().freeMemory(cameraUBOMemory);
            cameraUBO = nullptr;
            cameraUBOMemory = nullptr;
        }

        if (renderPass)
            device.getLogicalDevice().destroyRenderPass(renderPass);
        if (vfxRenderPass)
            device.getLogicalDevice().destroyRenderPass(vfxRenderPass);
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
            if (textureDescriptorSet)
                device.getLogicalDevice().freeDescriptorSets(textureDescriptorPool, textureDescriptorSet);
            device.getLogicalDevice().destroyDescriptorPool(textureDescriptorPool);
            textureDescriptorPool = nullptr;
            textureDescriptorSet = nullptr;
        }
        if (textureDescriptorSetLayout)
        {
            device.getLogicalDevice().destroyDescriptorSetLayout(textureDescriptorSetLayout);
            textureDescriptorSetLayout = nullptr;
        }
        textureDescriptorsInitialized = false;

        renderPass = nullptr;
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

    void StaticMeshPipeline::cleanUp()
    {
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
