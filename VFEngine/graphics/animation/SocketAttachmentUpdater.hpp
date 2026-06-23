#pragma once

#include "AnimationExport.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace animation
{
    class AnimationLayerStack;
    class AnimationDataCache;

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_ANIMATION_API SocketAttachmentUpdater
    {
    public:
        SocketAttachmentUpdater(
            const std::unordered_map<entt::entity, std::unique_ptr<AnimationLayerStack>>& animators,
            AnimationDataCache& dataCache);

        void update();
        const std::vector<glm::mat4>* getCachedSocketTransforms(entt::entity entity) const;

    private:
        void buildSocketTransformCache();
        void resolveAttachmentParent(entt::entity attachedEntity);
        void applyAttachmentTransform(entt::entity attachedEntity);
        // Shared tail: compose final world = parentWorld * socketModelTransform * entityLocal.
        void applyModelOffset(entt::entity attachedEntity, entt::entity parentEntity,
                              const glm::mat4& socketModelTransform);
        // Depth-first, parent-first resolve so chained sockets converge in one frame.
        void resolveChain(entt::entity attachedEntity);

        const std::unordered_map<entt::entity, std::unique_ptr<AnimationLayerStack>>& animators;
        AnimationDataCache& dataCache;
        std::unordered_map<entt::entity, std::vector<glm::mat4>> socketTransformCache;
        std::unordered_set<entt::entity> resolvedThisFrame;
        std::unordered_set<entt::entity> inProgress;
    };
#pragma warning(pop)
}
