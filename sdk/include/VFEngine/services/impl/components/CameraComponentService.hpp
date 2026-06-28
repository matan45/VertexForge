#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <memory>
#include <optional>

namespace scene {
    class SceneGraphSystem;
}

namespace events {
    class EventDispatcher;
}

namespace services {

    class CameraComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit CameraComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // Camera Operations
        std::optional<CameraData> getCameraData(EntityHandle entity) const;
        bool setCameraData(EntityHandle entity, const CameraData& camera);
        std::optional<EntityHandle> getPrimaryCamera() const;
        bool addCameraComponent(EntityHandle entity);
        bool removeCameraComponent(EntityHandle entity);
        bool hasCameraComponent(EntityHandle entity) const;

    };

}
