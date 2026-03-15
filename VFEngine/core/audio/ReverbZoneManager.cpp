#include "ReverbZoneManager.hpp"
#include "ReverbPresets.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>

namespace core::audio
{
    void ReverbZoneManager::init(int reservedSendIndex)
    {
        reservedSend = reservedSendIndex;

        if (!AudioSystem::isEfxAvailable() || reservedSendIndex < 0)
        {
            enabled = false;
            return;
        }

        AudioSystem::alGenEffects(1, &effectObject);
        if (AudioSystem::checkError("ReverbZoneManager alGenEffects"))
        {
            enabled = false;
            return;
        }

        AudioSystem::alGenAuxiliaryEffectSlots(1, &auxSlot);
        if (AudioSystem::checkError("ReverbZoneManager alGenAuxiliaryEffectSlots"))
        {
            AudioSystem::alDeleteEffects(1, &effectObject);
            effectObject = 0;
            enabled = false;
            return;
        }

        // Start with no effect attached
        AudioSystem::alAuxiliaryEffectSloti(auxSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);

        enabled = true;
        reverbActive = false;
    }

    void ReverbZoneManager::cleanUp()
    {
        if (auxSlot)
        {
            AudioSystem::alAuxiliaryEffectSloti(auxSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
            AudioSystem::alDeleteAuxiliaryEffectSlots(1, &auxSlot);
            auxSlot = 0;
        }
        if (effectObject)
        {
            AudioSystem::alDeleteEffects(1, &effectObject);
            effectObject = 0;
        }
        routedSources.clear();
        enabled = false;
    }

    void ReverbZoneManager::update(const glm::vec3& listenerPos, entt::registry& registry)
    {
        if (!enabled) return;

        std::vector<ActiveZone> activeZones;

        auto view = registry.view<components::ReverbZoneComponent, components::WorldTransformComponent>();
        for (auto entity : view)
        {
            auto& zone = view.get<components::ReverbZoneComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            // Skip inactive entities
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive) continue;
            }

            glm::vec3 zonePos = glm::vec3(worldTransform.worldMatrix[3]);
            float signedDist = 0.0f;

            if (zone.shape == components::ReverbZoneShape::Sphere)
            {
                signedDist = glm::distance(listenerPos, zonePos) - zone.radius;
            }
            else // Box
            {
                // Transform listener to zone's local space
                glm::mat4 invWorld = glm::inverse(worldTransform.worldMatrix);
                glm::vec3 localPos = glm::vec3(invWorld * glm::vec4(listenerPos, 1.0f));

                // Signed distance to AABB centered at origin
                glm::vec3 d = glm::abs(localPos) - zone.halfExtents;
                glm::vec3 clamped = glm::max(d, glm::vec3(0.0f));
                float outside = glm::length(clamped);
                float inside = glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
                signedDist = outside + inside;
            }

            // Update runtime state
            zone.isListenerInside = (signedDist <= 0.0f);

            if (signedDist < zone.falloffDistance)
            {
                float weight;
                if (signedDist <= 0.0f)
                {
                    weight = 1.0f;
                }
                else
                {
                    weight = 1.0f - (signedDist / zone.falloffDistance);
                }
                weight *= zone.wetLevel;
                zone.currentBlendWeight = weight;

                if (weight > 0.0f)
                {
                    ActiveZone az;
                    az.priority = zone.priority;
                    az.blendWeight = weight;

                    if (!zone.presetName.empty())
                    {
                        az.params = ReverbPresets::fromPreset(zone.presetName);
                    }
                    else
                    {
                        az.params = zone.customParams;
                    }

                    activeZones.push_back(az);
                }
            }
            else
            {
                zone.currentBlendWeight = 0.0f;
            }
        }

        if (activeZones.empty())
        {
            if (reverbActive)
            {
                AudioSystem::alAuxiliaryEffectSloti(auxSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
                reverbActive = false;
            }
            return;
        }

        // Sort by priority descending
        std::sort(activeZones.begin(), activeZones.end(),
            [](const ActiveZone& a, const ActiveZone& b) { return a.priority > b.priority; });

        types::ReverbParams finalParams;

        if (activeZones.size() == 1)
        {
            finalParams = activeZones[0].params;
            // Scale gain by blend weight for fade-in/out
            finalParams.gain *= activeZones[0].blendWeight;
        }
        else
        {
            // Blend between top two priority zones
            const auto& primary = activeZones[0];
            const auto& secondary = activeZones[1];

            if (primary.blendWeight >= 1.0f)
            {
                finalParams = primary.params;
            }
            else
            {
                // Cross-fade: primary at its weight, secondary fills remainder
                float t = primary.blendWeight;
                finalParams = lerpParams(secondary.params, primary.params, t);
            }
        }

        applyReverbToEffect(finalParams);

        AudioSystem::alAuxiliaryEffectSloti(auxSlot, AL_EFFECTSLOT_EFFECT,
            static_cast<ALint>(effectObject));
        AudioSystem::checkError("ReverbZoneManager attach effect");

        reverbActive = true;
    }

