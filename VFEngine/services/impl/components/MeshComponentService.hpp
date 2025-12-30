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

    class MeshComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit MeshComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // Mesh Operations
        std::optional<MeshData> getMeshData(EntityHandle entity) const;
        bool setMeshData(EntityHandle entity, const MeshData& mesh);
        bool addMeshComponent(EntityHandle entity);
        bool removeMeshComponent(EntityHandle entity);
        bool hasMeshComponent(EntityHandle entity) const;
    };

}
