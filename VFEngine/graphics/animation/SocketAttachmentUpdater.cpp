#include "SocketAttachmentUpdater.hpp"
#include "AnimationDataCache.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <glm/gtc/quaternion.hpp>

namespace animation
{
    SocketAttachmentUpdater::SocketAttachmentUpdater(
        const std::unordered_map<entt::entity, std::unique_ptr<AnimationLayerStack>>& animators,
        AnimationDataCache& dataCache)
        : animators(animators)
        , dataCache(dataCache)
    {
    }

    void SocketAttachmentUpdater::update()
    {
        buildSocketTransformCache();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto attachmentView = registry.view<components::SocketAttachmentComponent>();
        for (auto attachedEntity : attachmentView)
        {
            if (!registry.valid(attachedEntity))
                continue;
            resolveAttachmentParent(attachedEntity);
            applyAttachmentTransform(attachedEntity);
        }
    }

    const std::vector<glm::mat4>* SocketAttachmentUpdater::getCachedSocketTransforms(entt::entity entity) const
    {
        auto it = socketTransformCache.find(entity);
        if (it != socketTransformCache.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    void SocketAttachmentUpdater::buildSocketTransformCache()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        socketTransformCache.clear();
        for (auto& [entity, animator] : animators)
        {
            if (!animator || !animator->isInitialized())
                continue;

            if (!registry.valid(entity))
                continue;

            const resource::SkeletonData* skeleton = nullptr;
            if (registry.all_of<components::MeshComponent>(entity))
            {
                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                if (!!meshComp.meshRef.isValid())
                {
                    skeleton = dataCache.loadSkeleton(meshComp.meshRef.resolve());
                }
            }

            if (!skeleton || skeleton->sockets.empty())
                continue;

            auto& transforms = socketTransformCache[entity];
            animator->computeSocketTransforms(skeleton->sockets, transforms);
        }
    }

    void SocketAttachmentUpdater::resolveAttachmentParent(entt::entity attachedEntity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(attachedEntity) || !registry.all_of<components::SocketAttachmentComponent>(attachedEntity))
        {
            return;
        }
        auto& attachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);

        if (!attachment.needsParentResolution || attachment.parentEntityName.empty())
            return;

        attachment.needsParentResolution = false;
        attachment.parentEntity = entt::null;
        attachment.cachedSocketIndex = -1;

        entt::entity ancestor = entt::null;
        if (registry.all_of<components::ParentComponent>(attachedEntity))
        {
            ancestor = registry.get<components::ParentComponent>(attachedEntity).parent;
        }
        while (ancestor != entt::null && registry.valid(ancestor))
        {
            if (registry.all_of<components::NameComponent>(ancestor) &&
                registry.get<components::NameComponent>(ancestor).name == attachment.parentEntityName)
            {
                attachment.parentEntity = ancestor;
                break;
            }
            if (registry.all_of<components::ParentComponent>(ancestor))
                ancestor = registry.get<components::ParentComponent>(ancestor).parent;
            else
                break;
        }

        if (attachment.parentEntity == entt::null)
        {
            auto nameView = registry.view<components::NameComponent>();
            for (auto candidate : nameView)
            {
                if (nameView.get<components::NameComponent>(candidate).name == attachment.parentEntityName)
                {
                    attachment.parentEntity = candidate;
                    break;
                }
            }
        }
    }

    void SocketAttachmentUpdater::applyAttachmentTransform(entt::entity attachedEntity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(attachedEntity) || !registry.all_of<components::SocketAttachmentComponent>(attachedEntity))
            return;
        auto& attachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);

        if (!attachment.isActive || attachment.parentEntity == entt::null)
            return;

        if (!registry.valid(attachment.parentEntity))
            return;

        const resource::SkeletonData* skeleton = nullptr;
        if (registry.all_of<components::MeshComponent>(attachment.parentEntity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(attachment.parentEntity);
            if (!!meshComp.meshRef.isValid())
            {
                skeleton = dataCache.loadSkeleton(meshComp.meshRef.resolve());
            }
        }

        int32_t socketIdx = attachment.cachedSocketIndex;
        if (socketIdx < 0 && skeleton)
        {
            socketIdx = skeleton->getSocketIndex(attachment.socketName);
            if (socketIdx < 0 && !attachment.socketName.empty() &&
                registry.all_of<components::MeshComponent>(attachment.parentEntity))
            {
                const auto& meshComp = registry.get<components::MeshComponent>(attachment.parentEntity);
                dataCache.invalidateSkeleton(meshComp.meshRef.resolve());
                skeleton = dataCache.loadSkeleton(meshComp.meshRef.resolve());
                if (skeleton)
                {
                    socketIdx = skeleton->getSocketIndex(attachment.socketName);
                }
            }
            auto& mutableAttachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);
            mutableAttachment.cachedSocketIndex = socketIdx;
        }

        if (socketIdx < 0)
            return;

        glm::mat4 socketModelTransform = glm::mat4(1.0f);

        auto cacheIt = socketTransformCache.find(attachment.parentEntity);
        if (cacheIt != socketTransformCache.end() &&
            socketIdx < static_cast<int32_t>(cacheIt->second.size()))
        {
            socketModelTransform = cacheIt->second[socketIdx];
        }
        else if (skeleton && socketIdx < static_cast<int32_t>(skeleton->sockets.size()))
        {
            const auto& socket = skeleton->sockets[socketIdx];
            glm::vec3 boneMeshPos(0.0f);
            if (socket.boneIndex >= 0 &&
                socket.boneIndex < static_cast<int32_t>(skeleton->bindPoses.size()))
            {
                boneMeshPos = glm::vec3(
                    skeleton->globalInverseTransform
                    * skeleton->bindPoses[socket.boneIndex]
                    * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            }
            socketModelTransform = glm::translate(glm::mat4(1.0f),
                boneMeshPos + socket.localPosition);
        }
        else
        {
            return;
        }

        glm::mat4 parentWorld = glm::mat4(1.0f);
        if (registry.all_of<components::WorldTransformComponent>(attachment.parentEntity))
        {
            parentWorld = registry.get<components::WorldTransformComponent>(attachment.parentEntity).worldMatrix;
        }

        glm::mat4 socketWorld = parentWorld * socketModelTransform;

        glm::mat4 entityLocal = glm::mat4(1.0f);
        if (registry.all_of<components::TransformComponent>(attachedEntity))
        {
            const auto& transform = registry.get<components::TransformComponent>(attachedEntity);
            glm::mat4 rot = glm::mat4_cast(glm::quat(glm::radians(transform.rotation)));
            entityLocal = rot * glm::scale(glm::mat4(1.0f), transform.scale);
        }

        glm::mat4 finalWorld = socketWorld * entityLocal;

        registry.get_or_emplace<components::WorldTransformComponent>(attachedEntity).worldMatrix = finalWorld;

        if (registry.all_of<components::TransformComponent>(attachedEntity))
        {
            auto& transform = registry.get<components::TransformComponent>(attachedEntity);
            transform.position = glm::vec3(finalWorld[3]);
            transform.isDirty = false;
        }
    }
}
