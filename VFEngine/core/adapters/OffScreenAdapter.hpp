#pragma once
#include "../../services/providers/IOffScreenProvider.hpp"
#include <memory>

namespace controllers {
    class OffScreen;
}

namespace core {

    /**
     * @brief Adapter that implements IOffScreenProvider by wrapping the OffScreen controller.
     *
     * This class bridges the Services layer with the Core layer's OffScreen controller,
     * allowing Services to use rendering functionality without direct dependencies on Core.
     */
    class OffScreenAdapter : public services::IOffScreenProvider {
    public:
        /**
         * @brief Construct adapter with existing OffScreen controller.
         * @param offScreen Pointer to the OffScreen controller (ownership retained by caller)
         */
        explicit OffScreenAdapter(::controllers::OffScreen* offScreen);
        ~OffScreenAdapter() override = default;

        // IOffScreenProvider implementation
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

    private:
        ::controllers::OffScreen* offScreen;
    };

}
