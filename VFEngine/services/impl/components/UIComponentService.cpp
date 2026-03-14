#include "UIComponentService.hpp"
#include "../../events/EventDispatcher.hpp"
#include "scene/SceneGraphSystem.hpp"

namespace services {

    UIComponentService::UIComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    void UIComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        registerCanvasRectImageHandlers(dispatcher);
        registerScrollLayoutHandlers(dispatcher);
        registerInteractiveHandlers(dispatcher);
        registerDropdownTabsHandlers(dispatcher);
        registerSliderProgressHandlers(dispatcher);
        registerAnimationHandlers(dispatcher);
        registerMaskHandlers(dispatcher);
    }

}
