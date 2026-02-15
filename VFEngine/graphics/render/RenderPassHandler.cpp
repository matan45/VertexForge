#include "RenderPassHandler.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "DebugRenderer.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "billboard/BillboardTypes.hpp"
#include "text/TextPipeline.hpp"
#include "text/TextTypes.hpp"
#include "ui/UIRenderPipeline.hpp"
#include "ui/UIRenderTypes.hpp"
#include "ui/UITextPipeline.hpp"
#include "ui/UITextRenderTypes.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/TerrainRaycastPipeline.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "material/MaterialTextureCache.hpp"
#include "../../services/providers/IVFXRuntimeProvider.hpp"
#include "../../services/providers/ITerrainRenderProvider.hpp"
#include "terrain/TerrainTile.hpp"
#include "resource/ResourceManager.hpp"
#include "material/MaterialTypes.hpp"
#include <queue>
#include <unordered_set>
#include <optional>

namespace
{
    std::optional<uint32_t> findTimeNodeId(const material::ShaderGraph& graph)
    {
        for (const auto& node : graph.nodes)
        {
            if (node.type == material::NodeType::Time)
            {
                return node.id;
            }
        }
        return std::nullopt;
    }

    bool timeNodeReachesPBROutput(const material::ShaderGraph& graph, uint32_t startNodeId)
    {
        std::unordered_set<uint32_t> visited;
        std::queue<uint32_t> toVisit;
        toVisit.push(startNodeId);

        while (!toVisit.empty())
        {
            uint32_t currentId = toVisit.front();
            toVisit.pop();

            if (visited.contains(currentId))
            {
                continue;
            }
            visited.insert(currentId);

            for (const auto& link : graph.links)
            {
                if (link.sourceNodeId != currentId)
                {
                    continue;
                }

                for (const auto& node : graph.nodes)
                {
                    if (node.id != link.targetNodeId)
                    {
                        continue;
                    }

                    if (node.type == material::NodeType::PBROutput)
                    {
                        return true;
                    }

                    if (node.type == material::NodeType::TextureSample && link.targetPin == "UV")
                    {
                        return true;
                    }

                    toVisit.push(link.targetNodeId);
                    break;
                }
            }
        }

        return false;
    }
}

namespace render
{
    RenderPassHandler::RenderPassHandler(core::Device& device, core::SwapChain& swapChain,
                                         core::OffscreenResources& offscreenResources) : device{device},
        swapChain{swapChain}, offscreenResources{offscreenResources}
        , clearColor{std::make_unique<ClearColor>(device, swapChain, offscreenResources)}
        , iblRenderer{std::make_unique<IBL>(device, swapChain, offscreenResources)}
        , meshPipeline{std::make_unique<mesh::StaticMeshPipeline>(device, swapChain, offscreenResources)}
        , billboardPipeline{std::make_unique<billboard::BillboardPipeline>(device, swapChain, offscreenResources)}
        , textPipeline{std::make_unique<text::TextPipeline>(device, swapChain, offscreenResources)}
        , uiPipeline{std::make_unique<ui::UIRenderPipeline>(device, swapChain, offscreenResources)}
        , uiTextPipeline{std::make_unique<ui::UITextPipeline>(device, swapChain, offscreenResources, textPipeline->getFontCache())}
        , cameraOcclusionManager{std::make_unique<occlusion::CameraOcclusionManager>(device, swapChain)}
        , debugRenderer{std::make_unique<DebugRenderer>(device, swapChain)}
        , gpuDrivenRenderer{std::make_unique<gpudriven::GPUDrivenRenderer>(device, swapChain)}
        , terrainRaycastPipeline{std::make_unique<gpudriven::TerrainRaycastPipeline>(device)}
        , postProcessPipeline{std::make_unique<postprocess::PostProcessPipeline>(device, swapChain, offscreenResources)}
    {
    }

    RenderPassHandler::~RenderPassHandler()
    {
        if (materialChangeCallbackId)
        {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
        }
    }

    void RenderPassHandler::init()
    {
        clearColor->init();

        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->init();
            terrainRaycastPipeline->updateDepthImageView(offscreenResources.depthImage.depthImageView);
        }

