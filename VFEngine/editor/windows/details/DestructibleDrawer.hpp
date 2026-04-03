#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class DestructibleDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawHealthSettings(services::DestructibleComponentData& data);
        bool drawFractureSettings(services::DestructibleComponentData& data);
        bool drawEffectSettings(services::DestructibleComponentData& data);
        bool drawPropagationSettings(services::DestructibleComponentData& data);
    };
}
