#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class WaterTileDrawer {
    public:
        bool draw(services::EntityHandle handle);
    };

}
