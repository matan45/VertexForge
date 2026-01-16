#pragma once

#include "AnimatorStateMachine.hpp"
#include "animator/AnimatorTypes.hpp"
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

    private:
        RuntimeAnimatorSystem() = default;
        ~RuntimeAnimatorSystem() = default;
        RuntimeAnimatorSystem(const RuntimeAnimatorSystem&) = delete;
        RuntimeAnimatorSystem& operator=(const RuntimeAnimatorSystem&) = delete;

        // Animation loading callback for AnimatorStateMachine
        const resource::AnimationData* loadAnimation(const std::string& path);

        // Storage: entity -> animator instance
        std::unordered_map<entt::entity, std::unique_ptr<AnimatorStateMachine>> animators;

        // Cache: animator path -> loaded animator data
        std::unordered_map<std::string, std::unique_ptr<animator::AnimatorData>> animatorDataCache;

        bool initialized = false;
    };
}
