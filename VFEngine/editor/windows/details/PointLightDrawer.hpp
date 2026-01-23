#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class PointLightDrawer {
    public:
        bool draw(services::EntityHandle handle);
    };

}
