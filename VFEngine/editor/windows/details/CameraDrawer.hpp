#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class CameraDrawer {
    public:
        bool draw(services::EntityHandle handle);
    };

}
