#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <glm/glm.hpp>
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

        // UI Scroll Operations
        bool addUIScrollComponent(EntityHandle entity);
        bool removeUIScrollComponent(EntityHandle entity);
        bool hasUIScrollComponent(EntityHandle entity) const;
        std::optional<UIScrollData> getUIScrollData(EntityHandle entity) const;
        bool setUIScrollData(EntityHandle entity, const UIScrollData& scrollData);
        bool setScrollOffset(EntityHandle entity, const glm::vec2& offset);
        std::optional<glm::vec2> getScrollOffset(EntityHandle entity) const;

        // UI Layout Group Operations
        bool addUILayoutGroupComponent(EntityHandle entity);
        bool removeUILayoutGroupComponent(EntityHandle entity);
        bool hasUILayoutGroupComponent(EntityHandle entity) const;
        std::optional<UILayoutGroupData> getUILayoutGroupData(EntityHandle entity) const;
        bool setUILayoutGroupData(EntityHandle entity, const UILayoutGroupData& layoutGroupData);

        // UI Label Operations
        bool addUILabelComponent(EntityHandle entity);
        bool removeUILabelComponent(EntityHandle entity);
        bool hasUILabelComponent(EntityHandle entity) const;
        std::optional<UILabelData> getUILabelData(EntityHandle entity) const;
        bool setUILabelData(EntityHandle entity, const UILabelData& labelData);
        std::optional<glm::vec2> getUILabelPreferredSize(EntityHandle entity) const;

        // UI Button Operations
        bool addUIButtonComponent(EntityHandle entity);
        bool removeUIButtonComponent(EntityHandle entity);
        bool hasUIButtonComponent(EntityHandle entity) const;
        std::optional<UIButtonData> getUIButtonData(EntityHandle entity) const;
        bool setUIButtonData(EntityHandle entity, const UIButtonData& buttonData);
    };

}
