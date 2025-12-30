#include "MeshAssetManager.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"

namespace controllers::offscreen
{
    MeshAssetManager::MeshAssetManager(render::RenderPassHandler& renderHandler)
        : renderHandler{renderHandler}
    {
    }

    std::string MeshAssetManager::load(std::string_view meshPath)
    {
        renderHandler.initMeshPipeline();

        auto* meshPipeline = renderHandler.getMeshPipeline();
        if (!meshPipeline)
        {
            return "";
        }

        return meshPipeline->loadMesh(meshPath);
    }

    void MeshAssetManager::unload(const std::string& meshId)
    {
        auto* meshPipeline = renderHandler.getMeshPipeline();
        if (meshPipeline)
        {
            meshPipeline->unloadMesh(meshId);
        }
    }

    bool MeshAssetManager::isLoaded(const std::string& meshPath) const
    {
        auto* meshPipeline = renderHandler.getMeshPipeline();
        return meshPipeline && meshPipeline->isMeshLoaded(meshPath);
    }

    std::vector<std::string> MeshAssetManager::getLoadedMeshes() const
    {
        auto* meshPipeline = renderHandler.getMeshPipeline();
        if (!meshPipeline)
        {
            return {};
        }
        return meshPipeline->getLoadedMeshIds();
    }

    std::optional<services::MeshBounds> MeshAssetManager::getBoundingBox(const std::string& meshPath) const
    {
        auto* meshPipeline = renderHandler.getMeshPipeline();
        if (!meshPipeline)
        {
            return std::nullopt;
        }

        const math::AABB* aabb = meshPipeline->getMeshBoundingBox(meshPath);
        if (!aabb)
        {
            return std::nullopt;
        }

        services::MeshBounds bounds;
        bounds.min = aabb->min;
        bounds.max = aabb->max;
        return bounds;
    }
}
