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

    class VFXComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit VFXComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addVFXComponent(EntityHandle entity);
        bool removeVFXComponent(EntityHandle entity);
        bool hasVFXComponent(EntityHandle entity) const;
        std::optional<VFXData> getVFXData(EntityHandle entity) const;
        bool setVFXData(EntityHandle entity, const VFXData& vfxData);

    private:
        void autoAttachBillboard(EntityHandle entity, uint32_t iconType);
        void autoDetachBillboard(EntityHandle entity, uint32_t iconType);
    };

}
