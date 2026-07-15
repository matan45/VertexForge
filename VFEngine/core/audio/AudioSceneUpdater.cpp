#include "AudioSceneUpdater.hpp"
#include "ReverbZoneManager.hpp"
#include "OcclusionPolicy.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/audio/AudioEvents.hpp"
#include "../../services/events/physics/PhysicsEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "math/TransformUtils.hpp"
#include <algorithm>
#include <cstdint>

namespace core::audio {

    namespace
    {
        // VK-1518: spread each emitter's ray cadence by a stable per-entity phase, so a
        // scene that starts 40 sounds on one frame doesn't then re-ray all 40 on the same
        // frame forever after. Knuth multiplicative hash — entity ids are small and
        // sequential, which a plain modulo would leave clustered.
        float seedRayPhase(std::uint64_t key)
        {
            const auto mixed = static_cast<std::uint32_t>(key * 2654435761ull);
            const float t = static_cast<float>(mixed % 1000u) / 1000.0f;
            return t * occlusion::kRayInterval;
        }
    }

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
        // VK-1518: cache the ray origin for occlusion. OUTSIDE the reverbZoneManager guard
        // on purpose — a scene with no reverb zones still needs occlusion. This hook is the
        // only listener-position feed both hosts share, so it is what makes occlusion work
        // in the editor at all (see occlusionListenerPosition in the header).
        occlusionListenerPosition = listenerPosition;
        hasOcclusionListener = true;

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

            if (!known)
            {
                // VK-1518: a new voice, or the same entity restarted on a different handle.
                // Force the next occlusion dispatch (the sentinel may already be spent on a
                // reused slot) and re-seed the ray phase. Without this a VK-1515-revived
                // voice would sit on a freshly reset pool source, i.e. audibly un-muffled,
                // until its next scheduled ray.
                entry.occlusionDispatched = -1.0f;
                entry.lpfDispatched = -1.0f;
                entry.volumeDispatched = -1.0f;
                entry.occlusionValue = 0.0f;
                entry.rayAccum = seedRayPhase(key);
            }

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

            // VK-1518: the authored cut amounts ride the occlusion command rather than the
            // play-time config, which costs nothing here (we already hold the component) and
            // makes inspector edits apply live. A disabled emitter reports amount 0, which
            // occlusionGain treats as inert — so unticking the box glides it back to clear
            // rather than freezing it mid-muffle.
            entry.lpfAmount = comp.enableOcclusion ? comp.occlusionLpf : 0.0f;
            entry.volumeAmount = comp.enableOcclusion ? comp.occlusionVolume : 0.0f;

            if (!comp.enableOcclusion || !hasOcclusionListener
                || glm::distance(occlusionListenerPosition, pos) > comp.maxDistance)
            {
                // Off, no listener yet, or already past its own falloff and inaudible —
                // either way it reads as clear and must not spend a ray.
                entry.occlusionValue = 0.0f;
                entry.rayAccum = 0.0f;
            }
            else
            {
                entry.rayAccum += dt;
                if (entry.rayAccum >= occlusion::kRayInterval)
                {
                    OcclusionCandidate candidate;
                    candidate.key = key;
                    candidate.handle = comp.activeHandle;
                    candidate.position = pos;
                    candidate.overdue = entry.rayAccum;
                    candidate.layerMask = comp.occlusionLayerMask;
                    occlusionScratch.push_back(candidate);
                }
            }

            // Per-frame bookkeeping (always): refresh the velocity basis and the seen latch,
            // keeping the last-dispatched pos/dir untouched when not re-synced this frame.
            entry.handle = comp.activeHandle;
            entry.lastFramePosition = pos;
            entry.hasLastFrame = true;
            entry.seenPlaying = seen;
        }

        serveOcclusionRays();
        dispatchOcclusion();
    }

    void AudioSceneUpdater::serveOcclusionRays()
    {
        if (occlusionScratch.empty())
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        const auto budget = static_cast<std::size_t>(kMaxOcclusionRaysPerFrame);
        if (occlusionScratch.size() > budget)
        {
            // Serve the most-overdue first. Skipped candidates keep their accumulated time
            // and win a later frame, so nothing starves; nth_element gets the top N without
            // sorting the tail we're about to drop.
            std::nth_element(occlusionScratch.begin(),
                             occlusionScratch.begin() + kMaxOcclusionRaysPerFrame,
                             occlusionScratch.end(),
                             [](const OcclusionCandidate& a, const OcclusionCandidate& b)
                             { return a.overdue > b.overdue; });
            occlusionScratch.resize(budget);
        }

        for (const auto& candidate : occlusionScratch)
        {
            auto it = emitterCache.find(candidate.key);
            if (it == emitterCache.end() || it->second.handle != candidate.handle)
            {
                continue;
            }
            EmitterCacheEntry& entry = it->second;

            const glm::vec3 toEmitter = candidate.position - occlusionListenerPosition;
            const float distance = glm::length(toEmitter);
            if (distance <= occlusion::kSelfHitSkin)
            {
                // Sitting on top of the listener — no room for a wall, and the ray would
                // have a non-positive length.
                entry.occlusionValue = 0.0f;
                entry.rayAccum = 0.0f;
                continue;
            }

            // Same synchronous main-thread pattern as ViewPort's pick ray. Safe here: both
            // hosts order the audio task after PhysicsSync, so this frame's step is done.
            // CQRS structs are not aggregates — default-construct, then assign.
            events::physics::RaycastQuery rayQuery;
            rayQuery.origin = occlusionListenerPosition;
            rayQuery.direction = toEmitter / distance;
            // Stop short of the emitter so its OWN collider isn't mistaken for a wall.
            rayQuery.maxDistance = distance - occlusion::kSelfHitSkin;
            rayQuery.layerMask = candidate.layerMask;
            const services::RaycastHit hit = dispatcher.query(rayQuery);

            entry.occlusionValue = occlusion::occlusionFromRay(hit.hit, hit.distance, distance,
                                                               occlusion::kSelfHitSkin);
            entry.rayAccum = 0.0f;
        }

        occlusionScratch.clear();
    }

    void AudioSceneUpdater::dispatchOcclusion()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // A separate dispatch from the transform re-sync above, and deliberately so: that one
        // is gated on the emitter MOVING, but occlusion changes when the WORLD moves. A
        // stationary source behind a closing door has to be told.
        for (auto& cached : emitterCache)
        {
            EmitterCacheEntry& entry = cached.second;
            if (entry.handle == 0)
            {
                continue;
            }

            const bool dirty = entry.occlusionDispatched != entry.occlusionValue
                            || entry.lpfDispatched != entry.lpfAmount
                            || entry.volumeDispatched != entry.volumeAmount;
            if (!dirty)
            {
                continue;
            }

            events::audio::SetSoundOcclusionCommand cmd;
            cmd.handle = services::AudioHandle{entry.handle};
            cmd.occlusion = entry.occlusionValue;
            cmd.lpfAmount = entry.lpfAmount;
            cmd.volumeAmount = entry.volumeAmount;
            dispatcher.execute(cmd);

            entry.occlusionDispatched = entry.occlusionValue;
            entry.lpfDispatched = entry.lpfAmount;
            entry.volumeDispatched = entry.volumeAmount;
        }
    }

}
