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

    class IBLComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit IBLComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // IBL Operations
        std::optional<IBLData> getIBLData(EntityHandle entity) const;
        bool setIBLData(EntityHandle entity, const IBLData& ibl);
        bool removeIBLComponent(EntityHandle entity);
        bool hasIBLComponent(EntityHandle entity) const;
    };

}
