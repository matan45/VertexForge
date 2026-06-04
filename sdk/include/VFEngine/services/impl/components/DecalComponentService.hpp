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

    class DecalComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit DecalComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addDecalComponent(EntityHandle entity);
        bool removeDecalComponent(EntityHandle entity);
        bool hasDecalComponent(EntityHandle entity) const;
        std::optional<DecalData> getDecalData(EntityHandle entity) const;
        bool setDecalData(EntityHandle entity, const DecalData& decalData);
    };

}
