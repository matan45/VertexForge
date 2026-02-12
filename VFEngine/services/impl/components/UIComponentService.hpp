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

    class UIComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit UIComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // UI Canvas Operations
        bool addUICanvasComponent(EntityHandle entity);
        bool removeUICanvasComponent(EntityHandle entity);
        bool hasUICanvasComponent(EntityHandle entity) const;
        std::optional<UICanvasData> getUICanvasData(EntityHandle entity) const;
        bool setUICanvasData(EntityHandle entity, const UICanvasData& canvasData);

        // UI Rect Operations
        bool addUIRectComponent(EntityHandle entity);
        bool removeUIRectComponent(EntityHandle entity);
        bool hasUIRectComponent(EntityHandle entity) const;
        std::optional<UIRectData> getUIRectData(EntityHandle entity) const;
        bool setUIRectData(EntityHandle entity, const UIRectData& rectData);

        // UI Image Operations
        bool addUIImageComponent(EntityHandle entity);
        bool removeUIImageComponent(EntityHandle entity);
        bool hasUIImageComponent(EntityHandle entity) const;
        std::optional<UIImageData> getUIImageData(EntityHandle entity) const;
        bool setUIImageData(EntityHandle entity, const UIImageData& imageData);
    };

}
