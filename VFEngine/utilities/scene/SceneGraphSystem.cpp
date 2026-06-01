#include "SceneGraphSystem.hpp"
#include "../print/Log.hpp"
#include "../components/MediaComponents.hpp"
#include "../threading/JobSystem.hpp"

namespace scene
{
    SceneGraphSystem::SceneGraphSystem() : root{Entity("Root")}
    {
    }

    entt::entity SceneGraphSystem::addChild(Entity& parent, Entity& child) const
    {
        if (!parent.isAlive() || !child.isAlive())
        {
            vfLogError("addChild: parent or child entity is not alive.");
            return entt::null;
        }

        parent.addChildren(child);
        markTransformDirty(child);

        return child.getHandle();
    }

    void SceneGraphSystem::removeEntity(Entity& entity)
    {
        if (!entity.isAlive() || entity == root)
        {
            vfLogError("Cannot remove the root entity or an invalid entity.");
            return;
        }

        auto children = entity.getChildren();
        for (auto& child : children)
        {
            if (child.isAlive())
            {
                removeEntity(child);
            }
        }

        Entity parent = entity.getParent();
        if (parent.isAlive())
        {
            parent.removeChildren(entity);
        }

        EntityRegistry::getRegistry().destroy(entity.getHandle());
    }

    void SceneGraphSystem::clearScene()
    {
        if (!root.isValid())
        {
            vfLogError("Root entity is invalid, cannot clear scene.");
            return;
        }

        auto children = root.getChildren();
        for (auto& child : children)
        {
            removeEntity(child);
        }

        root.removeAllOptionalComponents();

        vfLogInfo("Scene cleared successfully.");
    }

    void SceneGraphSystem::moveEntity(Entity& entity, Entity& newParent) const
    {
        if (isDescendant(entity, newParent))
        {
            vfLogError("Invalid entity or parent.");
            return;
        }

        Entity oldParent = entity.getParent();
        if (oldParent.isValid())
        {
            oldParent.removeChildren(entity);
        }

        newParent.addChildren(entity);
        markTransformDirty(entity);
    }

    std::vector<scene::Entity> SceneGraphSystem::findAllEntitiesByName(std::string_view name) const
    {
        std::vector<Entity> foundEntities;

        auto view = EntityRegistry::getRegistry().view<components::NameComponent>();

        for (auto entityHandle : view)
        {
            const auto& entityName = view.get<components::NameComponent>(entityHandle).name;
            if (entityName == name)
            {
                foundEntities.emplace_back(entityHandle);
            }
        }

        return foundEntities;
    }

    void SceneGraphSystem::updateWorldTransforms()
    {
        if (!root.isAlive())
        {
            return;
        }
        glm::mat4 identityMatrix(1.0f);
        {
            auto& registry = EntityRegistry::getRegistry();
            auto view = registry.view<components::TransformComponent>(
                entt::exclude<components::WorldTransformComponent>);
            for (auto entity : view)
            {
                registry.emplace<components::WorldTransformComponent>(entity);
            }
        }
        // Parallelize top-level children: each subtree is independent and disjoint —
        // no entity belongs to two subtrees. Each thread reads/writes only its own subtree's
        // components (via entity handles, not registry iteration), so there is no concurrent
        // access to the same component instances. No structural registry mutations occur here
        // (emplace is done in the pre-pass above, single-threaded).
        // Note: EnTT stores components contiguously, so adjacent entities from different
        // subtrees may share cache lines (false sharing). Only parallelize when there are
        // enough children for the work to outweigh the overhead.
        auto children = root.getChildren();
        constexpr size_t PARALLEL_THRESHOLD = 4;
        if (children.size() >= PARALLEL_THRESHOLD)
        {
            std::vector<std::future<void>> futures;
            futures.reserve(children.size());

            for (auto& child : children)
            {
                if (child.isAlive())
                {
                    futures.push_back(threading::JobSystem::instance().submit(
                        [this, child, identityMatrix]() mutable
                        {
                            updateChildWorldTransforms(child, identityMatrix);
                        }, threading::JobPriority::HIGH
                    ));
                }
            }

            for (auto& f : futures)
            {
                f.get();
            }
        }
        else
        {
            updateChildWorldTransforms(root, identityMatrix);
        }
    }

