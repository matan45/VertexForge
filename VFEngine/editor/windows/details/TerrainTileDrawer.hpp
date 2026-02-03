#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class TerrainTileDrawer {
    public:
        bool draw(services::EntityHandle handle);
    };

}
