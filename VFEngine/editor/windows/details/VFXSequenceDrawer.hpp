#pragma once
#include "data/EntityHandle.hpp"

namespace components
{
    struct VFXSequenceComponent;
}

namespace windows::details
{
    class VFXSequenceDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawSequenceFilePath(components::VFXSequenceComponent& seq);
        bool drawSettings(components::VFXSequenceComponent& seq);
        bool drawTriggers(components::VFXSequenceComponent& seq);
    };
}