    void SceneGraphSystem::updateCamera() const
    {
        auto view = EntityRegistry::getRegistry().view<components::CameraComponent, components::TransformComponent>();

        for (auto entityHandle : view)
        {
            auto entity = Entity(entityHandle);
            auto& camera = entity.getComponent<components::CameraComponent>();

            const auto& transform = entity.getComponent<components::TransformComponent>();
            if (entity.hasComponent<components::WorldTransformComponent>())
            {
                // World-space eye position with the LOCAL Euler rotation: inverting the world matrix
                // (or decomposing it to Euler) bakes in the object X·Y·Z order / extractEulerAngleXYZ
                // yaw singularity, flipping a yawing fixed-pitch camera to the sky past ±90° (VK-1350).
                const auto& worldTransform = entity.getComponent<components::WorldTransformComponent>();
                camera.updateViewMatrixFromWorldEye(worldTransform.worldMatrix, transform.rotation);
            }
            else
            {
                camera.updateViewMatrix(transform.position, transform.rotation);
            }
        }
    }

    void SceneGraphSystem::markTransformDirty(Entity& entity) const
    {
        if (!entity.isAlive()) return;
        if (entity.hasComponent<components::TransformComponent>())
        {
            entity.getComponent<components::TransformComponent>().isDirty = true;
        }
        for (auto& child : entity.getChildren())
        {
            if (child.isAlive())
            {
                markTransformDirty(child);
            }
        }
    }

    void SceneGraphSystem::updateChildWorldTransforms(Entity& entity, const glm::mat4& parentWorldTransform, bool parentDirty)
    {
        if (!entity.isAlive())
        {
            return;
        }
        if (!entity.hasComponent<components::TransformComponent>())
        {
            return;
        }

        // Skip entities whose transform is driven by the socket attachment system
        if (entity.hasComponent<components::SocketAttachmentComponent>())
        {
            const auto& attachment = entity.getComponent<components::SocketAttachmentComponent>();
            if (attachment.isActive && attachment.parentEntity != entt::null)
            {
                // WorldTransformComponent is guaranteed by the pre-pass
                glm::mat4 worldMatrix = entity.getComponent<components::WorldTransformComponent>().worldMatrix;
                for (auto& child : entity.getChildren())
                {
                    if (child.isAlive())
                    {
                        updateChildWorldTransforms(child, worldMatrix, false);
                    }
                }
                return;
            }
        }

        auto& transform = entity.getComponent<components::TransformComponent>();
        // WorldTransformComponent is guaranteed by the pre-pass in updateWorldTransforms()
        auto& worldTransform = entity.getComponent<components::WorldTransformComponent>();
        glm::mat4 worldMatrix;

        bool dirty = transform.isDirty || parentDirty;

        if (dirty)
        {
            worldMatrix = parentWorldTransform * transform.getMatrix();
            worldTransform.worldMatrix = worldMatrix;
            transform.isDirty = false;
        }
        else
        {
            worldMatrix = worldTransform.worldMatrix;
        }

        for (auto& child : entity.getChildren())
        {
            if (child.isAlive())
            {
                updateChildWorldTransforms(child, worldMatrix, dirty);
            }
        }
    }

    bool SceneGraphSystem::isDescendant(scene::Entity& parent, scene::Entity& child) const
    {
        if (!parent.isValid() || !child.isValid())
        {
            return false;
        }

        for (auto& childEntity : parent.getChildren())
        {
            if (childEntity == child || isDescendant(childEntity, child))
            {
                return true;
            }
        }

        return false;
    }
}
