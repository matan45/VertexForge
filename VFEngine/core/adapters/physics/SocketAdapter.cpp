#include "SocketAdapter.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "../../services/events/physics/SocketEvents.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../graphics/animation/RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

#include "print/Log.hpp"
namespace core
{
    SocketAdapter::SocketAdapter() = default;
    SocketAdapter::~SocketAdapter() = default;

    std::optional<entt::entity> SocketAdapter::resolveEntity(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!services::internal::isValidHandle(handle, registry))
            return std::nullopt;
        return services::internal::fromHandle(handle);
    }

    const resource::SkeletonData* SocketAdapter::getSkeletonForEntity(entt::entity entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::MeshComponent>(entity))
            return nullptr;

        const auto& meshComp = registry.get<components::MeshComponent>(entity);
        if (!meshComp.meshRef.isValid())
            return nullptr;

        return animation::RuntimeAnimatorSystem::instance().loadSkeleton(meshComp.meshRef.resolve());
    }

    const std::vector<animator::SocketDefinition>* SocketAdapter::getSocketsForEntity(entt::entity entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::MeshComponent>(entity))
            return nullptr;

        const auto& meshComp = registry.get<components::MeshComponent>(entity);
        if (!meshComp.meshRef.isValid())
            return nullptr;

        return animation::RuntimeAnimatorSystem::instance().loadSockets(meshComp.meshRef.resolve());
    }

    bool SocketAdapter::attachToSocket(services::EntityHandle childEntity, services::EntityHandle parentEntity,
                                       const std::string& socketName)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!services::internal::isValidHandle(childEntity, registry) ||
            !services::internal::isValidHandle(parentEntity, registry))
        {
            return false;
        }

        auto child = services::internal::fromHandle(childEntity);
        auto parent = services::internal::fromHandle(parentEntity);

        const auto* sockets = getSocketsForEntity(parent);
        if (!sockets || sockets->empty())
        {
            vfLogWarning("[SocketAdapter] Parent entity has no socket data");
            return false;
        }

        int32_t socketIdx = animator::indexOfSocket(*sockets, socketName);
        if (socketIdx < 0)
        {
            vfLogWarning("[SocketAdapter] Socket '{}' not found on parent entity", socketName);
            return false;
        }

        auto& attachment = registry.get_or_emplace<components::SocketAttachmentComponent>(child);
        attachment.parentEntity = parent;
        attachment.socketName = socketName;
        attachment.cachedSocketIndex = socketIdx;
        attachment.isActive = true;
        // Re-attach may reuse an existing component; clear the VK-1432 static-offset
        // classification so the updater re-resolves it for the new parent/socket.
        attachment.parentKind = components::SocketAttachmentComponent::ParentKind::Unknown;

        if (registry.all_of<components::NameComponent>(parent))
        {
            attachment.parentEntityName = registry.get<components::NameComponent>(parent).name;
        }

        // VK-1590: exact identity for cross-sector resolution. The else-branch matters —
        // re-attaching to a UUID-less parent must not leave the previous parent's UUID behind.
        if (const auto* uuidComp = registry.try_get<components::UUIDComponent>(parent))
        {
            attachment.parentEntityUUID = uuidComp->id.getValue();
        }
        else
        {
            attachment.parentEntityUUID = 0;
        }

        if (registry.all_of<components::TransformComponent>(child))
        {
            auto& transform = registry.get<components::TransformComponent>(child);

            const auto& socket = (*sockets)[socketIdx];
            glm::mat4 socketModel = socket.getLocalOffsetMatrix();
            // Skinned socket: pre-multiply by the bone's bind pose. Static sockets have
            // boneIndex == -1, so the offset matrix alone is the model-space transform.
            if (socket.boneIndex >= 0)
            {
                if (const auto* skeleton = getSkeletonForEntity(parent);
                    skeleton && socket.boneIndex < static_cast<int32_t>(skeleton->bindPoses.size()))
                {
                    socketModel = skeleton->bindPoses[socket.boneIndex] * socketModel;
                }
            }

            glm::mat4 parentWorld = glm::mat4(1.0f);
            if (registry.all_of<components::WorldTransformComponent>(parent))
            {
                parentWorld = registry.get<components::WorldTransformComponent>(parent).worldMatrix;
            }

            transform.position = glm::vec3(parentWorld * socketModel * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            transform.isDirty = true;
        }

        events::socket::SocketAttachmentChangedNotification notif;
        notif.childEntity = childEntity;
        notif.parentEntity = parentEntity;
        notif.socketName = socketName;
        notif.attached = true;
        events::EventDispatcher::instance().publish(notif);

        return true;
    }

    void SocketAdapter::detachFromSocket(services::EntityHandle childEntity)
    {
        auto resolved = resolveEntity(childEntity);
        if (!resolved) return;
        auto child = *resolved;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::SocketAttachmentComponent>(child))
            return;

        const auto& attachment = registry.get<components::SocketAttachmentComponent>(child);
        auto parentHandle = services::internal::toHandle(attachment.parentEntity);
        std::string socketName = attachment.socketName;

        registry.remove<components::SocketAttachmentComponent>(child);

        if (registry.all_of<components::TransformComponent>(child))
        {
            registry.get<components::TransformComponent>(child).isDirty = true;
        }

        events::socket::SocketAttachmentChangedNotification notif;
        notif.childEntity = childEntity;
        notif.parentEntity = parentHandle;
        notif.socketName = std::move(socketName);
        notif.attached = false;
        events::EventDispatcher::instance().publish(notif);
    }

    void SocketAdapter::setSocketActive(services::EntityHandle entity, bool active)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return;
        auto e = *resolved;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.all_of<components::SocketAttachmentComponent>(e))
        {
            registry.get<components::SocketAttachmentComponent>(e).isActive = active;
            if (registry.all_of<components::TransformComponent>(e))
            {
                registry.get<components::TransformComponent>(e).isDirty = true;
            }
        }
    }

    std::vector<std::string> SocketAdapter::getSocketNames(services::EntityHandle entity) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return {};

        const auto* sockets = getSocketsForEntity(*resolved);
        if (!sockets) return {};

        std::vector<std::string> names;
        names.reserve(sockets->size());
        for (const auto& socket : *sockets)
        {
            names.push_back(socket.name);
        }
        return names;
    }

    bool SocketAdapter::hasSocket(services::EntityHandle entity, const std::string& socketName) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        const auto* sockets = getSocketsForEntity(*resolved);
        return sockets && animator::indexOfSocket(*sockets, socketName) >= 0;
    }

    bool SocketAdapter::isAttached(services::EntityHandle entity) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::SocketAttachmentComponent>(*resolved))
            return false;
        const auto& attachment = registry.get<components::SocketAttachmentComponent>(*resolved);
        return attachment.isActive && attachment.parentEntity != entt::null;
    }

    bool SocketAdapter::addSocketAttachmentComponent(services::EntityHandle entity)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.all_of<components::SocketAttachmentComponent>(*resolved))
            return false;
        registry.emplace<components::SocketAttachmentComponent>(*resolved);
        return true;
    }

    bool SocketAdapter::removeSocketAttachmentComponent(services::EntityHandle entity)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;
        auto e = *resolved;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::SocketAttachmentComponent>(e))
            return false;

        const auto& attachment = registry.get<components::SocketAttachmentComponent>(e);
        auto parentHandle = services::internal::toHandle(attachment.parentEntity);

        registry.remove<components::SocketAttachmentComponent>(e);

        if (registry.all_of<components::TransformComponent>(e))
        {
            registry.get<components::TransformComponent>(e).isDirty = true;
        }

        events::socket::SocketAttachmentChangedNotification notif;
        notif.childEntity = entity;
        notif.parentEntity = parentHandle;
        notif.attached = false;
        events::EventDispatcher::instance().publish(notif);
        return true;
    }

    bool SocketAdapter::hasSocketAttachmentComponent(services::EntityHandle entity) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        return registry.all_of<components::SocketAttachmentComponent>(*resolved);
    }

    bool SocketAdapter::addSocketOverrideComponent(services::EntityHandle entity)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.all_of<components::SocketOverrideComponent>(*resolved))
            return false;
        registry.emplace<components::SocketOverrideComponent>(*resolved);
        return true;
    }

    bool SocketAdapter::removeSocketOverrideComponent(services::EntityHandle entity)
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::SocketOverrideComponent>(*resolved))
            return false;
        registry.remove<components::SocketOverrideComponent>(*resolved);
        return true;
    }

    bool SocketAdapter::hasSocketOverrideComponent(services::EntityHandle entity) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        return registry.all_of<components::SocketOverrideComponent>(*resolved);
    }

    std::optional<events::socket::SocketAttachmentData>
    SocketAdapter::getSocketAttachmentData(services::EntityHandle entity) const
    {
        auto resolved = resolveEntity(entity);
        if (!resolved) return std::nullopt;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::SocketAttachmentComponent>(*resolved))
            return std::nullopt;

        const auto& attachment = registry.get<components::SocketAttachmentComponent>(*resolved);
        events::socket::SocketAttachmentData data;
        data.parentEntity = services::internal::toHandle(attachment.parentEntity);
        data.socketName = attachment.socketName;
        data.isActive = attachment.isActive;

        if (attachment.parentEntity != entt::null &&
            registry.valid(attachment.parentEntity) &&
            registry.all_of<components::NameComponent>(attachment.parentEntity))
        {
            data.parentEntityName = registry.get<components::NameComponent>(attachment.parentEntity).name;
        }

        return data;
    }

    glm::vec3 SocketAdapter::getSocketWorldPosition(services::EntityHandle parentEntity,
                                                     const std::string& socketName) const
    {
        glm::mat4 transform = getSocketWorldTransform(parentEntity, socketName);
        return glm::vec3(transform[3]);
    }

    glm::mat4 SocketAdapter::getSocketWorldTransform(services::EntityHandle parentEntity,
                                                      const std::string& socketName) const
    {
        auto resolved = resolveEntity(parentEntity);
        if (!resolved) return glm::mat4(1.0f);
        auto parent = *resolved;

        auto& registry = scene::EntityRegistry::getRegistry();

        auto& animSystem = animation::RuntimeAnimatorSystem::instance();

        // Discriminator: a mesh WITH a skeleton is skinned — its sockets resolve through
        // animated bone transforms (the original path, verbatim). A mesh with NO skeleton
        // is static and resolves directly from its SOK2 sockets (VK-1427). Keying on
        // skeleton presence (not animator state) keeps the skinned path bit-identical to
        // the original even on the first frame before the animator initializes.
        const auto* skeleton = getSkeletonForEntity(parent);
        if (skeleton)
        {
            int32_t socketIdx = skeleton->getSocketIndex(socketName);
            if (socketIdx < 0)
                return glm::mat4(1.0f);

            const std::vector<glm::mat4>* socketTransforms = animSystem.getCachedSocketTransforms(parent);
            std::vector<glm::mat4> computedTransforms;
            if (!socketTransforms)
            {
                auto* animator = animSystem.getAnimator(parent);
                if (!animator || !animator->isInitialized())
                    return glm::mat4(1.0f);
                animator->computeSocketTransforms(skeleton->sockets, computedTransforms);
                socketTransforms = &computedTransforms;
            }

            if (socketIdx >= static_cast<int32_t>(socketTransforms->size()))
                return glm::mat4(1.0f);

            glm::mat4 parentWorld = glm::mat4(1.0f);
            if (registry.all_of<components::WorldTransformComponent>(parent))
            {
                parentWorld = registry.get<components::WorldTransformComponent>(parent).worldMatrix;
            }

            return parentWorld * (*socketTransforms)[socketIdx];
        }

        // Static mesh: model-space socket transform is the local offset matrix.
        const auto* sockets = getSocketsForEntity(parent);
        if (!sockets)
            return glm::mat4(1.0f);

        int32_t socketIdx = animator::indexOfSocket(*sockets, socketName);
        if (socketIdx < 0)
            return glm::mat4(1.0f);

        glm::mat4 parentWorld = glm::mat4(1.0f);
        if (registry.all_of<components::WorldTransformComponent>(parent))
        {
            parentWorld = registry.get<components::WorldTransformComponent>(parent).worldMatrix;
        }

        return parentWorld * (*sockets)[socketIdx].getLocalOffsetMatrix();
    }
}
