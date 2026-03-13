#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class WorldSectorDrawer {
    public:
        void draw(services::EntityHandle handle);
    };

}
