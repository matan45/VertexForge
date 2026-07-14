#include "AudioSceneUpdater.hpp"
#include "ReverbZoneManager.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/audio/AudioEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "math/TransformUtils.hpp"
#include <cstdint>

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

        updateReverbZones(position);
    }

    void AudioSceneUpdater::updateReverbZones(const glm::vec3& listenerPosition)
    {
        if (reverbZoneManager)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            reverbZoneManager->update(listenerPosition, registry);
        }
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

            // Calculate forward direction from rotation (shared helper — single source of truth)
            glm::vec3 forward = math::forwardFromEulerDegrees(transform.rotation);

            glm::vec3 up(0.0f, 1.0f, 0.0f);

            updateListenerFromCamera(position, forward, up);

            break;  // Only use first primary camera
        }
    }

    void AudioSceneUpdater::updateEmitters(float dt)
    {
        (void)dt;  // Reserved for VK-1506 doppler: velocity = (pos - cachedPos) / dt.

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();

        // 1 mm / unit-vector delta before a source is re-dispatched — a stationary
        // emitter costs one map lookup and zero commands per frame.
        constexpr float kEmitterEps = 1e-3f;

        auto view = registry.view<components::AudioSource3DComponent,
                                  components::TransformComponent,
                                  components::WorldTransformComponent>();

        for (auto entity : view)
        {
            auto& comp = view.get<components::AudioSource3DComponent>(entity);
            const std::uint64_t key = static_cast<std::uint64_t>(entity);

            if (comp.activeHandle == 0 || !comp.isPlaying)
            {
                emitterCache.erase(key);
                continue;
            }

            // Race-safe stale-handle reset (VK-1354-aware): the lock-free snapshot may not
            // yet show a just-started handle, so "not playing" only counts as "finished"
            // once we have positively observed it playing at least once (seenPlaying latch).
            events::audio::IsSoundPlayingQuery playingQuery;
            playingQuery.handle = services::AudioHandle{comp.activeHandle};
            const bool nowPlaying = dispatcher.query(playingQuery);

            auto it = emitterCache.find(key);
            const bool known = (it != emitterCache.end() && it->second.handle == comp.activeHandle);

            bool seen = known ? it->second.seenPlaying : false;
            seen = seen || nowPlaying;

            if (seen && !nowPlaying)
            {
                // Genuinely finished — clear stale runtime flags and drop the cache entry.
                comp.isPlaying = false;
                comp.activeHandle = 0;
                emitterCache.erase(key);
                continue;
            }

            // Follow: re-sync position/direction from the entity's live world transform.
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);
            const glm::vec3 pos = glm::vec3(worldTransform.worldMatrix[3]);
            const glm::vec3 dir = math::forwardFromEulerDegrees(transform.rotation);

            // Dirty-check against the LAST DISPATCHED transform (not last frame), so slow
            // sub-epsilon drift still accumulates to a re-sync instead of being lost forever.
            const bool dirty = !known
                            || math::positionMovedBeyond(it->second.position, pos, kEmitterEps)
                            || math::positionMovedBeyond(it->second.direction, dir, kEmitterEps);

            if (dirty)
            {
                events::audio::SetSoundTransformCommand cmd;
                cmd.handle = services::AudioHandle{comp.activeHandle};
                cmd.position = pos;
                cmd.direction = dir;
                cmd.velocity = glm::vec3(0.0f);  // VK-1506 fills this from (pos - cachedPos)/dt.
                dispatcher.execute(cmd);
                emitterCache[key] = EmitterCacheEntry{comp.activeHandle, pos, dir, seen};
            }
            else
            {
                // Not re-synced this frame: keep the last-dispatched pos/dir, refresh the latch.
                it->second.seenPlaying = seen;
            }
        }
    }

}
