#include "SocketAttachmentUpdater.hpp"
#include "AnimationDataCache.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "animator/SocketTypes.hpp"
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
        attachment.parentKind = components::SocketAttachmentComponent::ParentKind::Unknown;

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
        using ParentKind = components::SocketAttachmentComponent::ParentKind;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(attachedEntity) || !registry.all_of<components::SocketAttachmentComponent>(attachedEntity))
            return;
        auto& attachment = registry.get<components::SocketAttachmentComponent>(attachedEntity);

        if (!attachment.isActive || attachment.parentEntity == entt::null)
            return;

        if (!registry.valid(attachment.parentEntity))
            return;

        // VK-1432 fast path: a static-mesh parent's model-space socket offset is constant.
        // Once resolved we skip loadSkeleton/loadSockets (3 locked cache finds), the
        // meshRef.resolve() string copy, and the per-frame getLocalOffsetMatrix() recompute,
        // re-multiplying only by the (possibly moved) parentWorld. Reset via the existing
        // cachedSocketIndex/needsParentResolution invalidation channels.
        if (attachment.parentKind == ParentKind::Static && attachment.cachedSocketIndex >= 0)
        {
            applyModelOffset(attachedEntity, attachment.parentEntity, attachment.cachedStaticSocketOffset);
            return;
        }

        // resolve() returns a const std::string& into the MeshComponent; bind it without
        // copying. It stays valid for this call (no registry mutation of the parent here).
        const std::string* parentMeshPath = nullptr;
        const resource::SkeletonData* skeleton = nullptr;
        const std::vector<animator::SocketDefinition>* staticSockets = nullptr;
        if (registry.all_of<components::MeshComponent>(attachment.parentEntity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(attachment.parentEntity);
            if (meshComp.meshRef.isValid())
            {
                parentMeshPath = &meshComp.meshRef.resolve();
                skeleton = dataCache.loadSkeleton(*parentMeshPath);
                // Static (non-skeletal) parent: SOK2 sockets. loadSockets returns the
                // skeleton's sockets when skinned, so only use it on the static path.
                if (!skeleton)
                {
                    staticSockets = dataCache.loadSockets(*parentMeshPath);
                }
            }
        }

        const bool havePath = parentMeshPath != nullptr && !parentMeshPath->empty();

        int32_t socketIdx = attachment.cachedSocketIndex;
        if (socketIdx < 0 && skeleton)
        {
            socketIdx = skeleton->getSocketIndex(attachment.socketName);
            if (socketIdx < 0 && !attachment.socketName.empty() && havePath)
            {
                dataCache.invalidateSkeleton(*parentMeshPath);
                skeleton = dataCache.loadSkeleton(*parentMeshPath);
                if (skeleton)
                {
                    socketIdx = skeleton->getSocketIndex(attachment.socketName);
                }
            }
            attachment.cachedSocketIndex = socketIdx;
        }
        else if (socketIdx < 0 && staticSockets)
        {
            socketIdx = animator::indexOfSocket(*staticSockets, attachment.socketName);
            if (socketIdx < 0 && !attachment.socketName.empty() && havePath)
            {
                dataCache.invalidateSkeleton(*parentMeshPath);
                staticSockets = dataCache.loadSockets(*parentMeshPath);
                if (staticSockets)
                {
                    socketIdx = animator::indexOfSocket(*staticSockets, attachment.socketName);
                }
            }
            attachment.cachedSocketIndex = socketIdx;
        }

        // VK-1432: classify the parent once (independent of how the index was obtained — it may
        // have been pre-set by SocketAdapter::attachToSocket). A Static classification caches the
        // constant model-space offset so every later frame takes the fast path above. Only commit
        // on a positive resolve so a transient load failure leaves parentKind == Unknown to retry.
        if (attachment.parentKind == ParentKind::Unknown && socketIdx >= 0)
        {
            if (skeleton)
            {
                // Skinned parents read the per-frame socketTransformCache; never the static fast path.
                attachment.parentKind = ParentKind::Skinned;
            }
            else if (staticSockets && socketIdx < static_cast<int32_t>(staticSockets->size()))
            {
                attachment.parentKind = ParentKind::Static;
                attachment.cachedStaticSocketOffset = (*staticSockets)[socketIdx].getLocalOffsetMatrix();
            }
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

        applyModelOffset(attachedEntity, attachment.parentEntity, socketModelTransform);
    }

    void SocketAttachmentUpdater::applyModelOffset(entt::entity attachedEntity, entt::entity parentEntity,
                                                   const glm::mat4& socketModelTransform)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        glm::mat4 parentWorld = glm::mat4(1.0f);
        if (registry.all_of<components::WorldTransformComponent>(parentEntity))
        {
            parentWorld = registry.get<components::WorldTransformComponent>(parentEntity).worldMatrix;
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
