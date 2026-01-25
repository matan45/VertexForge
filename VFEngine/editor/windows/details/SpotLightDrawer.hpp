#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class SpotLightDrawer {
    public:
        bool draw(services::EntityHandle handle);
    };

}
