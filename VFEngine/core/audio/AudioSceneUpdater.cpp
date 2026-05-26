#include "AudioSceneUpdater.hpp"
#include "ReverbZoneManager.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/audio/AudioEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"

namespace core::audio {

    void AudioSceneUpdater::updateListenerFromCamera(const glm::vec3& position,
                                                      const glm::vec3& forward,
                                                      const glm::vec3& up)
    {
        static int logBudget = 3;
        const bool log = logBudget > 0;
        auto trace = [&](const char* stage) {
            if (log) vfLogInfo("[AudioSceneUpdater::updateListenerFromCamera] {}", stage);
        };

        trace("entry");
        auto& dispatcher = events::EventDispatcher::instance();
        trace("got dispatcher");
        events::audio::SetListenerPositionCommand cmd;
        cmd.position = position;
        cmd.forward = forward;
        cmd.up = up;
        trace("about to dispatch SetListenerPositionCommand");
        dispatcher.execute(cmd);
        trace("SetListenerPositionCommand returned");

        // Update reverb zones based on listener position
        if (reverbZoneManager)
        {
            trace("reverbZoneManager non-null, calling update");
            auto& registry = scene::EntityRegistry::getRegistry();
            reverbZoneManager->update(position, registry);
            trace("reverbZoneManager->update returned");
        }
        else
        {
            trace("reverbZoneManager null, skipping");
        }
        trace("exit");
        if (log) logBudget--;
    }

    void AudioSceneUpdater::updateListenerFromPrimaryCamera()
    {
        // Diagnostic: rate-limited per-call logs to find which line aborts
        // (VK-1330 / VK-1333 investigation).
        static int logBudget = 3;
        const bool log = logBudget > 0;
        auto trace = [&](const char* stage) {
            if (log) vfLogInfo("[AudioSceneUpdater] {}", stage);
        };

        trace("entry");
        auto& registry = scene::EntityRegistry::getRegistry();
        trace("got registry");
        auto view = registry.view<components::CameraComponent,
                                   components::TransformComponent,
                                   components::WorldTransformComponent>();
        trace("got view");

        for (auto entityHandle : view)
        {
            if (log) vfLogInfo("[AudioSceneUpdater] iter entity {}",
                               static_cast<uint32_t>(entityHandle));
            // Skip inactive entities
            if (registry.all_of<components::NameComponent>(entityHandle))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entityHandle);
                if (!nameComp.isActive)
                {
                    trace("inactive, skipping");
                    continue;
                }
            }

            trace("about to read CameraComponent");
            const auto& camera = view.get<components::CameraComponent>(entityHandle);
            trace("got CameraComponent");

            // Only use the primary camera for listener position
            if (!camera.isPrimary) { trace("not primary, skipping"); continue; }

            trace("about to read TransformComponent");
            const auto& transform = view.get<components::TransformComponent>(entityHandle);
            trace("about to read WorldTransformComponent");
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entityHandle);
            trace("read components");

            // Extract position from world matrix
            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);
            trace("computed position");

            // Calculate forward direction from rotation
            float yawRad = glm::radians(transform.rotation.y);
            float pitchRad = glm::radians(transform.rotation.x);
            glm::vec3 forward;
            forward.x = -std::sin(yawRad) * std::cos(pitchRad);
            forward.y = std::sin(pitchRad);
            forward.z = -std::cos(yawRad) * std::cos(pitchRad);
            forward = glm::normalize(forward);
            trace("computed forward");

            glm::vec3 up(0.0f, 1.0f, 0.0f);

            trace("about to updateListenerFromCamera");
            updateListenerFromCamera(position, forward, up);
            trace("updateListenerFromCamera returned");

            break;  // Only use first primary camera
        }
        trace("exit");
        if (log) logBudget--;
    }

}
