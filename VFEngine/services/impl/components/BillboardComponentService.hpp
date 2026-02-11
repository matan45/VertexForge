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

    class BillboardComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit BillboardComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addBillboardComponent(EntityHandle entity);
        bool removeBillboardComponent(EntityHandle entity);
        bool hasBillboardComponent(EntityHandle entity) const;
        std::optional<BillboardData> getBillboardData(EntityHandle entity) const;
        bool setBillboardData(EntityHandle entity, const BillboardData& billboardData);
    };

}
