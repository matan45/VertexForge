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

    class RenderTextureComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit RenderTextureComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addRenderTextureComponent(EntityHandle entity);
        bool removeRenderTextureComponent(EntityHandle entity);
        bool hasRenderTextureComponent(EntityHandle entity) const;
        std::optional<RenderTextureData> getRenderTextureData(EntityHandle entity) const;
        bool setRenderTextureData(EntityHandle entity, const RenderTextureData& data);
    };

}
