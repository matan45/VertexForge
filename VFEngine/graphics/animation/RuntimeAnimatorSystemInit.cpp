#include "RuntimeAnimatorSystem.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../services/events/project/SceneEvents.hpp"
#include "../../services/events/editor/EditorModeEvents.hpp"
#include "../../services/events/physics/SocketEvents.hpp"
#include "../../services/events/world/WorldSectorEvents.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "asset/AssetRef.hpp"

namespace animation
{
    void RuntimeAnimatorSystem::initialize()
    {
        if (initialized)
            return;

        initialized = true;
        socketUpdater = std::make_unique<SocketAttachmentUpdater>(animators, dataCache);
        subscribeToEvents();
    }

    void RuntimeAnimatorSystem::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                if (!notification.animatorPath.empty())
                    return;

                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity entity = services::internal::fromHandle(notification.entity);
                if (!registry.valid(entity))
                    return;
                if (hasAnimator(entity))
                    destroyEntityAnimator(entity);
            });

        editorModeChangedToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification&)
            {
                clearAnimatorInstances();
                dataCache.clearSkeletons();
                pendingCacheCleanup = true;

                auto& reg = scene::EntityRegistry::getRegistry();
                auto view = reg.view<components::SocketAttachmentComponent>();
                for (auto entity : view)
                    reg.get<components::SocketAttachmentComponent>(entity).needsParentResolution = true;
            });

        socketDataSavedToken = dispatcher.subscribe<events::socket::SocketDataSavedNotification>(
            [this](const events::socket::SocketDataSavedNotification& notification)
            {
                dataCache.invalidateSkeleton(notification.meshPath);
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::SocketAttachmentComponent>();
                for (auto entity : view)
                    registry.get<components::SocketAttachmentComponent>(entity).cachedSocketIndex = -1;
            });

        subscribeToWorldEvents();
    }

    void RuntimeAnimatorSystem::subscribeToWorldEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sectorLoadedToken = dispatcher.subscribe<events::world::SectorLoadedNotification>(
            [this](const events::world::SectorLoadedNotification&)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::MeshComponent>();
                std::lock_guard<std::mutex> lock(pendingInitMutex);
                for (auto entity : view)
                {
                    const auto& meshComp = view.get<components::MeshComponent>(entity);
                    if (meshComp.animatorRef.isValid() && !hasAnimator(entity))
                        pendingInitQueue.push_back({entity, meshComp.animatorRef.resolve()});
                }
            });

        sectorUnloadedToken = dispatcher.subscribe<events::world::SectorUnloadedNotification>(
            [this](const events::world::SectorUnloadedNotification&)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                std::vector<entt::entity> toRemove;
                for (const auto& [entity, animator] : animators)
                {
                    if (!registry.valid(entity))
                        toRemove.push_back(entity);
                }
                for (auto entity : toRemove)
                    destroyEntityAnimator(entity);

                std::lock_guard<std::mutex> lock(pendingInitMutex);
                pendingInitQueue.erase(
                    std::remove_if(pendingInitQueue.begin(), pendingInitQueue.end(),
                        [&registry](const PendingAnimatorInit& pending) {
                            return !registry.valid(pending.entity);
                        }),
                    pendingInitQueue.end());
            });
    }

    const resource::SkeletonData* RuntimeAnimatorSystem::resolveEntitySkeleton(entt::entity entity, std::string& outMeshPath)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.all_of<components::MeshComponent>(entity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            outMeshPath = meshComp.meshRef.resolve();
            if (!outMeshPath.empty())
                return dataCache.loadSkeleton(outMeshPath);
        }
        return nullptr;
    }

    void RuntimeAnimatorSystem::setupEntityAnimatorComponent(entt::entity entity, AnimationLayerStack* layerStack, const std::string& animatorPath)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::AnimatorComponent>(entity))
            registry.emplace<components::AnimatorComponent>(entity);

        auto& animComp = registry.get<components::AnimatorComponent>(entity);
        animComp.stateMachine = layerStack->getBaseStateMachine();
        animComp.animatorRef = asset::AssetRef::fromPath(animatorPath);
        animComp.isInitialized = true;

        if (registry.all_of<components::MeshComponent>(entity))
        {
            const auto& meshComp = registry.get<components::MeshComponent>(entity);
            animComp.applyRootMotion = meshComp.applyRootMotion;
        }
        layerStack->setRootMotionEnabled(animComp.applyRootMotion);
    }

    void RuntimeAnimatorSystem::initializeEntityAnimator(entt::entity entity, const std::string& animatorPath)
    {
        if (animatorPath.empty())
            return;

        auto it = animators.find(entity);
        if (it != animators.end())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::AnimatorComponent>(entity))
            {
                auto& animComp = registry.get<components::AnimatorComponent>(entity);
                if (animComp.animatorRef.resolve() == animatorPath && animComp.isInitialized)
                    return;
            }
            destroyEntityAnimator(entity);
        }

        auto animatorData = dataCache.loadAnimatorData(animatorPath);
        if (!animatorData)
            return;

        std::string meshPath;
        const resource::SkeletonData* skeleton = resolveEntitySkeleton(entity, meshPath);
        if (!skeleton)
        {
            if (meshPath.empty())
            {
                vfLogWarning("[RuntimeAnimatorSystem] Entity {} has no mesh path - cannot create animator",
                             static_cast<uint32_t>(entity));
            }
            return;
        }

        auto layerStack = std::make_unique<AnimationLayerStack>();
        layerStack->initialize(*animatorData, skeleton, [this](const std::string& path) -> const resource::AnimationData*
        {
            return dataCache.loadAnimation(path);
        });

        AnimationLayerStack* rawPtr = layerStack.get();
        animators[entity] = std::move(layerStack);
        setupEntityAnimatorComponent(entity, rawPtr, animatorPath);
    }
}
