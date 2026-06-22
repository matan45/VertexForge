#include "AnimationDataCache.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "asset/AssetRef.hpp"
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

namespace animation
{
    std::shared_ptr<animator::AnimatorData> AnimationDataCache::loadAnimatorData(const std::string& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        {
            std::shared_lock readLock(cacheMutex);
            auto it = animatorDataCache.find(path);
            if (it != animatorDataCache.end())
            {
                return it->second;
            }
        }

        // Load outside the lock so concurrent cold loads of different assets don't serialize
        // (and so a blocking resource load never holds the cache lock).
        auto data = resource::ResourceManager::loadAnimator(asset::AssetRef::fromPath(path));
        if (!data)
        {
            vfLogError("[AnimationDataCache] Failed to load animator: {}", path);
            return nullptr;
        }

        // First-writer-wins: if another worker inserted meanwhile, drop our copy and return theirs.
        std::unique_lock writeLock(cacheMutex);
        auto [it, inserted] = animatorDataCache.try_emplace(path, std::move(data));
        return it->second;
    }

    const resource::AnimationData* AnimationDataCache::loadAnimation(const std::string& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        {
            std::shared_lock readLock(cacheMutex);
            auto it = animationDataCache.find(path);
            if (it != animationDataCache.end())
            {
                return it->second.get();
            }
        }

        // Load outside the lock (loadAnimationAsync().get() blocks) so the cache lock is never
        // held across a load and concurrent cold loads don't serialize.
        auto future = resource::ResourceManager::loadAnimationAsync(asset::AssetRef::fromPath(path));
        auto animData = future.get();

        if (!animData)
        {
            vfLogError("[AnimationDataCache] Failed to load animation: {}", path);
            return nullptr;
        }

        // First-writer-wins: re-check under the write lock; another worker may have inserted it.
        std::unique_lock writeLock(cacheMutex);
        auto [it, inserted] = animationDataCache.try_emplace(path, std::move(animData));
        return it->second.get();
    }

    const resource::SkeletonData* AnimationDataCache::loadSkeleton(const std::string& meshPath)
    {
        if (meshPath.empty())
        {
            return nullptr;
        }

        {
            // Key-presence is the hit test: a present nullptr is the negative cache for a mesh
            // with no skeleton, exactly as below.
            std::shared_lock readLock(cacheMutex);
            auto it = skeletonDataCache.find(meshPath);
            if (it != skeletonDataCache.end())
            {
                return it->second.get();
            }
        }

        // Read the skeleton outside the lock.
        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            // Transient open failure: do not cache (matches original behavior).
            vfLogError("[AnimationDataCache] Failed to open mesh for skeleton: {}", meshPath);
            return nullptr;
        }

        if (!stream->hasSkeletonData())
        {
            vfLogWarning("[AnimationDataCache] Mesh has no skeleton data: {}", meshPath);
            std::unique_lock writeLock(cacheMutex);
            auto [it, inserted] = skeletonDataCache.try_emplace(meshPath, nullptr);
            return it->second.get();
        }

        auto skeletonData = std::make_shared<resource::SkeletonData>();
        if (!stream->readSkeleton(*skeletonData))
        {
            vfLogError("[AnimationDataCache] Failed to read skeleton from: {}", meshPath);
            std::unique_lock writeLock(cacheMutex);
            auto [it, inserted] = skeletonDataCache.try_emplace(meshPath, nullptr);
            return it->second.get();
        }

