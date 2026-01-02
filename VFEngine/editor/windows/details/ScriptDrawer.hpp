#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class ScriptDrawer {
    public:
        // Returns true if script component exists
        bool draw(services::EntityHandle handle);
    };

}