    void ReverbZoneManager::routeSource(ALuint sourceId)
    {
        if (!enabled || sourceId == 0) return;

        alSource3i(sourceId, AL_AUXILIARY_SEND_FILTER,
            static_cast<ALint>(auxSlot), reservedSend, AL_FILTER_NULL);
        AudioSystem::checkError("ReverbZoneManager routeSource");

        // Track for future reference
        if (std::find(routedSources.begin(), routedSources.end(), sourceId) == routedSources.end())
        {
            routedSources.push_back(sourceId);
        }
    }

    void ReverbZoneManager::unrouteSource(ALuint sourceId)
    {
        if (!enabled || sourceId == 0) return;

        alSource3i(sourceId, AL_AUXILIARY_SEND_FILTER,
            AL_EFFECTSLOT_NULL, reservedSend, AL_FILTER_NULL);
        AudioSystem::checkError("ReverbZoneManager unrouteSource");

        routedSources.erase(
            std::remove(routedSources.begin(), routedSources.end(), sourceId),
            routedSources.end());
    }

    types::ReverbParams ReverbZoneManager::lerpParams(const types::ReverbParams& a,
                                                       const types::ReverbParams& b, float t) const
    {
        types::ReverbParams r;
        auto mix = [t](float va, float vb) { return va + (vb - va) * t; };

        r.density = mix(a.density, b.density);
        r.diffusion = mix(a.diffusion, b.diffusion);
        r.gain = mix(a.gain, b.gain);
        r.gainHF = mix(a.gainHF, b.gainHF);
        r.gainLF = mix(a.gainLF, b.gainLF);
        r.decayTime = mix(a.decayTime, b.decayTime);
        r.decayHFRatio = mix(a.decayHFRatio, b.decayHFRatio);
        r.decayLFRatio = mix(a.decayLFRatio, b.decayLFRatio);
        r.reflectionsGain = mix(a.reflectionsGain, b.reflectionsGain);
        r.reflectionsDelay = mix(a.reflectionsDelay, b.reflectionsDelay);
        for (int i = 0; i < 3; ++i)
        {
            r.reflectionsPan[i] = mix(a.reflectionsPan[i], b.reflectionsPan[i]);
        }
        r.lateReverbGain = mix(a.lateReverbGain, b.lateReverbGain);
        r.lateReverbDelay = mix(a.lateReverbDelay, b.lateReverbDelay);
        for (int i = 0; i < 3; ++i)
        {
            r.lateReverbPan[i] = mix(a.lateReverbPan[i], b.lateReverbPan[i]);
        }
        r.echoTime = mix(a.echoTime, b.echoTime);
        r.echoDepth = mix(a.echoDepth, b.echoDepth);
        r.modulationTime = mix(a.modulationTime, b.modulationTime);
        r.modulationDepth = mix(a.modulationDepth, b.modulationDepth);
        r.airAbsorptionGainHF = mix(a.airAbsorptionGainHF, b.airAbsorptionGainHF);
        r.hfReference = mix(a.hfReference, b.hfReference);
        r.lfReference = mix(a.lfReference, b.lfReference);
        r.roomRolloffFactor = mix(a.roomRolloffFactor, b.roomRolloffFactor);
        r.decayHFLimit = (t >= 0.5f) ? b.decayHFLimit : a.decayHFLimit;
        return r;
    }

    void ReverbZoneManager::applyReverbToEffect(const types::ReverbParams& p)
    {
        AudioSystem::alEffecti(effectObject, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_DENSITY, p.density);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_DIFFUSION, p.diffusion);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_GAIN, p.gain);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_GAINHF, p.gainHF);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_GAINLF, p.gainLF);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_DECAY_TIME, p.decayTime);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_DECAY_HFRATIO, p.decayHFRatio);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_DECAY_LFRATIO, p.decayLFRatio);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_REFLECTIONS_GAIN, p.reflectionsGain);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_REFLECTIONS_DELAY, p.reflectionsDelay);
        AudioSystem::alEffectfv(effectObject, AL_EAXREVERB_REFLECTIONS_PAN, p.reflectionsPan);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_LATE_REVERB_GAIN, p.lateReverbGain);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_LATE_REVERB_DELAY, p.lateReverbDelay);
        AudioSystem::alEffectfv(effectObject, AL_EAXREVERB_LATE_REVERB_PAN, p.lateReverbPan);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_ECHO_TIME, p.echoTime);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_ECHO_DEPTH, p.echoDepth);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_MODULATION_TIME, p.modulationTime);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_MODULATION_DEPTH, p.modulationDepth);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, p.airAbsorptionGainHF);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_HFREFERENCE, p.hfReference);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_LFREFERENCE, p.lfReference);
        AudioSystem::alEffectf(effectObject, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, p.roomRolloffFactor);
        AudioSystem::alEffecti(effectObject, AL_EAXREVERB_DECAY_HFLIMIT, p.decayHFLimit);
        AudioSystem::checkError("ReverbZoneManager applyReverbToEffect");
    }
}
