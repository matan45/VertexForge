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

    class LightComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit LightComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // Directional Light Operations
        bool addDirectionalLightComponent(EntityHandle entity);
        bool removeDirectionalLightComponent(EntityHandle entity);
        bool hasDirectionalLightComponent(EntityHandle entity) const;
        std::optional<DirectionalLightData> getDirectionalLightData(EntityHandle entity) const;
        bool setDirectionalLightData(EntityHandle entity, const DirectionalLightData& lightData);

        // Point Light Operations
        bool addPointLightComponent(EntityHandle entity);
        bool removePointLightComponent(EntityHandle entity);
        bool hasPointLightComponent(EntityHandle entity) const;
        std::optional<PointLightData> getPointLightData(EntityHandle entity) const;
        bool setPointLightData(EntityHandle entity, const PointLightData& lightData);

        // Spot Light Operations
        bool addSpotLightComponent(EntityHandle entity);
        bool removeSpotLightComponent(EntityHandle entity);
        bool hasSpotLightComponent(EntityHandle entity) const;
        std::optional<SpotLightData> getSpotLightData(EntityHandle entity) const;
        bool setSpotLightData(EntityHandle entity, const SpotLightData& lightData);

    private:
        void autoAttachBillboard(EntityHandle entity);
        void autoDetachBillboard(EntityHandle entity);
        bool hasAnyLightComponent(EntityHandle entity) const;
    };

}
