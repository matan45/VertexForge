#pragma once
#include "data/EntityHandle.hpp"

namespace events {
    class EventDispatcher;
}

namespace windows::details {

    class PointLightDrawer {
    public:
        bool draw(services::EntityHandle handle);

    private:
        void drawShadowOverride(services::EntityHandle entity, events::EventDispatcher& dispatcher);
    };

}
