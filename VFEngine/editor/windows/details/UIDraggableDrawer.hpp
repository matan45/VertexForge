#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details {
    class UIDraggableDrawer {
    public:
        bool draw(services::EntityHandle handle);
    private:
        bool drawHeader(bool& outRemove);
    };
}
