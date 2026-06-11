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
        void drawRuntimeControls(services::EntityHandle handle, const services::PhysicsAnimationComponentData& data);

        int selectedHitBone = 0;
        float globalStrength = 1.0f;
    };
}
