#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details {
    class UIDropTargetDrawer {
    public:
        bool draw(services::EntityHandle handle);
    private:
        bool drawHeader(bool& outRemove);
    };
}
