#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "config/Config.hpp"
#include "ConvexDecompositionRegenerator.hpp"

#include <atomic>
#include <future>
#include <string>

namespace windows::details
{
    class ColliderDrawer
    {
    public:
        ~ColliderDrawer();

        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawShapeSelection(services::ColliderComponentData& colliderData);
        bool drawShapeParameters(services::EntityHandle handle, services::ColliderComponentData& colliderData);
        bool drawConvexRegenerationControls(services::EntityHandle handle,
                                            const services::ColliderComponentData& colliderData);
        bool drawConvexRegenerationSettings();
        std::string resolveMeshPath(services::EntityHandle handle,
                                    const services::ColliderComponentData& colliderData) const;
        void pollRegenerationResult(services::EntityHandle handle, const std::string& meshPath);
        bool drawPhysicsMaterial(services::ColliderComponentData& colliderData);
        bool drawTriggerSettings(services::ColliderComponentData& colliderData);
        bool drawCollisionLayer(services::ColliderComponentData& colliderData);

        importConfig::MeshImportConfig convexRegenConfig;
        std::future<types::ConvexRegenerationResult> convexRegenFuture;
        std::atomic<bool> cancelConvexRegen{false};
        std::atomic<float> convexRegenProgress{0.0f};
        std::string convexRegenStatus;
        std::string activeConvexRegenMeshPath;
        services::EntityHandle activeConvexRegenEntity;
    };
}
