#pragma once

#include "AnimatorStateMachine.hpp"
#include "animator/AnimatorTypes.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <string>

namespace animation
{
    /**
     * RuntimeAnimatorSystem manages all AnimatorStateMachine instances for entities.
     *
     * Responsibilities:
     * - Creates/destroys animators when entities gain/lose animatorPath
     * - Updates all active animators each frame
     * - Provides access to animators by entity for CQRS event handlers
     * - Loads animator data and animations via ResourceManager
     *
     * Thread Safety: This class is NOT thread-safe. All methods must be called from the main thread.
     * The system is updated during the render phase via FramePreparationSystem which runs on the main thread.
     */
    class RuntimeAnimatorSystem
    {
    public:
        static RuntimeAnimatorSystem& instance();

        // Lifecycle
        void initialize();
        void shutdown();

        // Frame update - call once per frame with delta time
        void updateAll(float deltaTime);

        // Entity animator management
        void initializeEntityAnimator(entt::entity entity, const std::string& animatorPath);
        void destroyEntityAnimator(entt::entity entity);
        bool hasAnimator(entt::entity entity) const;

        // Get animator for entity (returns nullptr if not found)
        AnimatorStateMachine* getAnimator(entt::entity entity);
        const AnimatorStateMachine* getAnimator(entt::entity entity) const;

        // Scan all entities with MeshComponent and initialize animators as needed
        void syncWithRegistry();

        // Clear all animators (e.g., when scene is unloaded)
        void clearAll();

        // Clear animator instances only, keep caches (e.g., when entering play mode)
        void clearAnimatorInstances();

        // Remove cache entries not used by any active animator
        // Call periodically or on scene load to prevent unbounded memory growth
        void cleanupUnusedCaches();

    private:
        RuntimeAnimatorSystem() = default;
        ~RuntimeAnimatorSystem() = default;
        RuntimeAnimatorSystem(const RuntimeAnimatorSystem&) = delete;
        RuntimeAnimatorSystem& operator=(const RuntimeAnimatorSystem&) = delete;

        // Animation loading callback for AnimatorStateMachine
        const resource::AnimationData* loadAnimation(const std::string& path);

        // Load skeleton from mesh file
        const resource::SkeletonData* loadSkeleton(const std::string& meshPath);

        // IMPORTANT: Member destruction order is reverse of declaration order.
        // Caches must be declared BEFORE animators so they are destroyed AFTER.
        // AnimatorStateMachine holds raw pointers to cached data, so caches must outlive animators.

        // Cache: animator path -> loaded animator data (keeps shared_ptr alive)
        std::unordered_map<std::string, std::shared_ptr<animator::AnimatorData>> animatorDataCache;

        // Cache: animation path -> loaded animation data (keeps shared_ptr alive)
        std::unordered_map<std::string, std::shared_ptr<resource::AnimationData>> animationDataCache;

        // Cache: mesh path -> skeleton data (for animation playback)
        std::unordered_map<std::string, std::shared_ptr<resource::SkeletonData>> skeletonDataCache;

        // Storage: entity -> animator instance (must be declared AFTER caches for correct destruction order)
        std::unordered_map<entt::entity, std::unique_ptr<AnimatorStateMachine>> animators;

        // Event subscription tokens
        events::SubscriptionToken meshDataChangedToken;
        events::SubscriptionToken editorModeChangedToken;

        bool initialized = false;
        bool pendingCacheCleanup = false;  // Set after mode change to trigger cleanup on next sync
    };
}
