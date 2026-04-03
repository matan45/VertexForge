#include "DebrisManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/render/MaterialEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../interfaces/physics/IPhysicsService.hpp"
#include <scene/EntityRegistry.hpp>
#include <scene/Entity.hpp>
#include <components/Components.hpp>
#include "DestructionHelpers.hpp"
#include <print/Log.hpp>
#include <algorithm>

namespace services
{
    using namespace services::destruction_internal;

    namespace
    {
        uint64_t assetKeyFromRef(const asset::AssetRef& ref)
        {
            return std::hash<uint64_t>{}(ref.getGUID().getValue());
        }
    }

    DebrisManager::DebrisManager(DebrisConfig config)
        : config(config)
        , pool(100)
    {
    }

    void DebrisManager::requestSpawn(std::vector<FragmentSpawnRequest> fragments)
    {
        for (auto& frag : fragments)
        {
            if (spawnQueue.size() < config.maxSpawnQueueSize)
            {
                spawnQueue.push_back(std::move(frag));
            }
        }
    }

    void DebrisManager::update(float deltaTime, uint32_t frameNumber)
    {
        processPendingSpawns(frameNumber);
        updateLifecycles(deltaTime);
        enforceBudget();
        updateFadeOuts(deltaTime);
    }

    void DebrisManager::reset()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Delete all active fragments
        auto view = registry.view<components::FragmentComponent>();
        for (auto entity : view)
        {
            EntityHandle handle;
            handle.id = static_cast<uint64_t>(static_cast<uint32_t>(entity));

            ::events::scene::DeleteEntityCommand deleteCmd;
            deleteCmd.entity = handle;
            dispatcher.execute(deleteCmd);
        }

        // Drain and delete pooled entities
        auto pooled = pool.drainAll();
        for (uint64_t id : pooled)
        {
            EntityHandle handle;
            handle.id = id;

            ::events::scene::DeleteEntityCommand deleteCmd;
            deleteCmd.entity = handle;
            dispatcher.execute(deleteCmd);
        }

