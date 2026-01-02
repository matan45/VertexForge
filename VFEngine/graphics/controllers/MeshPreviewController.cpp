#include "MeshPreviewController.hpp"
#include "../core/VulkanContext.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
#include "../loaders/AsyncMeshLoader.hpp"
#include "resource/Types.hpp"
#include "print/Logger.hpp"

namespace controllers
{
    MeshPreviewController::MeshPreviewController()
        : swapChain{*core::VulkanContext::getSwapChain()}
          , device{*core::VulkanContext::getDevice()}
          , offScreen{std::make_unique<render::OffScreenViewPort>(device, swapChain)}
          , asyncLoader{std::make_unique<loaders::AsyncMeshLoader>()}
    {
    }

    MeshPreviewController::~MeshPreviewController()
    {
        device.getLogicalDevice().waitIdle();
        cleanUp();
    }

    void MeshPreviewController::init()
    {
        if (initialized)
        {
            return;
        }

        offScreen->init();

        // Initialize mesh pipeline with default IBL textures
        // Disable GPU-driven rendering for mesh preview to support submesh highlighting
        auto* renderHandler = offScreen->getRenderPassHandler();
        renderHandler->initMeshPipeline(false);

        initialized = true;
    }

    void MeshPreviewController::cleanUp()
    {
        if (initialized && !loadedMeshPath.empty())
        {
            unloadMesh();
        }

        if (initialized && offScreen)
        {
            offScreen->cleanUp();
        }

        offScreen.reset();
        initialized = false;
    }

    void MeshPreviewController::unloadMesh()
    {
        if (loadedMeshPath.empty())
        {
            return;
        }

        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        if (meshPipeline)
        {
            meshPipeline->unloadMesh(loadedMeshPath);
        }

        loadedMeshPath.clear();
        meshBounds = math::AABB();
        highlightedSubMesh = -1;
    }

    void MeshPreviewController::loadMeshAsync(const std::string& meshPath)
    {
        if (!initialized)
        {
            init();
        }

        if (!pendingMeshPath.empty())
        {
            asyncLoader->cancelLoad(pendingMeshPath);
        }

        if (!loadedMeshPath.empty())
        {
            unloadMesh();
        }

        pendingMeshPath = meshPath;
        asyncLoader->startLoad(meshPath);
    }

    void MeshPreviewController::cancelMeshLoading()
    {
        if (!pendingMeshPath.empty())
        {
            asyncLoader->cancelLoad(pendingMeshPath);
            pendingMeshPath.clear();
        }
    }

    services::MeshLoadingProgress MeshPreviewController::getMeshLoadingProgress() const
    {
        if (pendingMeshPath.empty())
        {
            services::MeshLoadingProgress progress;
            if (!loadedMeshPath.empty())
            {
                progress.state = services::LoadingState::Complete;
                progress.progress = 1.0f;
                progress.statusMessage = "Loaded";
            }
            return progress;
        }

        return asyncLoader->getProgress(pendingMeshPath);
    }

    bool MeshPreviewController::updateAsyncLoading()
    {
        if (pendingMeshPath.empty())
        {
            return false; // No async loading in progress
        }

        // Check for pending GPU work
        if (!asyncLoader->update())
        {
            return false; // Still loading from disk or no work ready
        }

        auto* renderHandler = offScreen->getRenderPassHandler();
        auto* meshPipeline = renderHandler->getMeshPipeline();

        if (!meshPipeline)
        {
            return false;
        }

        auto result = asyncLoader->processGPUUpload(meshPipeline);

        if (result.success)
        {
            loadedMeshPath = pendingMeshPath;
            meshBounds = result.bounds;
            pendingMeshPath.clear();
            asyncLoader->clearCompleted();
            return true;
        }
        else if (asyncLoader->getProgress(pendingMeshPath).isDone())
        {
            // Loading failed or was cancelled
            pendingMeshPath.clear();
            asyncLoader->clearCompleted();
            return true;
        }

        return false;
    }

    std::vector<services::SubMeshInfo> MeshPreviewController::getSubMeshInfo() const
    {
        std::vector<services::SubMeshInfo> result;

        if (loadedMeshPath.empty())
        {
            return result;
        }

        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        if (!meshPipeline)
        {
            return result;
        }

        const render::mesh::MeshGPUData* gpuData = meshPipeline->getMesh(loadedMeshPath);
        if (!gpuData)
        {
            return result;
        }

        for (size_t i = 0; i < gpuData->subMeshes.size(); ++i)
        {
            const auto& subMesh = gpuData->subMeshes[i];
            services::SubMeshInfo info;
            info.name = subMesh.name.empty() ? ("SubMesh_" + std::to_string(i)) : subMesh.name;
            info.vertexCount = subMesh.lodLevels[0].vertexCount;
            info.indexCount = subMesh.lodLevels[0].indexCount;
            result.push_back(info);
        }

        return result;
    }

    std::vector<services::LODInfo> MeshPreviewController::getLODInfo() const
    {
        std::vector<services::LODInfo> result;

        if (loadedMeshPath.empty())
        {
            return result;
        }

        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        if (!meshPipeline)
        {
            return result;
        }

        const render::mesh::MeshGPUData* gpuData = meshPipeline->getMesh(loadedMeshPath);
        if (!gpuData || gpuData->subMeshes.empty())
        {
            return result;
        }

        // Aggregate LOD info across all submeshes
        static constexpr float lodPercents[] = {100.0f, 50.0f, 25.0f, 12.5f};

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            services::LODInfo info;
            info.lodLevel = lod;
            info.vertexCount = 0;
            info.indexCount = 0;
            info.reductionPercent = lodPercents[lod];

            for (const auto& subMesh : gpuData->subMeshes)
            {
                info.vertexCount += subMesh.lodLevels[lod].vertexCount;
                info.indexCount += subMesh.lodLevels[lod].indexCount;
            }

            result.push_back(info);
        }

        return result;
    }

    void MeshPreviewController::updateCamera(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();

        if (renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->getMeshPipeline()->updateCameraUBO(view, projection, cameraPos);
        }

        currentFrustum.extractFromMatrix(projection * view);
    }

    void* MeshPreviewController::render()
    {
        if (!initialized || loadedMeshPath.empty())
        {
            return nullptr;
        }

        auto* renderHandler = offScreen->getRenderPassHandler();

        std::vector<render::mesh::MeshRenderData> meshDrawList;

        render::mesh::MeshRenderData renderData;
        renderData.meshPath = loadedMeshPath;
        renderData.modelMatrix = modelMatrix;
        renderData.albedo = glm::vec4(0.5294f, 0.8078f, 0.9216f, 1.0f); // Light blue
        renderData.metallic = 0.0f;
        renderData.roughness = 1.0f;
        renderData.ao = 1.0f;
        renderData.emission = 0.0f;
        renderData.showBoundingBox = false;
        renderData.highlightedSubMesh = highlightedSubMesh;
        renderData.forceLODLevel = forceLODLevel;

        meshDrawList.push_back(renderData);

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&currentFrustum);

        // Render and return descriptor set
        vk::DescriptorSet descriptorSet = offScreen->render();
        return static_cast<void*>(descriptorSet);
    }
}
