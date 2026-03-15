#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace animation
{
    class AnimationLayerStack;
    class AnimationDataCache;

    class SocketAttachmentUpdater
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

        const std::unordered_map<entt::entity, std::unique_ptr<AnimationLayerStack>>& animators;
        AnimationDataCache& dataCache;
        std::unordered_map<entt::entity, std::vector<glm::mat4>> socketTransformCache;
    };
}
