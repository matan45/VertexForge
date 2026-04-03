#include "DestructionEffectsManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../events/audio/AudioEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../interfaces/audio/IAudioService.hpp"
#include "../../data/VFXTypes.hpp"
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace services
{
    namespace
    {
        entt::entity fromHandle(EntityHandle handle)
        {
            return static_cast<entt::entity>(static_cast<uint32_t>(handle.id));
        }

        bool isValidHandle(EntityHandle handle, entt::registry& registry)
        {
            if (!handle.isValid()) return false;
            return registry.valid(fromHandle(handle));
        }
    }

    DestructionEffectsManager::DestructionEffectsManager(DestructionEffectsConfig config)
        : config(config)
    {
    }

    void DestructionEffectsManager::onDamageApplied(EntityHandle entity, float damageAmount,
                                                     const glm::vec3& impactPoint,
                                                     const glm::vec3& impactDir,
                                                     components::DamageType damageType)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!isValidHandle(entity, registry))
        {
            return;
        }

        scene::Entity sceneEntity(fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            return;
        }

        const auto& destructible = sceneEntity.getComponent<components::DestructibleComponent>();

        // Spawn damage VFX at impact point
        if (destructible.onDamageVFX.isValid())
        {
            spawnVFX(destructible.onDamageVFX, impactPoint);
        }

        // Play damage audio at impact point
        if (destructible.onDamageAudio.isValid())
        {
            float volume = glm::clamp(destructible.fragmentMassTotal * config.volumeScaleMassFactor,
                                      0.1f, 1.0f);
            playSound3D(destructible.onDamageAudio, impactPoint, volume);
        }

        // Spawn damage decal
        if (destructible.damageDecalAlbedo.isValid())
        {
            spawnDamageDecal(destructible.damageDecalAlbedo, destructible.damageDecalNormal,
                            impactPoint, impactDir);
        }
    }

    DestructionEffectsSnapshot DestructionEffectsManager::captureSnapshot(EntityHandle entity)
    {
        DestructionEffectsSnapshot snapshot;
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!isValidHandle(entity, registry))
        {
            return snapshot;
        }

        scene::Entity sceneEntity(fromHandle(entity));

        if (sceneEntity.hasComponent<components::TransformComponent>())
        {
            snapshot.position = sceneEntity.getComponent<components::TransformComponent>().position;
        }

        if (sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            const auto& d = sceneEntity.getComponent<components::DestructibleComponent>();
            snapshot.materialType = d.materialType;
            snapshot.fragmentMassTotal = d.fragmentMassTotal;
            snapshot.onDestroyVFX = d.onDestroyVFX;
            snapshot.onDestroyAudio = d.onDestroyAudio;
        }

        return snapshot;
    }

    void DestructionEffectsManager::onDestructionTriggered(
        const DestructionEffectsSnapshot& snapshot,
        const glm::vec3& impactPoint,
        const glm::vec3& impactDir)
    {
        // Spawn destruction VFX
        if (snapshot.onDestroyVFX.isValid())
        {
            spawnVFX(snapshot.onDestroyVFX, impactPoint);
        }

        // Play destruction audio
        if (snapshot.onDestroyAudio.isValid())
        {
            float volume = glm::clamp(snapshot.fragmentMassTotal * config.volumeScaleMassFactor,
                                      0.2f, 1.0f);
            playSound3D(snapshot.onDestroyAudio, impactPoint, volume);
        }
    }

    void DestructionEffectsManager::onFragmentCollision(EntityHandle fragmentEntity,
                                                         const glm::vec3& contactPoint,
                                                         float impulse)
    {
        if (collisionSoundsThisSecond >= config.maxFragmentCollisionSoundsPerSecond)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!isValidHandle(fragmentEntity, registry))
        {
            return;
        }

        scene::Entity entity(fromHandle(fragmentEntity));
        if (!entity.hasComponent<components::FragmentComponent>())
        {
            return;
        }

        const auto& fragment = entity.getComponent<components::FragmentComponent>();
        if (!fragment.collisionAudioRef.isValid())
        {
            return;
        }

        float volume = glm::clamp(impulse * 0.5f, 0.05f, 0.8f);
        playSound3D(fragment.collisionAudioRef, contactPoint, volume);
        ++collisionSoundsThisSecond;
    }

    void DestructionEffectsManager::update(float deltaTime)
    {
        collisionSoundTimer += deltaTime;
        if (collisionSoundTimer >= 1.0f)
        {
            collisionSoundTimer -= 1.0f;
            collisionSoundsThisSecond = 0;
        }
    }

    void DestructionEffectsManager::spawnVFX(const asset::AssetRef& vfxRef,
                                              const glm::vec3& position)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        ::services::events::vfxruntime::CreateVFXInstanceCommand createCmd;
        createCmd.params.vfxAssetPath = vfxRef.resolve();
        createCmd.params.worldTransform = glm::translate(glm::mat4(1.0f), position);
        createCmd.params.loop = false;
        createCmd.params.priority = VFXEmitterPriority::High;

        auto instanceId = dispatcher.execute(createCmd);

        ::services::events::vfxruntime::PlayVFXInstanceCommand playCmd;
        playCmd.instanceId = instanceId;
        dispatcher.execute(playCmd);
    }

    void DestructionEffectsManager::playSound3D(const asset::AssetRef& audioRef,
                                                 const glm::vec3& position,
                                                 float volume)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        ::events::audio::PlaySound3DCommand cmd;
        cmd.path = audioRef.resolve();
        cmd.position = position;
        cmd.params.volume = volume;
        cmd.params.is3D = true;
        cmd.params.minDistance = 1.0f;
        cmd.params.maxDistance = 100.0f;
        cmd.params.busName = "SFX";
        dispatcher.execute(cmd);
    }

    void DestructionEffectsManager::spawnDamageDecal(const asset::AssetRef& albedo,
                                                      const asset::AssetRef& normal,
                                                      const glm::vec3& impactPoint,
                                                      const glm::vec3& impactDir)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Create a decal entity
        ::events::scene::CreateEntityCommand createCmd;
        createCmd.name = "damage_decal";
        auto decalEntity = dispatcher.execute(createCmd);

        if (!decalEntity.isValid())
        {
            return;
        }

        // Position at impact point
        ::events::scene::SetTransformCommand transformCmd;
        transformCmd.entity = decalEntity;
        transformCmd.transform.position = impactPoint;
        dispatcher.execute(transformCmd);

        // Add decal component
        ::events::scene::AddDecalComponentCommand addCmd;
        addCmd.entity = decalEntity;
        dispatcher.execute(addCmd);

        // Set decal data
        ::events::scene::SetDecalDataCommand setCmd;
        setCmd.entity = decalEntity;
        setCmd.decalData.albedoTextureRef = albedo;
        setCmd.decalData.normalTextureRef = normal;
        setCmd.decalData.halfExtents = glm::vec3(config.decalHalfExtents, config.decalHalfExtents, 0.05f);
        dispatcher.execute(setCmd);
    }
}
