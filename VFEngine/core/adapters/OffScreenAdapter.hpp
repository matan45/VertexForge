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
        void meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f) override;
        bool isMeshLoaded(const std::string& meshPath) const override;
        std::vector<std::string> getLoadedMeshes() const override;
        void prepareFrameMeshes() override;
    };
}
