#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class DirectionalLightDrawer {
    public:
        bool draw(services::EntityHandle handle);
    };

}
