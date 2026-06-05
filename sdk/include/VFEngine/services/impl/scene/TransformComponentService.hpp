#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <memory>
#include <optional>

namespace scene
{
    class SceneGraphSystem;
}

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class TransformComponentService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit TransformComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        void setTransform(EntityHandle entity, const TransformData& transform);
        void setWorldTransform(EntityHandle entity, const TransformData& worldTransform);
        std::optional<TransformData> getTransform(EntityHandle entity) const;
        std::optional<TransformData> getWorldTransform(EntityHandle entity) const;
    };
}
