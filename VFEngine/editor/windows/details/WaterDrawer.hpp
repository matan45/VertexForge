#pragma once
#include "data/EntityHandle.hpp"
#include <string>

namespace windows::details {

    class WaterDrawer {
    public:
        bool draw(services::EntityHandle handle);

    private:
        int pendingTileX = 0;
        int pendingTileZ = 0;
        std::string statusMessage;
        int statusFrameCounter = 0;
    };

}
