#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    // VK-1607: inspector for WaterBodyComponent — a bounded lake or pool sitting at its own level,
    // additive to the scene's single ocean.
    class WaterBodyDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawSettings(services::WaterBodyComponentData& bodyData);
    };
}
