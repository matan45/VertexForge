#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class LightmapRootDrawer {
    public:
        void draw(services::EntityHandle handle);
    };

}
