#pragma once
#include "../interfaces/ISceneService.hpp"
#include "../events/SceneEvents.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <optional>

// Forward declarations to avoid including scene headers
namespace scene {
    class SceneGraphSystem;
}

namespace services {

    // Internal conversion functions between EntityHandle and entt::entity
    namespace internal {
        inline EntityHandle toHandle(entt::entity entity) {
            return EntityHandle{ static_cast<uint64_t>(static_cast<uint32_t>(entity)) };
        }

        inline entt::entity fromHandle(EntityHandle handle) {
            return static_cast<entt::entity>(static_cast<uint32_t>(handle.id));
        }

        inline bool isValidHandle(EntityHandle handle, entt::registry& registry) {
            if (!handle.isValid()) return false;
            auto entity = fromHandle(handle);
            return registry.valid(entity);
        }
    }

    class SceneServiceImpl : public ISceneService {
    public:
        explicit SceneServiceImpl(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~SceneServiceImpl() override = default;

        // Register all command and query handlers with the EventDispatcher
        void registerEventHandlers();

        // Entity Lifecycle
        EntityHandle createEntity(const std::string& name,
            std::optional<EntityHandle> parent = std::nullopt) override;
        bool deleteEntity(EntityHandle entity, bool deleteChildren = true) override;
        EntityHandle duplicateEntity(EntityHandle entity) override;

        // Hierarchy Operations
        bool reparentEntity(EntityHandle entity, EntityHandle newParent) override;
        bool moveEntity(EntityHandle entity, EntityHandle targetParent, int insertIndex) override;
        EntityHandle getRoot() const override;

        // Entity Queries
        std::optional<EntityData> getEntity(EntityHandle handle) const override;
        std::optional<EntityHandle> findEntityByName(const std::string& name) const override;
        std::vector<EntityHandle> findEntitiesByName(const std::string& name) const override;
        std::vector<EntityHandle> getEntitiesWithComponent(ComponentTypeId type) const override;
        SceneHierarchyData getSceneHierarchy() const override;

        // Transform Operations
        void setTransform(EntityHandle entity, const TransformData& transform) override;
        std::optional<TransformData> getTransform(EntityHandle entity) const override;
        std::optional<TransformData> getWorldTransform(EntityHandle entity) const override;

        // Component Queries
        bool hasComponent(EntityHandle entity, ComponentTypeId type) const override;
        std::vector<ComponentTypeId> getComponentTypes(EntityHandle entity) const override;

        // Camera Operations
        std::optional<CameraData> getCameraData(EntityHandle entity) const override;
        bool setCameraData(EntityHandle entity, const CameraData& camera) override;
        std::optional<EntityHandle> getPrimaryCamera() const override;
        bool addCameraComponent(EntityHandle entity) override;
        bool removeCameraComponent(EntityHandle entity) override;

        // IBL Operations
        std::optional<IBLData> getIBLData(EntityHandle entity) const override;
        bool setIBLData(EntityHandle entity, const IBLData& ibl) override;
        bool removeIBLComponent(EntityHandle entity) override;

        // Hierarchy - Children
        std::vector<EntityHandle> getChildren(EntityHandle entity) const override;

        // Selection State
        void setSelectedEntity(std::optional<EntityHandle> entity) override;
        std::optional<EntityHandle> getSelectedEntity() const override;

        // Entity Naming
        std::string getEntityName(EntityHandle entity) const override;
        void setEntityName(EntityHandle entity, const std::string& name) override;

        // Scene Lifecycle
        bool newScene();
        bool saveScene(const std::string& filePath);
        bool loadScene(const std::string& filePath);

    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        std::optional<EntityHandle> selectedEntity;

        // Helper to build EntityData from entt::entity
        EntityData buildEntityData(entt::entity entity) const;

        // Recursive helper for scene hierarchy
        void collectHierarchy(entt::entity entity, std::vector<EntityData>& entities) const;
    };

}
