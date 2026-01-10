#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class ColliderDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawShapeSelection(services::ColliderComponentData& colliderData);
        bool drawShapeParameters(services::ColliderComponentData& colliderData);
        bool drawPhysicsMaterial(services::ColliderComponentData& colliderData);
        bool drawTriggerSettings(services::ColliderComponentData& colliderData);
        bool drawCollisionLayer(services::ColliderComponentData& colliderData);
    };
}
