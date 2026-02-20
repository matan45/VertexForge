#include "SocketAdapter.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "../../services/events/SocketEvents.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../graphics/animation/RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"

namespace core
{
    namespace
    {
        template<typename Func>
        void withEntity(services::EntityHandle entity, Func&& func)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!services::internal::isValidHandle(entity, registry))
                return;
            func(services::internal::fromHandle(entity));
        }

        template<typename T, typename Func>
        T withEntityOr(services::EntityHandle entity, T defaultVal, Func&& func)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!services::internal::isValidHandle(entity, registry))
                return defaultVal;
            return func(services::internal::fromHandle(entity));
        }

        const resource::SkeletonData* getSkeletonForEntity(entt::entity entity)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!registry.all_of<components::MeshComponent>(entity))
                return nullptr;

            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            if (meshComp.meshPath.empty())
                return nullptr;

            auto stream = resource::MeshStreamResource::openStream(meshComp.meshPath);
            if (!stream || !stream->hasSkeletonData())
                return nullptr;

            // Use RuntimeAnimatorSystem's cached skeleton if available
            auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
            if (animator)
            {
                // The skeleton is stored in the system's cache — we can look it up
                // via the public loadSkeleton which is private... We'll read it fresh.
            }

            // Read skeleton data - this is a cached call in MeshStreamResource
            static thread_local resource::SkeletonData tempSkeleton;
            if (!stream->readSkeleton(tempSkeleton))
                return nullptr;

            return &tempSkeleton;
        }
    }

    SocketAdapter::SocketAdapter() = default;
    SocketAdapter::~SocketAdapter() = default;

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

        // Verify parent has a skeleton with the named socket
        const auto* skeleton = getSkeletonForEntity(parent);
        if (!skeleton)
        {
            vfLogWarning("[SocketAdapter] Parent entity has no skeleton data");
            return false;
        }

        int32_t socketIdx = skeleton->getSocketIndex(socketName);
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

        // Publish notification
        services::events::socket::SocketAttachmentChangedNotification notif;
        notif.childEntity = childEntity;
        notif.parentEntity = parentEntity;
        notif.socketName = socketName;
        notif.attached = true;
        events::EventDispatcher::instance().publish(notif);

        return true;
    }

    void SocketAdapter::detachFromSocket(services::EntityHandle childEntity)
    {
        withEntity(childEntity, [&](entt::entity child)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::SocketAttachmentComponent>(child))
            {
                const auto& attachment = registry.get<components::SocketAttachmentComponent>(child);
                auto parentHandle = services::internal::toHandle(attachment.parentEntity);

                registry.remove<components::SocketAttachmentComponent>(child);

                // Mark transform dirty so SceneGraphSystem recalculates
                if (registry.all_of<components::TransformComponent>(child))
                {
                    registry.get<components::TransformComponent>(child).isDirty = true;
                }

                services::events::socket::SocketAttachmentChangedNotification notif;
                notif.childEntity = childEntity;
                notif.parentEntity = parentHandle;
                notif.attached = false;
                events::EventDispatcher::instance().publish(notif);
            }
        });
    }

    void SocketAdapter::setSocketActive(services::EntityHandle entity, bool active)
    {
        withEntity(entity, [&](entt::entity e)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::SocketAttachmentComponent>(e))
            {
                registry.get<components::SocketAttachmentComponent>(e).isActive = active;
                if (registry.all_of<components::TransformComponent>(e))
                {
                    registry.get<components::TransformComponent>(e).isDirty = true;
                }
            }
        });
    }

    std::vector<std::string> SocketAdapter::getSocketNames(services::EntityHandle entity) const
    {
        return withEntityOr<std::vector<std::string>>(entity, {}, [](entt::entity e)
        {
            const auto* skeleton = getSkeletonForEntity(e);
            if (!skeleton)
                return std::vector<std::string>{};

            std::vector<std::string> names;
            names.reserve(skeleton->sockets.size());
            for (const auto& socket : skeleton->sockets)
            {
                names.push_back(socket.name);
            }
            return names;
        });
    }

    bool SocketAdapter::hasSocket(services::EntityHandle entity, const std::string& socketName) const
    {
        return withEntityOr<bool>(entity, false, [&socketName](entt::entity e)
        {
            const auto* skeleton = getSkeletonForEntity(e);
            return skeleton && skeleton->getSocketIndex(socketName) >= 0;
        });
    }

    bool SocketAdapter::isAttached(services::EntityHandle entity) const
    {
        return withEntityOr<bool>(entity, false, [](entt::entity e)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!registry.all_of<components::SocketAttachmentComponent>(e))
                return false;
            const auto& attachment = registry.get<components::SocketAttachmentComponent>(e);
            return attachment.isActive && attachment.parentEntity != entt::null;
        });
    }

    bool SocketAdapter::addSocketAttachmentComponent(services::EntityHandle entity)
    {
        return withEntityOr<bool>(entity, false, [](entt::entity e)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::SocketAttachmentComponent>(e))
                return false;
            registry.emplace<components::SocketAttachmentComponent>(e);
            return true;
        });
    }

    bool SocketAdapter::removeSocketAttachmentComponent(services::EntityHandle entity)
    {
        return withEntityOr<bool>(entity, false, [&entity](entt::entity e)
        {
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

            services::events::socket::SocketAttachmentChangedNotification notif;
            notif.childEntity = entity;
            notif.parentEntity = parentHandle;
            notif.attached = false;
            events::EventDispatcher::instance().publish(notif);
            return true;
        });
    }

    bool SocketAdapter::hasSocketAttachmentComponent(services::EntityHandle entity) const
    {
        return withEntityOr<bool>(entity, false, [](entt::entity e)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            return registry.all_of<components::SocketAttachmentComponent>(e);
        });
    }

    std::optional<services::events::socket::SocketAttachmentData>
    SocketAdapter::getSocketAttachmentData(services::EntityHandle entity) const
    {
        return withEntityOr<std::optional<services::events::socket::SocketAttachmentData>>(
            entity, std::nullopt, [](entt::entity e)
            -> std::optional<services::events::socket::SocketAttachmentData>
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!registry.all_of<components::SocketAttachmentComponent>(e))
                return std::nullopt;

            const auto& attachment = registry.get<components::SocketAttachmentComponent>(e);
            services::events::socket::SocketAttachmentData data;
            data.parentEntity = services::internal::toHandle(attachment.parentEntity);
            data.socketName = attachment.socketName;
            data.isActive = attachment.isActive;

            // Try to get parent entity name
            if (attachment.parentEntity != entt::null &&
                registry.valid(attachment.parentEntity) &&
                registry.all_of<components::NameComponent>(attachment.parentEntity))
            {
                data.parentEntityName = registry.get<components::NameComponent>(attachment.parentEntity).name;
            }

            return data;
        });
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
        return withEntityOr<glm::mat4>(parentEntity, glm::mat4(1.0f), [&socketName](entt::entity parent)
        {
            auto& registry = scene::EntityRegistry::getRegistry();

            const auto* skeleton = getSkeletonForEntity(parent);
            if (!skeleton)
                return glm::mat4(1.0f);

            int32_t socketIdx = skeleton->getSocketIndex(socketName);
            if (socketIdx < 0)
                return glm::mat4(1.0f);

            // Compute socket model-space transform via AnimatorStateMachine
            auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(parent);
            if (!animator || !animator->isInitialized())
                return glm::mat4(1.0f);

            std::vector<glm::mat4> socketTransforms;
            animator->computeSocketTransforms(skeleton->sockets, socketTransforms);

            if (socketIdx >= static_cast<int32_t>(socketTransforms.size()))
                return glm::mat4(1.0f);

            // Apply parent world transform
            glm::mat4 parentWorld = glm::mat4(1.0f);
            if (registry.all_of<components::WorldTransformComponent>(parent))
            {
                parentWorld = registry.get<components::WorldTransformComponent>(parent).worldMatrix;
            }

            return parentWorld * socketTransforms[socketIdx];
        });
    }
}
