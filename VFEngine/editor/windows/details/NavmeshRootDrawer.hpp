#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class NavmeshRootDrawer {
    public:
        void draw(services::EntityHandle handle);
    };

}
