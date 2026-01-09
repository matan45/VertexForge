#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class RigidBodyDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawBodyType(services::RigidBodyComponentData& rigidBodyData);
        bool drawMassSettings(services::RigidBodyComponentData& rigidBodyData);
        bool drawDampingSettings(services::RigidBodyComponentData& rigidBodyData);
        bool drawConstraints(services::RigidBodyComponentData& rigidBodyData);
    };
}
