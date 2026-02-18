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
        bool drawDefaultMode(services::PhysicsAnimationComponentData& data);
        bool drawCollisionLayer(services::PhysicsAnimationComponentData& data);
        bool drawBoneMappings(services::PhysicsAnimationComponentData& data);
        bool drawJointLimits(services::PhysicsAnimationComponentData& data);
    };
}
