#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class IBLDrawer {
    public:
        void draw(services::EntityHandle handle);
    };

}
