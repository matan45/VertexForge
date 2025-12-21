#pragma once
#include "../../services/providers/IMeshPreviewProvider.hpp"
#include "../../graphics/controllers/MeshPreviewController.hpp"
#include <memory>
#include <unordered_map>

namespace core
{
    class MeshPreviewAdapter : public services::IMeshPreviewProvider
    {
    private:
        std::unordered_map<services::PreviewInstanceId, std::unique_ptr<::controllers::MeshPreviewController>> controllers;

    public:
        explicit MeshPreviewAdapter() = default;
        ~MeshPreviewAdapter() noexcept override;

        void initMeshPreview(services::PreviewInstanceId instanceId) override;
        void cleanUpMeshPreview(services::PreviewInstanceId instanceId) override;
        bool isMeshPreviewInitialized(services::PreviewInstanceId instanceId) const override;
        bool loadPreviewMesh(services::PreviewInstanceId instanceId, const std::string& meshPath, math::AABB& outBounds) override;
        void unloadPreviewMesh(services::PreviewInstanceId instanceId) override;
        bool isPreviewMeshLoaded(services::PreviewInstanceId instanceId) const override;
        std::vector<services::SubMeshInfo> getPreviewMeshSubMeshInfo(services::PreviewInstanceId instanceId) const override;
        std::vector<services::LODInfo> getPreviewMeshLODInfo(services::PreviewInstanceId instanceId) const override;
        math::AABB getPreviewMeshBounds(services::PreviewInstanceId instanceId) const override;
        void setMeshPreviewParams(services::PreviewInstanceId instanceId, const services::MeshPreviewParams& params) override;
        void updateMeshCamera(services::PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos) override;
        void* renderMeshPreview(services::PreviewInstanceId instanceId) override;

    private:
        controllers::MeshPreviewController* getController(services::PreviewInstanceId instanceId) const;
    };
}
