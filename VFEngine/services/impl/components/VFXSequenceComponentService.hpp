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

    // VK-1438: brings VFXSequenceComponent onto the standard DTO/command path used by VFX and
    // Billboard (replacing the registry-direct drawer/popup idiom VK-1425 shipped). Mirrors
    // VFXComponentService verbatim, including the auto-attached Particle editor billboard so a
    // dropped/added sequence entity has a selectable viewport representation.
    class VFXSequenceComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit VFXSequenceComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addVFXSequenceComponent(EntityHandle entity);
        bool removeVFXSequenceComponent(EntityHandle entity);
        bool hasVFXSequenceComponent(EntityHandle entity) const;
        std::optional<VFXSequenceData> getVFXSequenceData(EntityHandle entity) const;
        bool setVFXSequenceData(EntityHandle entity, const VFXSequenceData& vfxSequenceData);

    };

}
