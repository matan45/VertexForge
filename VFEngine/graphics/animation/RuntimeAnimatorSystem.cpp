#include "RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"
#include "../../services/events/SceneEvents.hpp"
#include "../../services/data/EntityConversion.hpp"

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

        // Subscribe to mesh data changes to handle animator cleanup when animatorPath is cleared
        auto& dispatcher = events::EventDispatcher::instance();
        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity entity = services::internal::fromHandle(notification.entity);

                if (!registry.valid(entity))
                {
                    return;
                }

                // If animator path is cleared, destroy the animator
                if (notification.animatorPath.empty())
                {
                    if (hasAnimator(entity))
                    {
                        destroyEntityAnimator(entity);
                    }
                }
            });

        vfLogInfo("[RuntimeAnimatorSystem] Initialized");
    }

    void RuntimeAnimatorSystem::shutdown()
    {
        // Unsubscribe from mesh data changed notifications
        if (meshDataChangedToken.isValid())
        {
            events::EventDispatcher::instance().unsubscribe(meshDataChangedToken);
            meshDataChangedToken = {};
        }

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

        // Check if animator data is already cached
        auto cacheIt = animatorDataCache.find(animatorPath);
        std::shared_ptr<animator::AnimatorData> animatorData;

        if (cacheIt != animatorDataCache.end())
        {
            animatorData = cacheIt->second;
        }
        else
        {
            // Load animator data
            animatorData = resource::ResourceManager::loadAnimator(animatorPath);
            if (!animatorData)
            {
                vfLogError("[RuntimeAnimatorSystem] Failed to load animator: {}", animatorPath);
                return;
            }
            // Store in cache to keep it alive
            animatorDataCache[animatorPath] = animatorData;
        }

        // Get mesh path from MeshComponent to load skeleton
        auto& registry = scene::EntityRegistry::getRegistry();
        const resource::SkeletonData* skeleton = nullptr;

        if (registry.all_of<components::MeshComponent>(entity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            if (!meshComp.meshPath.empty())
            {
                skeleton = loadSkeleton(meshComp.meshPath);
            }
        }

        if (!skeleton)
        {
            vfLogWarning("[RuntimeAnimatorSystem] No skeleton available for entity {} - animation may not work",
                         static_cast<uint32_t>(entity));
        }

        // Create state machine
        auto stateMachine = std::make_unique<AnimatorStateMachine>();

        // Initialize with animation load callback
        stateMachine->initialize(*animatorData, skeleton, [this](const std::string& path) -> const resource::AnimationData*
        {
            return loadAnimation(path);
        });

        // Store in map
        AnimatorStateMachine* rawPtr = stateMachine.get();
        animators[entity] = std::move(stateMachine);

        // Update AnimatorComponent on entity
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
        animationDataCache.clear();
        skeletonDataCache.clear();
        vfLogInfo("[RuntimeAnimatorSystem] Cleared all animators");
    }

    const resource::AnimationData* RuntimeAnimatorSystem::loadAnimation(const std::string& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        // Check if already cached
        auto it = animationDataCache.find(path);
        if (it != animationDataCache.end())
        {
            return it->second.get();
        }

        // Load animation asynchronously and wait for result
        auto future = resource::ResourceManager::loadAnimationAsync(path);
        auto animData = future.get();

        if (!animData)
        {
            vfLogError("[RuntimeAnimatorSystem] Failed to load animation: {}", path);
            return nullptr;
        }

        vfLogInfo("[RuntimeAnimatorSystem] Loaded animation '{}': channels={}, duration={}",
            path, animData->channels.size(), animData->duration);

        // Store in cache to keep the shared_ptr alive
        animationDataCache[path] = animData;

        return animData.get();
    }

    const resource::SkeletonData* RuntimeAnimatorSystem::loadSkeleton(const std::string& meshPath)
    {
        if (meshPath.empty())
        {
            return nullptr;
        }

        // Check if already cached
        auto it = skeletonDataCache.find(meshPath);
        if (it != skeletonDataCache.end())
        {
            return it->second.get();
        }

        // Open mesh stream and read skeleton
        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            vfLogError("[RuntimeAnimatorSystem] Failed to open mesh for skeleton: {}", meshPath);
            return nullptr;
        }

        if (!stream->hasSkeletonData())
        {
            vfLogWarning("[RuntimeAnimatorSystem] Mesh has no skeleton data: {}", meshPath);
            return nullptr;
        }

        auto skeletonData = std::make_shared<resource::SkeletonData>();
        if (!stream->readSkeleton(*skeletonData))
        {
            vfLogError("[RuntimeAnimatorSystem] Failed to read skeleton from: {}", meshPath);
            return nullptr;
        }

        vfLogInfo("[RuntimeAnimatorSystem] Loaded skeleton from '{}': {} bones",
                  meshPath, skeletonData->bones.size());

        // Store in cache
        skeletonDataCache[meshPath] = skeletonData;

        return skeletonData.get();
    }
}
