#include "RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"
#include "../../services/events/SceneEvents.hpp"
#include "../../services/events/EditorModeEvents.hpp"
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
        // Note: We only handle destruction here. Creation is handled by syncWithRegistry during play mode
        // to avoid entity ID mismatches between edit and play mode
        auto& dispatcher = events::EventDispatcher::instance();
        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                // Only handle animator destruction when path is cleared
                if (!notification.animatorPath.empty())
                {
                    return;
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity entity = services::internal::fromHandle(notification.entity);

                if (!registry.valid(entity))
                {
                    return;
                }

                if (hasAnimator(entity))
                {
                    destroyEntityAnimator(entity);
                }
            });

        // Subscribe to editor mode changes to clear animator instances when switching modes
        // This ensures fresh initialization with correct entity IDs (entity IDs change between edit/play mode)
        editorModeChangedToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& notification)
            {
                // Clear animator instances when switching between Edit and Play modes
                // Entity IDs are different in each mode, so animators must be recreated
                clearAnimatorInstances();
                vfLogInfo("[RuntimeAnimatorSystem] Cleared animators for mode change to {} - will reinitialize",
                          notification.currentMode == services::EditorMode::Play ? "Play" : "Edit");
            });

        vfLogInfo("[RuntimeAnimatorSystem] Initialized");
    }

    void RuntimeAnimatorSystem::shutdown()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Unsubscribe from mesh data changed notifications
        if (meshDataChangedToken.isValid())
        {
            dispatcher.unsubscribe(meshDataChangedToken);
            meshDataChangedToken = {};
        }

        // Unsubscribe from editor mode changed notifications
        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
            editorModeChangedToken = {};
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
        std::string meshPath;

        if (registry.all_of<components::MeshComponent>(entity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            meshPath = meshComp.meshPath;
            if (!meshPath.empty())
            {
                skeleton = loadSkeleton(meshPath);
            }
        }

        if (!skeleton)
        {
            if (meshPath.empty())
            {
                // No mesh path - can't have skeleton, don't create animator
                vfLogWarning("[RuntimeAnimatorSystem] Entity {} has no mesh path - cannot create animator",
                             static_cast<uint32_t>(entity));
                return;
            }

            // Check if skeleton loading failed definitively (cached null) vs just not ready yet
            auto cacheIt = skeletonDataCache.find(meshPath);
            if (cacheIt != skeletonDataCache.end() && !cacheIt->second)
            {
                // Mesh definitively has no skeleton data - don't create animator
                // (warning already logged in loadSkeleton)
                return;
            }
            // Mesh not ready yet - will retry next frame
            return;
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

    void RuntimeAnimatorSystem::clearAnimatorInstances()
    {
        // Clear AnimatorComponent pointers but keep caches for fast reinitialization
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
        vfLogInfo("[RuntimeAnimatorSystem] Cleared animator instances (caches preserved)");
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
            // Cache nullptr so we don't keep retrying for meshes without skeleton
            skeletonDataCache[meshPath] = nullptr;
            vfLogWarning("[RuntimeAnimatorSystem] Mesh has no skeleton data: {}", meshPath);
            return nullptr;
        }

        auto skeletonData = std::make_shared<resource::SkeletonData>();
        if (!stream->readSkeleton(*skeletonData))
        {
            // Cache nullptr so we don't keep retrying for broken skeleton data
            skeletonDataCache[meshPath] = nullptr;
            vfLogError("[RuntimeAnimatorSystem] Failed to read skeleton from: {}", meshPath);
            return nullptr;
        }

        // Store in cache
        skeletonDataCache[meshPath] = skeletonData;

        return skeletonData.get();
    }
}
