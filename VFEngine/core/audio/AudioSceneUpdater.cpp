#include "AudioSceneUpdater.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/audio/AudioEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core::audio {

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
            // Skip inactive entities
            if (registry.all_of<components::NameComponent>(entityHandle))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entityHandle);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

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

}