        if (!materialChangeCallbackId)
        {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath)
                {
                    customShaderRequirementCache.erase(materialPath);

                    if (!material::isInstanceFile(materialPath))
                    {
                        std::erase_if(customShaderRequirementCache, [](const auto& pair)
                        {
                            return material::isInstanceFile(pair.first);
                        });
                    }
                });
        }
    }

    void RenderPassHandler::initMeshPipeline(bool enableGPUDriven)
    {
        if (meshPipelineInitialized)
        {
            return;
        }

        if (iblRenderer->isInitialized())
        {
            const auto& irradiance = iblRenderer->getIrradianceImage();
            const auto& prefilter = iblRenderer->getPrefilterImage();
            const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
            meshPipeline->init(irradiance, prefilter, brdfLUT);
        }
        else
        {
            meshPipeline->initWithDefaults();
        }
        meshPipelineInitialized = true;

        if (enableGPUDriven)
        {
            initGPUDrivenRenderer();
        }

        if (vfxRuntimeProvider && !vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->init(meshPipeline->getRenderPass());
        }
    }

    void RenderPassHandler::initGPUDrivenRenderer()
    {
        if (gpuDrivenRendererInitialized)
        {
            return;
        }

        if (!meshPipelineInitialized)
        {
            return;
        }

        vk::DescriptorSetLayout iblLayout = meshPipeline->getIBLDescriptorSetLayout();
        vk::RenderPass renderPass = meshPipeline->getRenderPass();

        gpuDrivenRenderer->init(iblLayout, renderPass);

        auto& texCache = meshPipeline->getMaterialTextureCache();
        gpuDrivenRenderer->setMaterialTextureCache(&texCache);

        if (texCache.hasDefaultTexture())
        {
            gpuDrivenRenderer->setDefaultTexture(texCache.getDefaultView(), texCache.getDefaultSampler());
        }

        gpuDrivenRenderer->setEnabled(true);
        gpuDrivenRendererInitialized = true;
    }

    void RenderPassHandler::reinitMeshPipelineWithDefaults()
    {
        if (!meshPipelineInitialized)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        meshPipeline->cleanUpForReinit();

        meshPipeline->initWithDefaults();

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->updateRenderPass(
                meshPipeline->getRenderPass(),
                meshPipeline->getIBLDescriptorSetLayout());
        }

        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
        }
    }

    void RenderPassHandler::reinitMeshPipelineWithIBL()
    {
        if (!meshPipelineInitialized)
        {
            return;
        }

        if (!iblRenderer->isInitialized())
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        meshPipeline->cleanUpForReinit();

        const auto& irradiance = iblRenderer->getIrradianceImage();
        const auto& prefilter = iblRenderer->getPrefilterImage();
        const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
        meshPipeline->init(irradiance, prefilter, brdfLUT);

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->updateRenderPass(
                meshPipeline->getRenderPass(),
                meshPipeline->getIBLDescriptorSetLayout());
        }

        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
        }
    }

    std::unordered_set<std::string> RenderPassHandler::collectCustomShaderMaterials(
        const std::vector<mesh::MeshRenderData>& meshes) const
    {
        std::unordered_set<std::string> uniqueMaterials;
        for (const auto& mesh : meshes)
        {
            if (!mesh.defaultMaterialPath.empty())
            {
                uniqueMaterials.insert(mesh.defaultMaterialPath);
            }
            for (const auto& [submeshName, matInfo] : mesh.submeshMaterials)
            {
                if (!matInfo.materialPath.empty())
                {
                    uniqueMaterials.insert(matInfo.materialPath);
                }
            }
        }

        std::unordered_set<std::string> customShaderMaterials;
        for (const auto& matPath : uniqueMaterials)
        {
            if (materialRequiresCustomShader(matPath))
            {
                customShaderMaterials.insert(matPath);
            }
        }
        return customShaderMaterials;
    }

    void RenderPassHandler::updateDebugBoundingBoxState()
    {
        if (!debugRendererInitialized || !debugRenderer)
        {
            return;
        }

        bool hasBoundingBoxes = false;
        for (const auto& mesh : currentMeshDrawList)
        {
            if (mesh.showBoundingBox) { hasBoundingBoxes = true; break; }
        }
        if (!hasBoundingBoxes)
        {
            for (const auto& mesh : customShaderMeshDrawList)
            {
                if (mesh.showBoundingBox) { hasBoundingBoxes = true; break; }
            }
        }
        debugRenderer->setHasBoundingBoxes(hasBoundingBoxes);
    }

    void RenderPassHandler::rebuildCombinedMeshDrawList()
    {
        combinedMeshDrawList.clear();
        combinedMeshDrawList.reserve(currentMeshDrawList.size() + customShaderMeshDrawList.size());
        combinedMeshDrawList.insert(combinedMeshDrawList.end(),
                                    currentMeshDrawList.begin(), currentMeshDrawList.end());
        combinedMeshDrawList.insert(combinedMeshDrawList.end(),
                                    customShaderMeshDrawList.begin(), customShaderMeshDrawList.end());
    }

    void RenderPassHandler::setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes)
    {
        currentMeshDrawList.clear();
        customShaderMeshDrawList.clear();

        auto customShaderMaterials = collectCustomShaderMaterials(meshes);

        for (auto& mesh : meshes)
        {
            bool needsCustomShader = false;

            if (!mesh.defaultMaterialPath.empty()
                && customShaderMaterials.contains(mesh.defaultMaterialPath))
            {
                needsCustomShader = true;
            }

            if (!needsCustomShader)
            {
                for (const auto& [submeshName, matInfo] : mesh.submeshMaterials)
                {
                    if (!matInfo.materialPath.empty()
                        && customShaderMaterials.contains(matInfo.materialPath))
                    {
                        needsCustomShader = true;
                        break;
                    }
                }
            }

            if (needsCustomShader)
            {
                customShaderMeshDrawList.push_back(std::move(mesh));
            }
            else
            {
                currentMeshDrawList.push_back(std::move(mesh));
            }
        }

        updateDebugBoundingBoxState();
        rebuildCombinedMeshDrawList();
    }

    void RenderPassHandler::initBillboardPipeline()
    {
        if (billboardPipelineInitialized)
        {
            return;
        }

        billboardPipeline->init();
        billboardPipelineInitialized = true;
    }

    void RenderPassHandler::setBillboardDrawList(std::vector<billboard::BillboardRenderData>&& billboards)
    {
        currentBillboardDrawList = std::move(billboards);
        if (billboardPipelineInitialized && billboardPipeline)
        {
            billboardPipeline->setBillboardList(currentBillboardDrawList);
        }
    }

    void RenderPassHandler::initTextPipeline()
    {
        if (textPipelineInitialized)
        {
            return;
        }

        textPipeline->init();
        textPipelineInitialized = true;
    }

    void RenderPassHandler::setTextDrawList(std::vector<text::TextRenderData>&& textEntities)
    {
        currentTextDrawList = std::move(textEntities);
        if (textPipelineInitialized && textPipeline)
        {
            textPipeline->setTextDrawList(currentTextDrawList);
        }
    }

    void RenderPassHandler::appendTextDrawList(std::vector<text::TextRenderData>&& textEntities)
    {
        currentTextDrawList.insert(currentTextDrawList.end(),
                                   std::make_move_iterator(textEntities.begin()),
                                   std::make_move_iterator(textEntities.end()));
        if (textPipelineInitialized && textPipeline)
        {
            textPipeline->setTextDrawList(currentTextDrawList);
        }
    }

    void RenderPassHandler::initUIRenderPipeline()
    {
        if (uiPipelineInitialized)
        {
            return;
        }

        uiPipeline->init();
        uiPipelineInitialized = true;
    }

    void RenderPassHandler::setUIImageDrawList(std::vector<ui::UIImageRenderData>&& images)
    {
        currentUIImageDrawList = std::move(images);
        if (uiPipelineInitialized && uiPipeline)
        {
            uiPipeline->setUIImageDrawList(currentUIImageDrawList);
        }
    }

    void RenderPassHandler::initUITextPipeline()
    {
        if (uiTextPipelineInitialized)
        {
            return;
        }

        initTextPipeline();

        uiTextPipeline->init();
        uiTextPipelineInitialized = true;
    }

    void RenderPassHandler::setUITextDrawList(std::vector<ui::UITextRenderData>&& labels)
    {
        currentUITextDrawList = std::move(labels);
        if (uiTextPipelineInitialized && uiTextPipeline)
        {
            uiTextPipeline->setUITextDrawList(currentUITextDrawList);
        }
    }

    void RenderPassHandler::initDebugRenderer()
    {
        if (debugRendererInitialized)
        {
            return;
        }

        if (!meshPipelineInitialized)
        {
            return;
        }

        debugRenderer->init(meshPipeline->getRenderPass());
        debugRendererInitialized = true;
    }

    void RenderPassHandler::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->setDeletionQueue(queue);
        }

        if (textPipeline)
        {
            textPipeline->setDeletionQueue(queue);
        }
        if (uiPipeline)
        {
            uiPipeline->setDeletionQueue(queue);
        }
        if (uiTextPipeline)
        {
            uiTextPipeline->setDeletionQueue(queue);
        }
        if (billboardPipeline)
        {
            billboardPipeline->setDeletionQueue(queue);
        }
    }

    void RenderPassHandler::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
    {
        vfxRuntimeProvider = provider;
    }

    void RenderPassHandler::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
    {
        terrainRenderProvider = provider;

        if (provider && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTileDataLoader(
                [provider](terrain::TerrainTile& tile, uint8_t lod) -> bool {
                    return provider->ensureTileLODData(tile, lod);
                });
            gpuDrivenRenderer->setTileRAMEvictor(
                [provider](terrain::TerrainTile& tile) {
                    provider->releaseTileRAMData(tile);
                });
        }
    }

    void RenderPassHandler::clearTerrainData()
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->clearTerrainData();
        }
    }

    void RenderPassHandler::recreateOverlayPipelines()
    {
        if (debugRendererInitialized)
        {
            debugRenderer->recreate(meshPipeline->getRenderPass());
        }

        if (billboardPipelineInitialized)
        {
            billboardPipeline->recreate();
        }

        if (textPipelineInitialized)
        {
            textPipeline->recreate();
        }

        if (uiPipelineInitialized)
        {
            uiPipeline->recreate();
        }

        if (uiTextPipelineInitialized)
        {
            uiTextPipeline->recreate();
        }
    }

    void RenderPassHandler::recreate()
    {
        iblRenderer->recreate();
        clearColor->recreate();

        if (meshPipelineInitialized)
        {
            meshPipeline->recreate();

            if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            {
                gpuDrivenRenderer->updateRenderPass(meshPipeline->getRenderPass());
            }

            if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            {
                vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
            }
        }

        recreateOverlayPipelines();

        for (const auto& [cameraId, camera] : cameraOcclusionManager->getAllCameras())
        {
            if (camera->hiZInitialized)
            {
                cameraOcclusionManager->recreateCameraHiZ(
                    cameraId,
                    offscreenResources.depthImage.depthImage,
                    offscreenResources.depthImage.depthImageView,
                    swapChain.getSwapchainDepthStencilFormat());
            }
        }

        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            terrainRaycastPipeline->updateDepthImageView(offscreenResources.depthImage.depthImageView);
        }

        if (postProcessPipeline && postProcessPipeline->isInitialized())
        {
            postProcessPipeline->recreate();
        }
    }

    void RenderPassHandler::cleanUpPipelines() const
    {
        if (billboardPipelineInitialized)
        {
            billboardPipeline->cleanUp();
        }

        if (textPipelineInitialized)
        {
            textPipeline->cleanUp();
        }

        if (uiPipelineInitialized)
        {
            uiPipeline->cleanUp();
        }

        if (uiTextPipelineInitialized)
        {
            uiTextPipeline->cleanUp();
        }

        if (debugRendererInitialized)
        {
            debugRenderer->cleanUp();
            debugRenderer->cleanUpShaders();
        }

        if (meshPipelineInitialized)
        {
            meshPipeline->cleanUpShader();
        }
    }

    void RenderPassHandler::cleanUp() const
    {
        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->cleanup();
        }

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->cleanup();
        }

        if (cameraOcclusionManager)
        {
            cameraOcclusionManager->cleanup();
        }

        cleanUpPipelines();

        if (postProcessPipeline)
        {
            postProcessPipeline->cleanup();
        }

        meshPipeline->cleanUp();
        iblRenderer->cleanUp();
        clearColor->cleanUp();
    }

    bool RenderPassHandler::materialRequiresCustomShader(const std::string& materialPath) const
    {
        if (materialPath.empty())
        {
            return false;
        }

        auto it = customShaderRequirementCache.find(materialPath);
        if (it != customShaderRequirementCache.end())
        {
            return it->second;
        }

        bool result = computeMaterialRequiresCustomShader(materialPath);
        customShaderRequirementCache[materialPath] = result;
        return result;
    }

    bool RenderPassHandler::computeMaterialRequiresCustomShader(const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return false;
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
                return false;
            }
        }

        auto matData = resource::ResourceManager::loadMaterial(parentPath);
        if (!matData)
        {
            return false;
        }

        auto timeNodeId = findTimeNodeId(matData->graph);
        if (!timeNodeId)
        {
            return false;
        }

        return timeNodeReachesPBROutput(matData->graph, *timeNodeId);
    }
}
