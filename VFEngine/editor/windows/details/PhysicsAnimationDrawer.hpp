#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class PhysicsAnimationDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        void drawFilePicker(services::EntityHandle handle, services::PhysicsAnimationComponentData& data, bool& changed);
        void drawConfigSummary(const services::PhysicsAnimationComponentData& data);
    };
}
