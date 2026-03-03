#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>

namespace events {
    class EventDispatcher;
}

namespace services {

    class BakeInfoComponentService {
    public:
        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool hasLightmapRoot(EntityHandle entity) const;
        std::optional<LightmapRootData> getLightmapRootData(EntityHandle entity) const;

        bool hasNavmeshRoot(EntityHandle entity) const;
        std::optional<NavmeshRootData> getNavmeshRootData(EntityHandle entity) const;
    };

}
