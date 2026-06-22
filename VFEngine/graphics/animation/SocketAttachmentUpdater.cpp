#include "SocketAttachmentUpdater.hpp"
#include "AnimationDataCache.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <glm/gtc/quaternion.hpp>

namespace animation
{
    namespace
    {
        // Linear name scan over a static socket vector; -1 if absent.
        int32_t findStaticSocketIndex(const std::vector<animator::SocketDefinition>& sockets,
                                      const std::string& socketName)
        {
            for (size_t i = 0; i < sockets.size(); ++i)
            {
                if (sockets[i].name == socketName)
                    return static_cast<int32_t>(i);
            }
            return -1;
        }
    }

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

        resolvedThisFrame.clear();
        inProgress.clear();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto attachmentView = registry.view<components::SocketAttachmentComponent>();
        for (auto attachedEntity : attachmentView)
        {
            if (!registry.valid(attachedEntity))
                continue;
            resolveChain(attachedEntity);
        }
    }

    void SocketAttachmentUpdater::resolveChain(entt::entity attachedEntity)
    {
        if (resolvedThisFrame.count(attachedEntity))
            return; // already applied this frame
        if (inProgress.count(attachedEntity))
            return; // cycle guard

        inProgress.insert(attachedEntity);

        auto& registry = scene::EntityRegistry::getRegistry();

        // Resolve THIS entity's parent (by name, if pending) BEFORE recursing/applying.
        // The view is visited in arbitrary order, so a chain whose child is reached before
        // its parent must still apply the parent against a resolved parentEntity this frame
        // — otherwise the parent would be marked resolved with a stale/unresolved parent.
        resolveAttachmentParent(attachedEntity);

        const auto* att = registry.try_get<components::SocketAttachmentComponent>(attachedEntity);
        if (att && att->isActive && att->parentEntity != entt::null &&
            registry.valid(att->parentEntity) &&
            registry.all_of<components::SocketAttachmentComponent>(att->parentEntity))
        {
            // Apply the parent first so its WorldTransform is fresh before we read it.
            resolveChain(att->parentEntity);
        }

        applyAttachmentTransform(attachedEntity);

        inProgress.erase(attachedEntity);
        resolvedThisFrame.insert(attachedEntity);
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
                if (meshComp.meshRef.isValid())
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

        std::string parentMeshPath;
        const resource::SkeletonData* skeleton = nullptr;
        const std::vector<animator::SocketDefinition>* staticSockets = nullptr;
        if (registry.all_of<components::MeshComponent>(attachment.parentEntity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(attachment.parentEntity);
            if (meshComp.meshRef.isValid())
            {
                parentMeshPath = meshComp.meshRef.resolve();
                skeleton = dataCache.loadSkeleton(parentMeshPath);
                // Static (non-skeletal) parent: SOK2 sockets. loadSockets returns the
                // skeleton's sockets when skinned, so only use it on the static path.
                if (!skeleton)
                {
                    staticSockets = dataCache.loadSockets(parentMeshPath);
                }
            }
        }

        int32_t socketIdx = attachment.cachedSocketIndex;
        if (socketIdx < 0 && skeleton)
        {
            socketIdx = skeleton->getSocketIndex(attachment.socketName);
            if (socketIdx < 0 && !attachment.socketName.empty() && !parentMeshPath.empty())
            {
                dataCache.invalidateSkeleton(parentMeshPath);
                skeleton = dataCache.loadSkeleton(parentMeshPath);
                if (skeleton)
                {
                    socketIdx = skeleton->getSocketIndex(attachment.socketName);
                }
            }
            auto& mutableAttachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);
            mutableAttachment.cachedSocketIndex = socketIdx;
        }
        else if (socketIdx < 0 && staticSockets)
        {
            socketIdx = findStaticSocketIndex(*staticSockets, attachment.socketName);
            if (socketIdx < 0 && !attachment.socketName.empty() && !parentMeshPath.empty())
            {
                dataCache.invalidateSkeleton(parentMeshPath);
                staticSockets = dataCache.loadSockets(parentMeshPath);
                if (staticSockets)
                {
                    socketIdx = findStaticSocketIndex(*staticSockets, attachment.socketName);
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
            socketModelTransform =
                glm::translate(glm::mat4(1.0f), boneMeshPos + socket.localPosition)
                * glm::mat4_cast(socket.localRotation);
        }
        else if (staticSockets && socketIdx < static_cast<int32_t>(staticSockets->size()))
        {
            // Static (non-skeletal) parent with no animator: the local offset matrix
            // is the model-space socket transform.
            socketModelTransform = (*staticSockets)[socketIdx].getLocalOffsetMatrix();
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
