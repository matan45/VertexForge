#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class MeshDrawer {
    public:
        // Returns true if mesh component exists
        bool draw(services::EntityHandle handle);
    };

}
