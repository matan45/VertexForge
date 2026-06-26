#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class VFXSequenceDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawSequenceFilePath(services::VFXSequenceData& seq);
        bool drawSettings(services::VFXSequenceData& seq);
        bool drawTriggers(services::VFXSequenceData& seq);
    };
}