        // First-writer-wins: a real skeleton inserted by another worker is kept over ours.
        std::unique_lock writeLock(cacheMutex);
        auto [it, inserted] = skeletonDataCache.try_emplace(meshPath, std::move(skeletonData));
        return it->second.get();
    }

    const std::vector<animator::SocketDefinition>* AnimationDataCache::loadSockets(const std::string& meshPath)
    {
        // Skinned mesh: skeleton.sockets is authoritative (return it whether empty or not).
        // loadSkeleton returns a present-nullptr for static meshes, so a null result here
        // means "not skinned" and we fall through to the static SOK2 path.
        const resource::SkeletonData* skeleton = loadSkeleton(meshPath);
        if (skeleton)
        {
            return &skeleton->sockets;
        }
        return loadStaticSockets(meshPath);
    }

    const std::vector<animator::SocketDefinition>* AnimationDataCache::loadStaticSockets(const std::string& meshPath)
    {
        if (meshPath.empty())
        {
            return nullptr;
        }

        {
            // Present-key is the hit test; a present nullptr is the negative cache.
            std::shared_lock readLock(cacheMutex);
            auto it = staticSocketCache.find(meshPath);
            if (it != staticSocketCache.end())
            {
                return it->second.get();
            }
        }

        // Read the SOK2 block outside the lock.
        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            // Transient open failure: do not cache.
            vfLogError("[AnimationDataCache] Failed to open mesh for static sockets: {}", meshPath);
            return nullptr;
        }

        if (!stream->hasSocketData())
        {
            std::unique_lock writeLock(cacheMutex);
            auto [it, inserted] = staticSocketCache.try_emplace(meshPath, nullptr);
            return it->second.get();
        }

        resource::SkeletonData tmp; // no bones -> socket.boneIndex resolves to -1 (static)
        if (!stream->readSockets(tmp))
        {
            vfLogError("[AnimationDataCache] Failed to read static sockets from: {}", meshPath);
            std::unique_lock writeLock(cacheMutex);
            auto [it, inserted] = staticSocketCache.try_emplace(meshPath, nullptr);
            return it->second.get();
        }

        auto sockets = std::make_shared<std::vector<animator::SocketDefinition>>(std::move(tmp.sockets));

        // First-writer-wins.
        std::unique_lock writeLock(cacheMutex);
        auto [it, inserted] = staticSocketCache.try_emplace(meshPath, std::move(sockets));
        return it->second.get();
    }

    void AnimationDataCache::invalidateSkeleton(const std::string& meshPath)
    {
        std::unique_lock writeLock(cacheMutex);
        skeletonDataCache.erase(meshPath);
        staticSocketCache.erase(meshPath);
    }

    void AnimationDataCache::clearSkeletons()
    {
        std::unique_lock writeLock(cacheMutex);
        skeletonDataCache.clear();
        staticSocketCache.clear();
    }

    void AnimationDataCache::clearAll()
    {
        std::unique_lock writeLock(cacheMutex);
        animatorDataCache.clear();
        animationDataCache.clear();
        skeletonDataCache.clear();
        staticSocketCache.clear();
    }

    void AnimationDataCache::cleanupUnused(const std::unordered_map<entt::entity, std::unique_ptr<AnimationLayerStack>>& animators)
    {
        // Mutates all three caches; serialize against the parallel loaders. cleanupUnused runs on
        // the main thread between frames today, so this lock is effectively uncontended.
        std::unique_lock writeLock(cacheMutex);

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
                if (animComp.animatorRef.isValid())
                {
                    usedAnimatorPaths.insert(animComp.animatorRef.resolve());
                }
            }

            if (registry.valid(entity) && registry.all_of<components::MeshComponent>(entity))
            {
                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                if (meshComp.meshRef.isValid())
                {
                    usedMeshPaths.insert(meshComp.meshRef.resolve());
                }
            }

            const animator::AnimatorData* animData = animator->getAnimatorData();
            if (animData)
            {
                for (const auto& state : animData->graph.states)
                {
                    if (state.animationRef.isValid())
                    {
                        usedAnimationPaths.insert(state.animationRef.resolve());
                    }
                }
            }
        }

        // Static-socket parents (VK-1427) have no animator, so they never appear in the
        // animator-derived usedMeshPaths above. Without seeding their mesh paths here,
        // every cleanup would evict the very staticSocketCache entries it was added for,
        // forcing a re-parse of the SOK2 block on the next socket query/attachment.
        for (auto entity : registry.view<components::SocketAttachmentComponent>())
        {
            const auto& att = registry.get<components::SocketAttachmentComponent>(entity);
            if (att.parentEntity != entt::null && registry.valid(att.parentEntity) &&
                registry.all_of<components::MeshComponent>(att.parentEntity))
            {
                const auto& meshComp = registry.get<components::MeshComponent>(att.parentEntity);
                if (meshComp.meshRef.isValid())
                {
                    usedMeshPaths.insert(meshComp.meshRef.resolve());
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

        for (auto it = staticSocketCache.begin(); it != staticSocketCache.end();)
        {
            if (usedMeshPaths.find(it->first) == usedMeshPaths.end())
            {
                it = staticSocketCache.erase(it);
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