        spawnQueue.clear();
        activeCount = 0;
    }

    uint32_t DebrisManager::getActiveCount() const
    {
        return activeCount;
    }

    void DebrisManager::processPendingSpawns(uint32_t frameNumber)
    {
        uint32_t spawned = 0;
        while (!spawnQueue.empty() && spawned < config.maxSpawnsPerFrame)
        {
            // Check budget before spawning
            if (activeCount >= config.maxActiveFragments)
            {
                break;
            }

            auto request = std::move(spawnQueue.front());
            spawnQueue.pop_front();

            spawnSingleFragment(request, frameNumber);
            ++spawned;
        }
    }

    EntityHandle DebrisManager::createFragmentEntity(const FragmentSpawnRequest& request, uint32_t frameNumber)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        ::events::scene::CreateEntityCommand createCmd;
        createCmd.name = "fragment_" + std::to_string(request.fragmentIndex);
        auto fragmentHandle = dispatcher.execute(createCmd);

        if (!fragmentHandle.isValid())
        {
            return fragmentHandle;
        }

        ::events::scene::SetTransformCommand transformCmd;
        transformCmd.entity = fragmentHandle;
        transformCmd.transform.position = request.position;
        transformCmd.transform.rotation = request.rotation;
        transformCmd.transform.scale = request.scale;
        dispatcher.execute(transformCmd);

        ::events::scene::AddMeshComponentCommand meshCmd;
        meshCmd.entity = fragmentHandle;
        dispatcher.execute(meshCmd);

        ::events::scene::SetMeshDataCommand meshDataCmd;
        meshDataCmd.entity = fragmentHandle;
        meshDataCmd.meshData.meshRef = request.fractureAssetRef;
        dispatcher.execute(meshDataCmd);

        ::events::material::SetMaterialDataCommand matCmd;
        matCmd.entity = fragmentHandle;
        matCmd.materialData = request.sourceMaterial;
        dispatcher.execute(matCmd);

        scene::Entity fragEntity(fromHandle(fragmentHandle));
        auto& fragComp = fragEntity.addComponent<components::FragmentComponent>();
        fragComp.sourceEntityId = request.sourceEntityId;
        fragComp.fragmentIndex = request.fragmentIndex;
        fragComp.lifetime = request.lifetime;
        fragComp.elapsed = 0.0f;
        fragComp.state = components::FragmentState::Active;
        fragComp.sleepTime = 0.0f;
        fragComp.fadeOutDuration = config.fadeOutDuration;
        fragComp.fadeProgress = 0.0f;
        fragComp.spawnFrame = frameNumber;
        fragComp.materialType = request.materialType;
        fragComp.collisionAudioRef = request.collisionAudioRef;

        return fragmentHandle;
    }

    void DebrisManager::spawnSingleFragment(const FragmentSpawnRequest& request, uint32_t frameNumber)
    {
        auto fragmentHandle = createFragmentEntity(request, frameNumber);
        if (!fragmentHandle.isValid())
        {
            return;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();

        ::events::physics::AddRigidBodyCommand rbCmd;
        rbCmd.entity = fragmentHandle;
        rbCmd.rigidBody.type = types::RigidBodyType::Dynamic;
        rbCmd.rigidBody.mass = request.mass;
        rbCmd.rigidBody.linearDamping = 0.5f;
        rbCmd.rigidBody.angularDamping = 0.5f;
        rbCmd.rigidBody.activateOnAdd = true;
        rbCmd.collider.shape = types::ColliderShape::ConvexMesh;
        rbCmd.collider.meshPath = request.fractureAssetRef.resolve();
        rbCmd.collider.collisionLayer = 1;
        dispatcher.execute(rbCmd);

        if (glm::length(request.impulse) > 0.001f)
        {
            ::events::physics::ApplyImpulseCommand impulseCmd;
            impulseCmd.entity = fragmentHandle;
            impulseCmd.impulse = request.impulse;
            dispatcher.execute(impulseCmd);
        }

        ++activeCount;
    }

    void DebrisManager::updateLifecycles(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        activeCount = 0;

        auto view = registry.view<components::FragmentComponent>();
        for (auto entity : view)
        {
            auto& fragment = view.get<components::FragmentComponent>(entity);

            if (fragment.state == components::FragmentState::Pooled)
            {
                continue;
            }

            ++activeCount;
            fragment.elapsed += deltaTime;

            EntityHandle handle;
            handle.id = static_cast<uint64_t>(static_cast<uint32_t>(entity));

            if (fragment.state == components::FragmentState::Active)
            {
                // Check if body went to sleep
                ::events::physics::IsBodySleepingQuery sleepQuery;
                sleepQuery.entity = handle;
                bool sleeping = dispatcher.query(sleepQuery);

                if (sleeping)
                {
                    fragment.state = components::FragmentState::Sleeping;
                    fragment.sleepTime = 0.0f;
                }
            }
            else if (fragment.state == components::FragmentState::Sleeping)
            {
                fragment.sleepTime += deltaTime;

                if (fragment.sleepTime >= fragment.lifetime)
                {
                    fragment.state = components::FragmentState::FadingOut;
                    fragment.fadeProgress = 0.0f;
                }
            }
        }
    }

    std::vector<DebrisManager::EvictionCandidate> DebrisManager::collectEvictionCandidates() const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<EvictionCandidate> candidates;

        auto view = registry.view<components::FragmentComponent>();
        for (auto entity : view)
        {
            const auto& fragment = view.get<components::FragmentComponent>(entity);
            if (fragment.state == components::FragmentState::Sleeping)
            {
                candidates.push_back({entity, fragment.distanceToCamera, fragment.spawnFrame});
            }
        }

        // Sort: farthest from camera first, then oldest
        std::sort(candidates.begin(), candidates.end(),
            [](const EvictionCandidate& a, const EvictionCandidate& b)
            {
                if (a.distanceToCamera != b.distanceToCamera)
                    return a.distanceToCamera > b.distanceToCamera;
                return a.spawnFrame < b.spawnFrame;
            });

        return candidates;
    }

    void DebrisManager::enforceBudget()
    {
        if (activeCount <= config.maxActiveFragments)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();
        auto candidates = collectEvictionCandidates();

        uint32_t excess = activeCount - config.maxActiveFragments;
        uint32_t removed = 0;

        for (const auto& candidate : candidates)
        {
            if (removed >= excess) break;

            auto& fragment = registry.get<components::FragmentComponent>(candidate.entity);

            if (excess > config.maxActiveFragments / 10)
            {
                EntityHandle handle;
                handle.id = static_cast<uint64_t>(static_cast<uint32_t>(candidate.entity));

                ::events::scene::DeleteEntityCommand deleteCmd;
                deleteCmd.entity = handle;
                dispatcher.execute(deleteCmd);
                --activeCount;
            }
            else
            {
                fragment.state = components::FragmentState::FadingOut;
                fragment.fadeProgress = 0.0f;
            }

            ++removed;
        }
    }

    void DebrisManager::updateFadeOuts(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        std::vector<entt::entity> toRemove;

        auto view = registry.view<components::FragmentComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            auto& fragment = view.get<components::FragmentComponent>(entity);
            if (fragment.state != components::FragmentState::FadingOut)
            {
                continue;
            }

            fragment.fadeProgress += deltaTime / fragment.fadeOutDuration;

            if (fragment.fadeProgress >= 1.0f)
            {
                toRemove.push_back(entity);
                continue;
            }

            // Scale down as fade effect
            auto& transform = view.get<components::TransformComponent>(entity);
            float scale = 1.0f - fragment.fadeProgress;
            EntityHandle handle;
            handle.id = static_cast<uint64_t>(static_cast<uint32_t>(entity));

            ::events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = handle;
            transformCmd.transform.position = transform.position;
            transformCmd.transform.rotation = transform.rotation;
            transformCmd.transform.scale = glm::vec3(scale);
            dispatcher.execute(transformCmd);
        }

        for (auto entity : toRemove)
        {
            EntityHandle handle;
            handle.id = static_cast<uint64_t>(static_cast<uint32_t>(entity));

            ::events::scene::DeleteEntityCommand deleteCmd;
            deleteCmd.entity = handle;
            dispatcher.execute(deleteCmd);
            --activeCount;
        }
    }
}
