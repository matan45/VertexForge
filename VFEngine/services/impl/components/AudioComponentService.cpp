#include "AudioComponentService.hpp"
#include "../../../utilities/scene/SceneGraphSystem.hpp"
#include "../../../utilities/scene/Entity.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SceneEvents.hpp"

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
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::AudioSource));
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
            sceneEntity.removeComponent<components::AudioSource2DComponent>();
            // Only remove billboard if no other audio component exists
            if (!sceneEntity.hasComponent<components::AudioSource3DComponent>()) {
                autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::AudioSource));
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
        data.audioFilePath = comp.audioFilePath;
        data.volume = comp.volume;
        data.pitch = comp.pitch;
        data.loop = comp.loop;
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
        comp.audioFilePath = audioData.audioFilePath;
        comp.volume = audioData.volume;
        comp.pitch = audioData.pitch;
        comp.loop = audioData.loop;
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
            autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::AudioSource));
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
            sceneEntity.removeComponent<components::AudioSource3DComponent>();
            // Only remove billboard if no other audio component exists
            if (!sceneEntity.hasComponent<components::AudioSource2DComponent>()) {
                autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::AudioSource));
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
        data.audioFilePath = comp.audioFilePath;
        data.volume = comp.volume;
        data.pitch = comp.pitch;
        data.loop = comp.loop;
        data.minDistance = comp.minDistance;
        data.maxDistance = comp.maxDistance;
        data.showDebugSpheres = comp.showDebugSpheres;
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
        comp.audioFilePath = audioData.audioFilePath;
        comp.volume = audioData.volume;
        comp.pitch = audioData.pitch;
        comp.loop = audioData.loop;
        comp.minDistance = audioData.minDistance;
        comp.maxDistance = audioData.maxDistance;
        comp.showDebugSpheres = audioData.showDebugSpheres;
        return true;
    }

    void AudioComponentService::autoAttachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.addComponent<components::BillboardComponent>();
            billboard.iconType = static_cast<components::BillboardIconType>(iconType);
            billboard.editorOnly = true;
            billboard.selectable = true;
        }
    }

    void AudioComponentService::autoDetachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
            // Only remove if it matches the expected icon type (auto-attached billboard)
            if (billboard.iconType == static_cast<components::BillboardIconType>(iconType)) {
                sceneEntity.removeComponent<components::BillboardComponent>();
            }
        }
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
    }

}
