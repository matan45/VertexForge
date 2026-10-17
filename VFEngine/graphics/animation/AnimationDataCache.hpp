#pragma once

#include "AnimationExport.hpp"
#include "animator/AnimatorTypes.hpp"
#include "asset/AssetGUID.hpp"
#include "resource/Types.hpp"
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace animation
{
#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_ANIMATION_API AnimationDataCache
    {
    private:
        std::unordered_map<std::string, std::shared_ptr<animator::AnimatorData>> animatorDataCache;
        std::unordered_map<std::string, std::shared_ptr<resource::AnimationData>> animationDataCache;
        std::unordered_map<std::string, std::shared_ptr<resource::SkeletonData>> skeletonDataCache;
        // Static-mesh sockets (VK-1427): meshes with no skeleton can still carry a SOK2
        // block. A present-nullptr is the negative cache (mesh has no socket block).
        std::unordered_map<std::string, std::shared_ptr<std::vector<animator::SocketDefinition>>> staticSocketCache;

        // Guards the three caches above. They are read/written concurrently by the parallel leader
        // animation evaluation (RuntimeAnimatorSystem::evaluateLeadersAndSync runs each leader's
        // update() on a worker thread, and update() can reach loadAnimation via the shared load
        // callback). shared_mutex keeps the steady-state cache-hit path concurrent (VK-1424).
        mutable std::shared_mutex cacheMutex;
    public:
        std::shared_ptr<animator::AnimatorData> loadAnimatorData(const std::string& path);
        const resource::AnimationData* loadAnimation(const std::string& path);
        const resource::SkeletonData* loadSkeleton(const std::string& meshPath);

        // Unified socket accessor (VK-1427): returns the skeletal sockets for a skinned
        // mesh, or the static SOK2 sockets for a non-skeletal mesh. nullptr if neither.
        const std::vector<animator::SocketDefinition>* loadSockets(const std::string& meshPath);

        // Drops the skeleton and static sockets of a mesh. Layer stacks keep raw pointers to the
        // skeleton: destroy every animator using it first.
        void invalidateSkeleton(const std::string& meshPath);

        // VK-1656: drops every cached animator whose key path resolves to `guid` (one asset can
        // be cached under several spellings of its path). Returns the dropped data so the caller
        // can keep it alive until no layer stack built from it remains (stacks hold raw pointers).
        std::vector<std::shared_ptr<animator::AnimatorData>> invalidateAnimatorData(const asset::AssetGUID& guid);

        void clearAll();

    private:
        // Reads the SOK2 block of a non-skeletal mesh into the static-socket cache.
        const std::vector<animator::SocketDefinition>* loadStaticSockets(const std::string& meshPath);
    };
#pragma warning(pop)
}
