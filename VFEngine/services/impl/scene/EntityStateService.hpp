#pragma once
#include "../../data/EntityHandle.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
}

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class EntityStateService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        // Multi-selection: front() is the primary entity (what the gizmo,
        // details panel and single-selection consumers operate on).
        std::vector<EntityHandle> selectedEntities;

    public:
        explicit EntityStateService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // Selection state
        void setSelectedEntity(std::optional<EntityHandle> entity);
        void setSelectedEntities(std::vector<EntityHandle> entities);
        std::optional<EntityHandle> getSelectedEntity() const;
        const std::vector<EntityHandle>& getSelectedEntities() const;
        void clearSelection();

        // Entity metadata
        void setEntityName(EntityHandle entity, const std::string& name);
        std::string getEntityName(EntityHandle entity) const;
        void setEntityActive(EntityHandle entity, bool isActive);

        // VK-1433 Phase 4 — tag/untag a Prefab Rig Preview editing-sandbox root. Returns false
        // for an invalid handle. Mirrors UIComponentService::markUIPreviewSandbox.
        bool markPreviewSandbox(EntityHandle entity, bool tagged);

        // Static state
        bool setEntityStatic(EntityHandle entity, bool isStatic);
        bool isEntityStatic(EntityHandle entity) const;

        // VK-1597: World Sector streaming policy. The component is only ever ADDED here - absence
        // means spatially loaded, so an entity the user has never touched keeps a clean payload,
        // but once pinned it keeps the component (holding true again) so the value round-trips.
        bool setEntitySpatiallyLoaded(EntityHandle entity, bool spatiallyLoaded);
        bool isEntitySpatiallyLoaded(EntityHandle entity) const;

        // VK-1599: which named runtime grid the entity streams on. 0 is the primary grid and the
        // default; setting 0 on an entity with no StreamingPolicyComponent is deliberately a no-op.
        bool setEntityStreamingGrid(EntityHandle entity, uint8_t gridIndex);
        [[nodiscard]] uint8_t getEntityStreamingGrid(EntityHandle entity) const;
    };
}
