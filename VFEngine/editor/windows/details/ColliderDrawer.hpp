#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

#include <string>

namespace windows::details
{
    class ColliderDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawShapeSelection(services::ColliderComponentData& colliderData);
        bool drawShapeParameters(services::EntityHandle handle, services::ColliderComponentData& colliderData);
        bool drawMeshColliderAssetControls(services::EntityHandle handle,
                                           const services::ColliderComponentData& colliderData);
        std::string resolveMeshPath(services::EntityHandle handle,
                                    const services::ColliderComponentData& colliderData) const;
        bool drawPhysicsMaterial(services::ColliderComponentData& colliderData);
        bool drawTriggerSettings(services::ColliderComponentData& colliderData);
        bool drawCollisionLayer(services::ColliderComponentData& colliderData);
    };
}
