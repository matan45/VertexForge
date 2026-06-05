#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <cstdint>

namespace events {
    class EventDispatcher;
}

namespace components {
    enum class BillboardIconType : uint8_t;
}

namespace services {

    class LightComponentService {
    public:
        LightComponentService() = default;

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addDirectionalLightComponent(EntityHandle entity);
        bool removeDirectionalLightComponent(EntityHandle entity);
        bool hasDirectionalLightComponent(EntityHandle entity) const;
        std::optional<DirectionalLightData> getDirectionalLightData(EntityHandle entity) const;
        bool setDirectionalLightData(EntityHandle entity, const DirectionalLightData& lightData);

        bool addPointLightComponent(EntityHandle entity);
        bool removePointLightComponent(EntityHandle entity);
        bool hasPointLightComponent(EntityHandle entity) const;
        std::optional<PointLightData> getPointLightData(EntityHandle entity) const;
        bool setPointLightData(EntityHandle entity, const PointLightData& lightData);

        bool addSpotLightComponent(EntityHandle entity);
        bool removeSpotLightComponent(EntityHandle entity);
        bool hasSpotLightComponent(EntityHandle entity) const;
        std::optional<SpotLightData> getSpotLightData(EntityHandle entity) const;
        bool setSpotLightData(EntityHandle entity, const SpotLightData& lightData);

        bool hasShadowOverride(EntityHandle entity) const;
        std::optional<ShadowOverrideData> getShadowOverrideData(EntityHandle entity) const;
        bool setShadowOverrideData(EntityHandle entity, const ShadowOverrideData& data);
        bool removeShadowOverride(EntityHandle entity);

    private:
        template<typename ComponentT>
        bool addLightImpl(EntityHandle entity, uint8_t lightType, components::BillboardIconType iconType);
        template<typename ComponentT>
        bool removeLightImpl(EntityHandle entity, uint8_t lightType, components::BillboardIconType iconType);
        template<typename ComponentT>
        bool hasLightImpl(EntityHandle entity) const;

        void autoAttachBillboard(EntityHandle entity, components::BillboardIconType iconType);
        void autoDetachBillboard(EntityHandle entity, components::BillboardIconType iconType);
        bool hasAnyLightComponent(EntityHandle entity) const;
    };

}
