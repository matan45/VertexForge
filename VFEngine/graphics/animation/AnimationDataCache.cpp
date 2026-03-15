#include "AnimationDataCache.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include <unordered_set>

namespace animation
{
    std::shared_ptr<animator::AnimatorData> AnimationDataCache::loadAnimatorData(const std::string& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        auto it = animatorDataCache.find(path);
        if (it != animatorDataCache.end())
        {
            return it->second;
        }

        auto data = resource::ResourceManager::loadAnimator(path);
        if (!data)
        {
            vfLogError("[AnimationDataCache] Failed to load animator: {}", path);
            return nullptr;
        }

        animatorDataCache[path] = data;
        return data;
    }

    const resource::AnimationData* AnimationDataCache::loadAnimation(const std::string& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        auto it = animationDataCache.find(path);
        if (it != animationDataCache.end())
        {
            return it->second.get();
        }

        auto future = resource::ResourceManager::loadAnimationAsync(path);
        auto animData = future.get();

        if (!animData)
        {
            vfLogError("[AnimationDataCache] Failed to load animation: {}", path);
            return nullptr;
        }

        animationDataCache[path] = animData;

        return animData.get();
    }

    const resource::SkeletonData* AnimationDataCache::loadSkeleton(const std::string& meshPath)
    {
        if (meshPath.empty())
        {
            return nullptr;
        }

        auto it = skeletonDataCache.find(meshPath);
        if (it != skeletonDataCache.end())
        {
            return it->second.get();
        }

        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            vfLogError("[AnimationDataCache] Failed to open mesh for skeleton: {}", meshPath);
            return nullptr;
        }

        if (!stream->hasSkeletonData())
        {
            skeletonDataCache[meshPath] = nullptr;
            vfLogWarning("[AnimationDataCache] Mesh has no skeleton data: {}", meshPath);
            return nullptr;
        }

        auto skeletonData = std::make_shared<resource::SkeletonData>();
        if (!stream->readSkeleton(*skeletonData))
        {
            skeletonDataCache[meshPath] = nullptr;
            vfLogError("[AnimationDataCache] Failed to read skeleton from: {}", meshPath);
            return nullptr;
        }

        skeletonDataCache[meshPath] = skeletonData;

        return skeletonData.get();
    }

    void AnimationDataCache::invalidateSkeleton(const std::string& meshPath)
    {
        skeletonDataCache.erase(meshPath);
    }

    void AnimationDataCache::clearSkeletons()
    {
        skeletonDataCache.clear();
    }

    void AnimationDataCache::clearAll()
    {
        animatorDataCache.clear();
        animationDataCache.clear();
        skeletonDataCache.clear();
    }

    void AnimationDataCache::cleanupUnused(const std::unordered_map<entt::entity, std::unique_ptr<AnimationLayerStack>>& animators)
    {
        std::unordered_set<std::string> usedAnimatorPaths;
        std::unordered_set<std::string> usedMeshPaths;
        std::unordered_set<std::string> usedAnimationPaths;

        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [entity, animator] : animators)
        {
            if (!animator)
            {
                continue;
            }

            if (registry.valid(entity) && registry.all_of<components::AnimatorComponent>(entity))
            {
                const auto& animComp = registry.get<components::AnimatorComponent>(entity);
                if (!animComp.animatorPath.empty())
                {
                    usedAnimatorPaths.insert(animComp.animatorPath);
                }
            }

            if (registry.valid(entity) && registry.all_of<components::MeshComponent>(entity))
            {
                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                if (!meshComp.meshPath.empty())
                {
                    usedMeshPaths.insert(meshComp.meshPath);
                }
            }

            const animator::AnimatorData* animData = animator->getAnimatorData();
            if (animData)
            {
                for (const auto& state : animData->graph.states)
                {
                    if (!state.animationPath.empty())
                    {
                        usedAnimationPaths.insert(state.animationPath);
                    }
                }
            }
        }

        size_t removedAnimators = 0;
        size_t removedAnimations = 0;
        size_t removedSkeletons = 0;

        for (auto it = animatorDataCache.begin(); it != animatorDataCache.end();)
        {
            if (usedAnimatorPaths.find(it->first) == usedAnimatorPaths.end())
            {
                it = animatorDataCache.erase(it);
                ++removedAnimators;
            }
            else
            {
                ++it;
            }
        }

        for (auto it = animationDataCache.begin(); it != animationDataCache.end();)
        {
            if (usedAnimationPaths.find(it->first) == usedAnimationPaths.end())
            {
                it = animationDataCache.erase(it);
                ++removedAnimations;
            }
            else
            {
                ++it;
            }
        }

        for (auto it = skeletonDataCache.begin(); it != skeletonDataCache.end();)
        {
            if (usedMeshPaths.find(it->first) == usedMeshPaths.end())
            {
                it = skeletonDataCache.erase(it);
                ++removedSkeletons;
            }
            else
            {
                ++it;
            }
        }

        if (removedAnimators > 0 || removedAnimations > 0 || removedSkeletons > 0)
        {
            vfLogInfo("Cleaned up {} animators, {} animations, {} skeletons",
                      removedAnimators, removedAnimations, removedSkeletons);
        }
    }
}
