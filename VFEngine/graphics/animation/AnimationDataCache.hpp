#pragma once

#include "animator/AnimatorTypes.hpp"
#include "resource/Types.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace animation
{
    class AnimationLayerStack;

    class AnimationDataCache
    {
    private:
        std::unordered_map<std::string, std::shared_ptr<animator::AnimatorData>> animatorDataCache;
        std::unordered_map<std::string, std::shared_ptr<resource::AnimationData>> animationDataCache;
        std::unordered_map<std::string, std::shared_ptr<resource::SkeletonData>> skeletonDataCache;
    public:
        std::shared_ptr<animator::AnimatorData> loadAnimatorData(const std::string& path);
        const resource::AnimationData* loadAnimation(const std::string& path);
        const resource::SkeletonData* loadSkeleton(const std::string& meshPath);

        void invalidateSkeleton(const std::string& meshPath);
        void clearSkeletons();
        void clearAll();

        void cleanupUnused(const std::unordered_map<entt::entity, std::unique_ptr<AnimationLayerStack>>& animators);
    };
}
