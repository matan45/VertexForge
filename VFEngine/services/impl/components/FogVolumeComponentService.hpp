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

    class FogVolumeComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit FogVolumeComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addFogVolumeComponent(EntityHandle entity);
        bool removeFogVolumeComponent(EntityHandle entity);
        bool hasFogVolumeComponent(EntityHandle entity) const;
        std::optional<FogVolumeData> getFogVolumeData(EntityHandle entity) const;
        bool setFogVolumeData(EntityHandle entity, const FogVolumeData& data);

    private:
        void autoAttachBillboard(EntityHandle entity, uint32_t iconType);
        void autoDetachBillboard(EntityHandle entity, uint32_t iconType);
    };

}
