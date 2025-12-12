#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/DTOs.hpp"
#include <optional>
#include <vector>
#include <string>

namespace services {

    // Scene service interface - abstracts scene/ECS operations
    // Presentation layer uses this instead of direct EntityRegistry access
    class ISceneService {
    public:
        virtual ~ISceneService() = default;

        // ============================================
        // Entity Lifecycle
        // ============================================

        // Create a new entity with optional parent
        virtual EntityHandle createEntity(const std::string& name,
            std::optional<EntityHandle> parent = std::nullopt) = 0;

        // Delete an entity and optionally its children
        virtual bool deleteEntity(EntityHandle entity, bool deleteChildren = true) = 0;

        // Duplicate an entity (deep copy)
        virtual EntityHandle duplicateEntity(EntityHandle entity) = 0;

        // ============================================
        // Hierarchy Operations
        // ============================================

        // Change entity's parent
        virtual bool reparentEntity(EntityHandle entity, EntityHandle newParent) = 0;

        // Move entity in sibling order (for reordering in scene graph UI)
        virtual bool moveEntity(EntityHandle entity, EntityHandle targetParent, int insertIndex) = 0;

        // Get the root entity of the scene
        virtual EntityHandle getRoot() const = 0;

        // Alias for getRoot (for clarity in UI code)
        virtual EntityHandle getRootEntity() const { return getRoot(); }

        // Get children of an entity
        virtual std::vector<EntityHandle> getChildren(EntityHandle entity) const = 0;

        // ============================================
        // Entity Queries
        // ============================================

        // Get full entity data
        virtual std::optional<EntityData> getEntity(EntityHandle handle) const = 0;

        // Alias for getEntity
        virtual std::optional<EntityData> getEntityData(EntityHandle handle) const { return getEntity(handle); }

        // Get entity by name (first match)
        virtual std::optional<EntityHandle> findEntityByName(const std::string& name) const = 0;

        // Find all entities with a given name
        virtual std::vector<EntityHandle> findEntitiesByName(const std::string& name) const = 0;

        // Get all entities with a specific component type
        virtual std::vector<EntityHandle> getEntitiesWithComponent(ComponentTypeId type) const = 0;

        // Get full scene hierarchy
        virtual SceneHierarchyData getSceneHierarchy() const = 0;

        // ============================================
        // Transform Operations
        // ============================================

        // Set entity transform (local space)
        virtual void setTransform(EntityHandle entity, const TransformData& transform) = 0;

        // Get entity transform (local space)
        virtual std::optional<TransformData> getTransform(EntityHandle entity) const = 0;

        // Get entity world transform
        virtual std::optional<TransformData> getWorldTransform(EntityHandle entity) const = 0;

        // ============================================
        // Component Queries
        // ============================================

        // Check if entity has a component
        virtual bool hasComponent(EntityHandle entity, ComponentTypeId type) const = 0;

        // Get list of component types on entity
        virtual std::vector<ComponentTypeId> getComponentTypes(EntityHandle entity) const = 0;

        // ============================================
        // Camera Operations (convenience methods)
        // ============================================

        // Get camera data if entity has camera component
        virtual std::optional<CameraData> getCameraData(EntityHandle entity) const = 0;

        // Set camera data
        virtual bool setCameraData(EntityHandle entity, const CameraData& camera) = 0;

        // Get first camera entity in scene
        virtual std::optional<EntityHandle> getPrimaryCamera() const = 0;

        // Add camera component to entity
        virtual bool addCameraComponent(EntityHandle entity) = 0;

        // Remove camera component from entity
        virtual bool removeCameraComponent(EntityHandle entity) = 0;

        // ============================================
        // IBL Component Operations
        // ============================================

        // Get IBL component data
        virtual std::optional<IBLData> getIBLData(EntityHandle entity) const = 0;

        // Set IBL component data (adds component if not present)
        virtual bool setIBLData(EntityHandle entity, const IBLData& ibl) = 0;

        // Remove IBL component from entity
        virtual bool removeIBLComponent(EntityHandle entity) = 0;

        // ============================================
        // Mesh Component Operations
        // ============================================

        // Get mesh component data
        virtual std::optional<MeshData> getMeshData(EntityHandle entity) const = 0;

        // Set mesh component data (adds component if not present)
        virtual bool setMeshData(EntityHandle entity, const MeshData& mesh) = 0;

        // Add mesh component to entity
        virtual bool addMeshComponent(EntityHandle entity) = 0;

        // Remove mesh component from entity
        virtual bool removeMeshComponent(EntityHandle entity) = 0;

        // Check if entity has mesh component
        virtual bool hasMeshComponent(EntityHandle entity) const = 0;

        // ============================================
        // Selection State
        // ============================================

        // Set the currently selected entity (for editor)
        virtual void setSelectedEntity(std::optional<EntityHandle> entity) = 0;

        // Get the currently selected entity
        virtual std::optional<EntityHandle> getSelectedEntity() const = 0;

        // ============================================
        // Entity Naming
        // ============================================

        // Get entity name
        virtual std::string getEntityName(EntityHandle entity) const = 0;

        // Set entity name
        virtual void setEntityName(EntityHandle entity, const std::string& name) = 0;
    };

}
