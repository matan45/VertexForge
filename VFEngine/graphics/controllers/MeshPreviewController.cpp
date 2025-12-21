#include "MeshPreviewController.hpp"
#include "../core/VulkanContext.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
#include "resource/Types.hpp"
#include "print/Logger.hpp"

namespace controllers
{
    MeshPreviewController::MeshPreviewController()
        : swapChain{ *core::VulkanContext::getSwapChain() }
        , device{ *core::VulkanContext::getDevice() }
        , offScreen{ std::make_unique<render::OffScreenViewPort>(device, swapChain) }
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
        auto* renderHandler = offScreen->getRenderPassHandler();
        renderHandler->initMeshPipeline();

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

    bool MeshPreviewController::loadMesh(const std::string& meshPath, math::AABB& outBounds)
    {
        if (!initialized)
        {
            init();
        }
        
        if (!loadedMeshPath.empty())
        {
            unloadMesh();
        }

        auto* renderHandler = offScreen->getRenderPassHandler();
        auto* meshPipeline = renderHandler->getMeshPipeline();

        if (!meshPipeline)
        {
            return false;
        }
        
        std::string meshId = meshPipeline->loadMesh(meshPath);
        if (meshId.empty())
        {
            return false;
        }

        loadedMeshPath = meshPath;

        // Get bounding box for camera fitting
        const math::AABB* bounds = meshPipeline->getMeshBoundingBox(meshPath);
        if (bounds)
        {
            meshBounds = *bounds;
            outBounds = meshBounds;
        }
        else
        {
            meshBounds = math::AABB(glm::vec3(-1.0f), glm::vec3(1.0f));
            outBounds = meshBounds;
        }

        return true;
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
        static constexpr float lodPercents[] = { 100.0f, 50.0f, 25.0f, 12.5f };

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

        // Update frustum for culling
        currentFrustum.extractFromMatrix(projection * view);
    }

    void* MeshPreviewController::render()
    {
        if (!initialized || loadedMeshPath.empty())
        {
            return nullptr;
        }

        auto* renderHandler = offScreen->getRenderPassHandler();

        // Create draw list with just the preview mesh
        std::vector<render::mesh::MeshRenderData> meshDrawList;

        render::mesh::MeshRenderData renderData;
        renderData.meshPath = loadedMeshPath;
        renderData.modelMatrix = modelMatrix;
        renderData.albedo = glm::vec4(0.5294f, 0.8078f, 0.9216f, 1.0f);  // Light blue
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
