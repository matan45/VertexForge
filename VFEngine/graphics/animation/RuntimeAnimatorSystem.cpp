#include "RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "print/EditorLogger.hpp"

namespace animation
{
    RuntimeAnimatorSystem& RuntimeAnimatorSystem::instance()
    {
        static RuntimeAnimatorSystem inst;
        return inst;
    }

    void RuntimeAnimatorSystem::initialize()
    {
        if (initialized)
        {
            return;
        }
        initialized = true;
        vfLogInfo("[RuntimeAnimatorSystem] Initialized");
    }

    void RuntimeAnimatorSystem::shutdown()
    {
        clearAll();
        initialized = false;
        vfLogInfo("[RuntimeAnimatorSystem] Shutdown");
    }

    void RuntimeAnimatorSystem::updateAll(float deltaTime)
    {
        for (auto& [entity, animator] : animators)
        {
            if (animator && animator->isInitialized() && animator->isPlaying())
            {
                animator->update(deltaTime);
            }
        }
    }

    void RuntimeAnimatorSystem::initializeEntityAnimator(entt::entity entity, const std::string& animatorPath)
    {
        if (animatorPath.empty())
        {
            return;
        }

        // Check if already initialized with same path
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            // Already has an animator - check if path changed
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::AnimatorComponent>(entity))
            {
                auto& animComp = registry.get<components::AnimatorComponent>(entity);
                if (animComp.animatorPath == animatorPath && animComp.isInitialized)
                {
                    return; // Already initialized with same path
                }
            }
            // Path changed - destroy old animator
            destroyEntityAnimator(entity);
        }

        // Load animator data
        auto animatorData = resource::ResourceManager::loadAnimator(animatorPath);
        if (!animatorData)
        {
            vfLogError("[RuntimeAnimatorSystem] Failed to load animator: {}", animatorPath);
            return;
        }

        // Create state machine
        auto stateMachine = std::make_unique<AnimatorStateMachine>();

        // Initialize with animation load callback
        stateMachine->initialize(*animatorData, [this](const std::string& path) -> const resource::AnimationData*
        {
            return loadAnimation(path);
        });

        // Store in map
        AnimatorStateMachine* rawPtr = stateMachine.get();
        animators[entity] = std::move(stateMachine);

        // Update AnimatorComponent on entity
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::AnimatorComponent>(entity))
        {
            registry.emplace<components::AnimatorComponent>(entity);
        }

        auto& animComp = registry.get<components::AnimatorComponent>(entity);
        animComp.stateMachine = rawPtr;
        animComp.animatorPath = animatorPath;
        animComp.isInitialized = true;

        vfLogInfo("[RuntimeAnimatorSystem] Initialized animator for entity {}: {}",
                  static_cast<uint32_t>(entity), animatorPath);
    }

    void RuntimeAnimatorSystem::destroyEntityAnimator(entt::entity entity)
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            animators.erase(it);
        }

        // Clear AnimatorComponent
        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.all_of<components::AnimatorComponent>(entity))
        {
            auto& animComp = registry.get<components::AnimatorComponent>(entity);
            animComp.stateMachine = nullptr;
            animComp.animatorPath.clear();
            animComp.isInitialized = false;
        }
    }

    bool RuntimeAnimatorSystem::hasAnimator(entt::entity entity) const
    {
        return animators.find(entity) != animators.end();
    }

    AnimatorStateMachine* RuntimeAnimatorSystem::getAnimator(entt::entity entity)
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    const AnimatorStateMachine* RuntimeAnimatorSystem::getAnimator(entt::entity entity) const
    {
        auto it = animators.find(entity);
        if (it != animators.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    void RuntimeAnimatorSystem::syncWithRegistry()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Find all entities with MeshComponent that have animatorPath set
        auto view = registry.view<components::MeshComponent>();
        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);

            if (!meshComp.animatorPath.empty())
            {
                // Initialize animator if not already done
                if (!hasAnimator(entity))
                {
                    initializeEntityAnimator(entity, meshComp.animatorPath);
                }
            }
            else
            {
                // No animator path - destroy if exists
                if (hasAnimator(entity))
                {
                    destroyEntityAnimator(entity);
                }
            }
        }

        // Clean up animators for entities that no longer exist or lost their MeshComponent
        std::vector<entt::entity> toRemove;
        for (const auto& [entity, animator] : animators)
        {
            if (!registry.valid(entity) || !registry.all_of<components::MeshComponent>(entity))
            {
                toRemove.push_back(entity);
            }
        }

        for (auto entity : toRemove)
        {
            animators.erase(entity);
            vfLogInfo("[RuntimeAnimatorSystem] Cleaned up animator for removed entity {}",
                      static_cast<uint32_t>(entity));
        }
    }

    void RuntimeAnimatorSystem::clearAll()
    {
        // Clear AnimatorComponent pointers first
        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [entity, animator] : animators)
        {
            if (registry.valid(entity) && registry.all_of<components::AnimatorComponent>(entity))
            {
                auto& animComp = registry.get<components::AnimatorComponent>(entity);
                animComp.stateMachine = nullptr;
                animComp.isInitialized = false;
            }
        }

        animators.clear();
        animatorDataCache.clear();
        vfLogInfo("[RuntimeAnimatorSystem] Cleared all animators");
    }

    const resource::AnimationData* RuntimeAnimatorSystem::loadAnimation(const std::string& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        // Load animation asynchronously and wait for result
        auto future = resource::ResourceManager::loadAnimationAsync(path);
        auto animData = future.get();

        if (!animData)
        {
            vfLogError("[RuntimeAnimatorSystem] Failed to load animation: {}", path);
            return nullptr;
        }

        // Return raw pointer - the shared_ptr keeps it alive in ResourceManager cache
        return animData.get();
    }
}
