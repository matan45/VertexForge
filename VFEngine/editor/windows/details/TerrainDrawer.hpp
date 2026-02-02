#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class TerrainDrawer {
    public:
        bool draw(services::EntityHandle handle);
    };

}
