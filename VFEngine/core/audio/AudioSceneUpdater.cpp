#include "AudioSceneUpdater.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/AudioEvents.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include "../../services/data/EntityConversion.hpp"

namespace core::audio {

    void AudioSceneUpdater::update()
    {
        updateAudioSourcePositions();
    }

    void AudioSceneUpdater::updateListenerFromCamera(const glm::vec3& position,
                                                      const glm::vec3& forward,
                                                      const glm::vec3& up)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::audio::SetListenerPositionCommand cmd;
        cmd.position = position;
        cmd.forward = forward;
        cmd.up = up;
        dispatcher.execute(cmd);
    }

    void AudioSceneUpdater::updateListenerFromPrimaryCamera()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent,
                                   components::TransformComponent,
                                   components::WorldTransformComponent>();

        for (auto entityHandle : view)
        {
            const auto& camera = view.get<components::CameraComponent>(entityHandle);

            // Only use the primary camera for listener position
            if (!camera.isPrimary) continue;

            const auto& transform = view.get<components::TransformComponent>(entityHandle);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entityHandle);

            // Extract position from world matrix
            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            // Calculate forward direction from rotation
            float yawRad = glm::radians(transform.rotation.y);
            float pitchRad = glm::radians(transform.rotation.x);
            glm::vec3 forward;
            forward.x = -std::sin(yawRad) * std::cos(pitchRad);
            forward.y = std::sin(pitchRad);
            forward.z = -std::cos(yawRad) * std::cos(pitchRad);
            forward = glm::normalize(forward);

            glm::vec3 up(0.0f, 1.0f, 0.0f);

            updateListenerFromCamera(position, forward, up);

            break;  // Only use first primary camera
        }
    }

    void AudioSceneUpdater::updateAudioSourcePositions()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        // Only 3D audio sources need position updates (2D audio is not spatial)
        auto view = registry.view<components::AudioSource3DComponent,
                                   components::WorldTransformComponent>();

        auto& dispatcher = events::EventDispatcher::instance();

        for (auto entityHandle : view)
        {
            auto& audioSource = view.get<components::AudioSource3DComponent>(entityHandle);

            // Only update 3D audio sources that are currently playing
            if (!audioSource.isPlaying || audioSource.activeHandle == 0)
                continue;

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entityHandle);
            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            events::audio::SetSoundPositionCommand cmd;
            cmd.handle = services::AudioHandle{audioSource.activeHandle};
            cmd.position = position;
            dispatcher.execute(cmd);
        }
    }

    void AudioSceneUpdater::stopAllAudioSources()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::audio::StopAllCommand cmd;
        dispatcher.execute(cmd);

        // Reset all 2D audio source component states
        auto& registry = scene::EntityRegistry::getRegistry();
        {
            auto view = registry.view<components::AudioSource2DComponent>();
            for (auto entityHandle : view)
            {
                auto& audioSource = view.get<components::AudioSource2DComponent>(entityHandle);
                audioSource.activeHandle = 0;
                audioSource.isPlaying = false;
            }
        }

        // Reset all 3D audio source component states
        {
            auto view = registry.view<components::AudioSource3DComponent>();
            for (auto entityHandle : view)
            {
                auto& audioSource = view.get<components::AudioSource3DComponent>(entityHandle);
                audioSource.activeHandle = 0;
                audioSource.isPlaying = false;
            }
        }
    }

}
