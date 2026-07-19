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

    // VK-1577 — authoring surface for components::ReflectionProbeComponent.
    //
    // Mirrors FogVolumeComponentService, plus requestBake(). Bake requests are expressed purely as
    // component state (`dirty = true`) rather than a renderer call: the render side already walks
    // the probe components every frame to build its SSBO, so it picks the flag up there. That keeps
    // this service free of any graphics dependency and means a bake request survives being issued
    // before the renderer exists (e.g. during scene load).
    class ReflectionProbeComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit ReflectionProbeComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addReflectionProbeComponent(EntityHandle entity);
        bool removeReflectionProbeComponent(EntityHandle entity);
        bool hasReflectionProbeComponent(EntityHandle entity) const;
        std::optional<ReflectionProbeData> getReflectionProbeData(EntityHandle entity) const;
        bool setReflectionProbeData(EntityHandle entity, const ReflectionProbeData& data);

        // Flags probes as needing a capture. An invalid handle means "every probe in the scene".
        // Returns true if at least one probe was flagged.
        bool requestBake(EntityHandle entity);
    };

}
