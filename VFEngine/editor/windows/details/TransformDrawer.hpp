#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class TransformDrawer {
    public:
        void draw(services::EntityHandle handle);
    };

}
