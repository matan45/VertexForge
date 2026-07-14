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
                                                      const glm::vec3& up,
                                                      float dt)
    {
        // VK-1506: listener velocity from a per-frame finite difference (runtime path only;
        // the editor dispatches the listener from ViewPort with velocity 0). dt <= 0 or a
        // teleport-sized jump yields zero, so a camera cut never chirps.
        glm::vec3 velocity(0.0f);
        if (hasLastListener)
            velocity = math::computeClampedVelocity(lastListenerPosition, position, dt, maxDopplerSpeed);
        lastListenerPosition = position;
        hasLastListener = true;

        auto& dispatcher = events::EventDispatcher::instance();
        events::audio::SetListenerPositionCommand cmd;
        cmd.position = position;
        cmd.forward = forward;
        cmd.up = up;
        cmd.velocity = velocity;
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

    void AudioSceneUpdater::updateListenerFromPrimaryCamera(float dt)
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

            updateListenerFromCamera(position, forward, up, dt);

            break;  // Only use first primary camera
        }
    }

    void AudioSceneUpdater::updateEmitters(float dt)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();

        // 1 mm / unit-vector delta before a source is re-dispatched — a stationary
        // emitter costs one map lookup and zero commands per frame.
        constexpr float kEmitterEps = 1e-3f;

        // VK-1506: below this speed (mm/s) an emitter is treated as at rest — the doppler
        // velocity is flushed to zero so a stopped source stops pitch-shifting.
        constexpr float kVelocityEps = 1e-3f;

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

            // Existing slot, or a fresh default one. known/seen were captured from `it` above;
            // `it` is not used past this point (this may insert and invalidate it).
            EmitterCacheEntry& entry = emitterCache[key];

            // VK-1506: velocity from a single-frame finite difference. The basis
            // (lastFramePosition) is refreshed EVERY frame below — independent of the
            // dispatch dirty-check — so slow movers aren't over-estimated by a stale delta.
            // A handle change resets the basis (guarded by matching handle); dt <= 0 or a
            // teleport-sized jump yields zero (no chirp).
            glm::vec3 velocity(0.0f);
            if (entry.hasLastFrame && entry.handle == comp.activeHandle)
                velocity = math::computeClampedVelocity(entry.lastFramePosition, pos, dt, maxDopplerSpeed);
            const bool moving = glm::dot(velocity, velocity) > kVelocityEps * kVelocityEps;

            // Dirty-check against the LAST DISPATCHED transform (not last frame), so slow
            // sub-epsilon drift still accumulates to a re-sync instead of being lost forever.
            // The final term flushes a single zero-velocity update when a moving source comes
            // to rest, so OpenAL's AL_VELOCITY doesn't stay stuck (phantom doppler).
            const bool dirty = !known
                            || math::positionMovedBeyond(entry.position, pos, kEmitterEps)
                            || math::positionMovedBeyond(entry.direction, dir, kEmitterEps)
                            || (entry.velocityDispatched && !moving);

            if (dirty)
            {
                events::audio::SetSoundTransformCommand cmd;
                cmd.handle = services::AudioHandle{comp.activeHandle};
                cmd.position = pos;
                cmd.direction = dir;
                cmd.velocity = velocity;
                dispatcher.execute(cmd);
                entry.position = pos;
                entry.direction = dir;
                entry.velocityDispatched = moving;
            }

            // Per-frame bookkeeping (always): refresh the velocity basis and the seen latch,
            // keeping the last-dispatched pos/dir untouched when not re-synced this frame.
            entry.handle = comp.activeHandle;
            entry.lastFramePosition = pos;
            entry.hasLastFrame = true;
            entry.seenPlaying = seen;
        }
    }

}
