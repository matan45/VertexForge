#pragma once
#include "../../services/providers/IOffScreenProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class OffScreenAdapter : public services::IOffScreenProvider
    {
    private:
        controllers::OffScreen* offScreen;

    public:
        explicit OffScreenAdapter(controllers::OffScreen* offScreen);
        ~OffScreenAdapter() override = default;
        
        void init() override;
        void cleanUp() override;
        void* render() override;

        // IBL API
        void iblSet(std::string_view iblPath) override;
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection) override;
        void iblRemove() override;

        // Mesh API
        std::string meshLoad(std::string_view meshPath) override;
        void meshUnload(const std::string& meshId) override;
        void meshUpdateCamera(services::CameraId cameraId, const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f) override;
        bool isMeshLoaded(const std::string& meshPath) const override;
        std::vector<std::string> getLoadedMeshes() const override;
        void prepareCameras() override;
        std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const override;
        void prepareFrameMeshes() override;

        // BVH spatial culling
        void rebuildBVH() override;
        void markBVHDirty() override;

        // Multi-camera occlusion culling
        void createCamera(services::CameraId id, bool enableOcclusion = false) override;
        void removeCamera(services::CameraId id) override;
        void setActiveCamera(services::CameraId id) override;
        services::CameraId getActiveCameraId() const override;
        void prepareFrameCameraFrustums() override;

        // Billboard API
        void prepareFrameBillboards() override;
        void setShowBillboardIcons(bool show) override;
        bool getShowBillboardIcons() const override;
        bool loadBillboardAtlas(const std::string& atlasPath) override;

        // Debug/Stats API
        services::CullingDebugStats getCullingStats() const override;
    };
}
