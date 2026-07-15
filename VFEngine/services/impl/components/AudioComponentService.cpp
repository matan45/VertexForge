#include "AudioComponentService.hpp"
#include "BillboardAutoIcon.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/scene/ReverbZoneEvents.hpp"
#include "resource/AssetLifecycleManager.hpp"

namespace services {

    AudioComponentService::AudioComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    // ========== 2D AUDIO SOURCE COMPONENT OPERATIONS ==========

    bool AudioComponentService::addAudioSource2DComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
            sceneEntity.addComponent<components::AudioSource2DComponent>();
            components_helpers::autoAttachBillboard(entity, components::BillboardIconType::Audio2D);
            return true;
        }
        return false;
    }

    bool AudioComponentService::removeAudioSource2DComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
            auto& comp = sceneEntity.getComponent<components::AudioSource2DComponent>();
            if (comp.audioRef.isValid()) {
                resource::AssetLifecycleManager::instance().release(comp.audioRef.getGUID());
            }
            sceneEntity.removeComponent<components::AudioSource2DComponent>();
            if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
                components_helpers::autoDetachBillboard(entity, components::BillboardIconType::Audio2D);
            }
            return true;
        }
        return false;
    }

    bool AudioComponentService::hasAudioSource2DComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::AudioSource2DComponent>();
    }

    std::optional<AudioSource2DData> AudioComponentService::getAudioSource2DData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::AudioSource2DComponent>();
        AudioSource2DData data;
        data.audioRef = comp.audioRef;
        data.volume = comp.volume;
        data.pitch = comp.pitch;
        data.loop = comp.loop;
        data.busName = comp.busName;
        data.priority = comp.priority;
        return data;
    }

    bool AudioComponentService::setAudioSource2DData(EntityHandle entity, const AudioSource2DData& audioData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
            sceneEntity.addComponent<components::AudioSource2DComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::AudioSource2DComponent>();
        auto& lifecycle = resource::AssetLifecycleManager::instance();
        if (comp.audioRef.isValid() && comp.audioRef != audioData.audioRef) {
            lifecycle.release(comp.audioRef.getGUID());
        }
        comp.audioRef = audioData.audioRef;
        comp.volume = audioData.volume;
        comp.pitch = audioData.pitch;
        comp.loop = audioData.loop;
        comp.busName = audioData.busName;
        comp.priority = audioData.priority;
        if (audioData.audioRef.isValid()) {
            lifecycle.acquire(audioData.audioRef.getGUID(), resource::AssetType::Audio);
        }
        return true;
    }

    // ========== 3D AUDIO SOURCE COMPONENT OPERATIONS ==========

    bool AudioComponentService::addAudioSource3DComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
            sceneEntity.addComponent<components::AudioSource3DComponent>();
            components_helpers::autoAttachBillboard(entity, components::BillboardIconType::Audio3D);
            return true;
        }
        return false;
    }

    bool AudioComponentService::removeAudioSource3DComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
            auto& comp = sceneEntity.getComponent<components::AudioSource3DComponent>();
            if (comp.audioRef.isValid()) {
                resource::AssetLifecycleManager::instance().release(comp.audioRef.getGUID());
            }
            sceneEntity.removeComponent<components::AudioSource3DComponent>();
            if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
                components_helpers::autoDetachBillboard(entity, components::BillboardIconType::Audio3D);
            }
            return true;
        }
        return false;
    }

    bool AudioComponentService::hasAudioSource3DComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::AudioSource3DComponent>();
    }

    std::optional<AudioSource3DData> AudioComponentService::getAudioSource3DData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::AudioSource3DComponent>();
        AudioSource3DData data;
        data.audioRef = comp.audioRef;
        data.volume = comp.volume;
        data.pitch = comp.pitch;
        data.loop = comp.loop;
        data.minDistance = comp.minDistance;
        data.maxDistance = comp.maxDistance;
        data.showDebugSpheres = comp.showDebugSpheres;
        data.enableDistanceFilter = comp.enableDistanceFilter;
        data.filterStartDistance = comp.filterStartDistance;
        data.filterMaxDistance = comp.filterMaxDistance;
        data.filterIntensity = comp.filterIntensity;
        data.innerConeAngle = comp.innerConeAngle;
        data.outerConeAngle = comp.outerConeAngle;
        data.outerConeGain = comp.outerConeGain;
        data.showDebugCone = comp.showDebugCone;
        data.busName = comp.busName;
        data.priority = comp.priority;
        return data;
    }

    bool AudioComponentService::setAudioSource3DData(EntityHandle entity, const AudioSource3DData& audioData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
            sceneEntity.addComponent<components::AudioSource3DComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::AudioSource3DComponent>();
        auto& lifecycle = resource::AssetLifecycleManager::instance();
        if (comp.audioRef.isValid() && comp.audioRef != audioData.audioRef) {
            lifecycle.release(comp.audioRef.getGUID());
        }
        comp.audioRef = audioData.audioRef;
        comp.volume = audioData.volume;
        comp.pitch = audioData.pitch;
        comp.loop = audioData.loop;
        comp.minDistance = audioData.minDistance;
        comp.maxDistance = audioData.maxDistance;
        comp.showDebugSpheres = audioData.showDebugSpheres;
        comp.enableDistanceFilter = audioData.enableDistanceFilter;
        comp.filterStartDistance = audioData.filterStartDistance;
        comp.filterMaxDistance = audioData.filterMaxDistance;
        comp.filterIntensity = audioData.filterIntensity;
        comp.innerConeAngle = audioData.innerConeAngle;
        comp.outerConeAngle = audioData.outerConeAngle;
        comp.outerConeGain = audioData.outerConeGain;
        comp.showDebugCone = audioData.showDebugCone;
        comp.busName = audioData.busName;
        comp.priority = audioData.priority;
        if (audioData.audioRef.isValid()) {
            lifecycle.acquire(audioData.audioRef.getGUID(), resource::AssetType::Audio);
        }
        return true;
    }

    // ========== REVERB ZONE COMPONENT OPERATIONS ==========

    bool AudioComponentService::addReverbZoneComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ReverbZoneComponent>()) {
            sceneEntity.addComponent<components::ReverbZoneComponent>();
            components_helpers::autoAttachBillboard(entity, components::BillboardIconType::ReverbZone);
            return true;
        }
        return false;
    }

    bool AudioComponentService::removeReverbZoneComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::ReverbZoneComponent>()) {
            sceneEntity.removeComponent<components::ReverbZoneComponent>();
            components_helpers::autoDetachBillboard(entity, components::BillboardIconType::ReverbZone);
            return true;
        }
        return false;
    }

    bool AudioComponentService::hasReverbZoneComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::ReverbZoneComponent>();
    }

    std::optional<ReverbZoneData> AudioComponentService::getReverbZoneData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ReverbZoneComponent>()) return std::nullopt;

        const auto& comp = sceneEntity.getComponent<components::ReverbZoneComponent>();
        ReverbZoneData data;
        data.shape = static_cast<uint8_t>(comp.shape);
        data.radius = comp.radius;
        data.halfExtents = comp.halfExtents;
        data.presetName = comp.presetName;
        data.customParams = comp.customParams;
        data.priority = comp.priority;
        data.falloffDistance = comp.falloffDistance;
        data.wetLevel = comp.wetLevel;
        data.showDebugVolume = comp.showDebugVolume;
        return data;
    }

    bool AudioComponentService::setReverbZoneData(EntityHandle entity, const ReverbZoneData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ReverbZoneComponent>()) {
            sceneEntity.addComponent<components::ReverbZoneComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::ReverbZoneComponent>();
        comp.shape = static_cast<components::ReverbZoneShape>(data.shape);
        comp.radius = data.radius;
        comp.halfExtents = data.halfExtents;
        comp.presetName = data.presetName;
        comp.customParams = data.customParams;
        comp.priority = data.priority;
        comp.falloffDistance = data.falloffDistance;
        comp.wetLevel = data.wetLevel;
        comp.showDebugVolume = data.showDebugVolume;
        return true;
    }

    void AudioComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        // 2D Audio Source component handlers
        dispatcher.registerCommandHandler<events::scene::AddAudioSource2DComponentCommand>(
            [this](const events::scene::AddAudioSource2DComponentCommand& cmd) {
                return addAudioSource2DComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveAudioSource2DComponentCommand>(
            [this](const events::scene::RemoveAudioSource2DComponentCommand& cmd) {
                return removeAudioSource2DComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetAudioSource2DDataCommand>(
            [this](const events::scene::SetAudioSource2DDataCommand& cmd) {
                return setAudioSource2DData(cmd.entity, cmd.audioData);
            });

        dispatcher.registerQueryHandler<events::scene::HasAudioSource2DComponentQuery>(
            [this](const events::scene::HasAudioSource2DComponentQuery& query) {
                return hasAudioSource2DComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetAudioSource2DDataQuery>(
            [this](const events::scene::GetAudioSource2DDataQuery& query) {
                return getAudioSource2DData(query.entity);
            });

        // 3D Audio Source component handlers
        dispatcher.registerCommandHandler<events::scene::AddAudioSource3DComponentCommand>(
            [this](const events::scene::AddAudioSource3DComponentCommand& cmd) {
                return addAudioSource3DComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveAudioSource3DComponentCommand>(
            [this](const events::scene::RemoveAudioSource3DComponentCommand& cmd) {
                return removeAudioSource3DComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetAudioSource3DDataCommand>(
            [this](const events::scene::SetAudioSource3DDataCommand& cmd) {
                return setAudioSource3DData(cmd.entity, cmd.audioData);
            });

        dispatcher.registerQueryHandler<events::scene::HasAudioSource3DComponentQuery>(
            [this](const events::scene::HasAudioSource3DComponentQuery& query) {
                return hasAudioSource3DComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetAudioSource3DDataQuery>(
            [this](const events::scene::GetAudioSource3DDataQuery& query) {
                return getAudioSource3DData(query.entity);
            });

        // Reverb Zone component handlers
        dispatcher.registerCommandHandler<events::scene::AddReverbZoneComponentCommand>(
            [this](const events::scene::AddReverbZoneComponentCommand& cmd) {
                return addReverbZoneComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveReverbZoneComponentCommand>(
            [this](const events::scene::RemoveReverbZoneComponentCommand& cmd) {
                return removeReverbZoneComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetReverbZoneDataCommand>(
            [this](const events::scene::SetReverbZoneDataCommand& cmd) {
                return setReverbZoneData(cmd.entity, cmd.data);
            });

        dispatcher.registerQueryHandler<events::scene::HasReverbZoneComponentQuery>(
            [this](const events::scene::HasReverbZoneComponentQuery& query) {
                return hasReverbZoneComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetReverbZoneDataQuery>(
            [this](const events::scene::GetReverbZoneDataQuery& query) {
                return getReverbZoneData(query.entity);
            });
    }

}
