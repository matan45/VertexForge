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

    class TextComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit TextComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool addTextComponent(EntityHandle entity);
        bool removeTextComponent(EntityHandle entity);
        bool hasTextComponent(EntityHandle entity) const;
        std::optional<TextData> getTextData(EntityHandle entity) const;
        bool setTextData(EntityHandle entity, const TextData& textData);
    };

}
