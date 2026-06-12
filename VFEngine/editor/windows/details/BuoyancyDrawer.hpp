#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class BuoyancyDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawSettings(services::BuoyancyComponentData& buoyancyData);
        bool drawCustomPoints(services::BuoyancyComponentData& buoyancyData);
    };
}
