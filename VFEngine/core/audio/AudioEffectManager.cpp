#include "AudioEffectManager.hpp"
#include "ReverbPresets.hpp"
#include "print/Log.hpp"
#include <AL/efx-presets.h>
#include <algorithm>

namespace core::audio
{
    void AudioEffectManager::init(int maxAuxSends)
    {
        maxSends = maxAuxSends;
    }

    void AudioEffectManager::cleanUp()
    {
        for (auto& [busId, slots] : busEffects)
        {
            for (auto& slot : slots)
            {
                if (slot.auxSlot)
                {
                    AudioSystem::alDeleteAuxiliaryEffectSlots(1, &slot.auxSlot);
                }
                if (slot.effectObject)
                {
                    AudioSystem::alDeleteEffects(1, &slot.effectObject);
                }
            }
        }
        busEffects.clear();
    }

    bool AudioEffectManager::addEffect(uint32_t busId, const types::BusEffectConfig& config)
    {
        if (!AudioSystem::isEfxAvailable() || maxSends <= 0)
        {
            return false;
        }

        auto& slots = busEffects[busId];
        if (static_cast<int>(slots.size()) >= maxSends)
        {
            vfLogWarning("AudioEffectManager: Bus {} already at max effects ({})", busId, maxSends);
            return false;
        }

        ALuint effect = 0;
        AudioSystem::alGenEffects(1, &effect);
        if (AudioSystem::checkError("alGenEffects"))
        {
            return false;
        }

        ALuint auxSlot = 0;
        AudioSystem::alGenAuxiliaryEffectSlots(1, &auxSlot);
        if (AudioSystem::checkError("alGenAuxiliaryEffectSlots"))
        {
            AudioSystem::alDeleteEffects(1, &effect);
            return false;
        }

        EffectSlotEntry entry;
        entry.configId = nextEffectId++;
        entry.effectObject = effect;
        entry.auxSlot = auxSlot;
        entry.type = config.type;
        entry.enabled = config.enabled;
        entry.wetDryMix = config.wetDryMix;
        entry.params = config.params;

        applyEffectParams(effect, config);

        // Attach effect to aux slot
        AudioSystem::alAuxiliaryEffectSloti(auxSlot, AL_EFFECTSLOT_EFFECT,
            entry.enabled ? static_cast<ALint>(effect) : AL_EFFECT_NULL);
        AudioSystem::checkError("alAuxiliaryEffectSloti attach");

        AudioSystem::alAuxiliaryEffectSlotf(auxSlot, AL_EFFECTSLOT_GAIN, entry.wetDryMix);
        AudioSystem::checkError("alAuxiliaryEffectSlotf gain");

        slots.push_back(entry);
        return true;
    }

    bool AudioEffectManager::removeEffect(uint32_t busId, uint32_t effectId)
    {
        auto it = busEffects.find(busId);
        if (it == busEffects.end())
        {
            return false;
        }

        auto& slots = it->second;
        auto slotIt = std::find_if(slots.begin(), slots.end(),
            [effectId](const EffectSlotEntry& e) { return e.configId == effectId; });

        if (slotIt == slots.end())
        {
            return false;
        }

        if (slotIt->auxSlot)
        {
            // Detach effect first
            AudioSystem::alAuxiliaryEffectSloti(slotIt->auxSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
            AudioSystem::alDeleteAuxiliaryEffectSlots(1, &slotIt->auxSlot);
        }
        if (slotIt->effectObject)
        {
            AudioSystem::alDeleteEffects(1, &slotIt->effectObject);
        }

        slots.erase(slotIt);
        return true;
    }

    bool AudioEffectManager::updateEffectParams(uint32_t busId, uint32_t effectId,
                                                 const types::BusEffectConfig& config)
    {
        auto* entry = findEffect(busId, effectId);
        if (!entry)
        {
            return false;
        }

        applyEffectParams(entry->effectObject, config);

        // Re-attach to update
        if (entry->enabled)
        {
            AudioSystem::alAuxiliaryEffectSloti(entry->auxSlot, AL_EFFECTSLOT_EFFECT,
                static_cast<ALint>(entry->effectObject));
            AudioSystem::checkError("updateEffectParams re-attach");
        }

        entry->params = config.params;
        return true;
    }

    bool AudioEffectManager::setEffectEnabled(uint32_t busId, uint32_t effectId, bool enabled)
    {
        auto* entry = findEffect(busId, effectId);
        if (!entry)
        {
            return false;
        }

        entry->enabled = enabled;
        AudioSystem::alAuxiliaryEffectSloti(entry->auxSlot, AL_EFFECTSLOT_EFFECT,
            enabled ? static_cast<ALint>(entry->effectObject) : AL_EFFECT_NULL);
        AudioSystem::checkError("setEffectEnabled");

        return true;
    }

    bool AudioEffectManager::setEffectWetDry(uint32_t busId, uint32_t effectId, float wetDry)
    {
        auto* entry = findEffect(busId, effectId);
        if (!entry)
        {
            return false;
        }

        entry->wetDryMix = std::clamp(wetDry, 0.0f, 1.0f);
        AudioSystem::alAuxiliaryEffectSlotf(entry->auxSlot, AL_EFFECTSLOT_GAIN, entry->wetDryMix);
        AudioSystem::checkError("setEffectWetDry");

        return true;
    }

    void AudioEffectManager::clearBusEffects(uint32_t busId)
    {
        auto it = busEffects.find(busId);
        if (it == busEffects.end())
        {
            return;
        }

        for (auto& slot : it->second)
        {
            if (slot.auxSlot)
            {
                AudioSystem::alAuxiliaryEffectSloti(slot.auxSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
                AudioSystem::alDeleteAuxiliaryEffectSlots(1, &slot.auxSlot);
            }
            if (slot.effectObject)
            {
                AudioSystem::alDeleteEffects(1, &slot.effectObject);
            }
        }
        it->second.clear();
    }

    void AudioEffectManager::routeSourceToBus(ALuint sourceId, uint32_t busId)
    {
        auto it = busEffects.find(busId);
        if (it == busEffects.end())
        {
            return;
        }

        const auto& slots = it->second;
        for (int i = 0; i < static_cast<int>(slots.size()) && i < maxSends; ++i)
        {
            alSource3i(sourceId, AL_AUXILIARY_SEND_FILTER,
                static_cast<ALint>(slots[i].auxSlot), i, AL_FILTER_NULL);
            AudioSystem::checkError("routeSourceToBus");
        }
    }

    void AudioEffectManager::unrouteSource(ALuint sourceId, uint32_t busId)
    {
        auto it = busEffects.find(busId);
        if (it == busEffects.end())
        {
            return;
        }

        for (int i = 0; i < static_cast<int>(it->second.size()) && i < maxSends; ++i)
        {
            alSource3i(sourceId, AL_AUXILIARY_SEND_FILTER,
                AL_EFFECTSLOT_NULL, i, AL_FILTER_NULL);
            AudioSystem::checkError("unrouteSource");
        }
    }

    std::vector<types::BusEffectConfig> AudioEffectManager::getBusEffectChain(uint32_t busId) const
    {
        std::vector<types::BusEffectConfig> result;
        auto it = busEffects.find(busId);
        if (it == busEffects.end())
        {
            return result;
        }

        for (const auto& slot : it->second)
        {
            types::BusEffectConfig config;
            config.id = slot.configId;
            config.type = slot.type;
            config.enabled = slot.enabled;
            config.wetDryMix = slot.wetDryMix;
            config.params = slot.params;
            result.push_back(config);
        }
        return result;
    }

    const std::vector<EffectSlotEntry>& AudioEffectManager::getBusEffectSlots(uint32_t busId) const
    {
        static const std::vector<EffectSlotEntry> empty;
        auto it = busEffects.find(busId);
        if (it == busEffects.end())
        {
            return empty;
        }
        return it->second;
    }

    EffectSlotEntry* AudioEffectManager::findEffect(uint32_t busId, uint32_t effectId)
    {
        auto it = busEffects.find(busId);
        if (it == busEffects.end())
        {
            return nullptr;
        }
        for (auto& slot : it->second)
        {
            if (slot.configId == effectId)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    const EffectSlotEntry* AudioEffectManager::findEffect(uint32_t busId, uint32_t effectId) const
    {
        auto it = busEffects.find(busId);
        if (it == busEffects.end())
        {
            return nullptr;
        }
        for (const auto& slot : it->second)
        {
            if (slot.configId == effectId)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    void AudioEffectManager::applyEffectParams(ALuint effect, const types::BusEffectConfig& config)
    {
        std::visit([this, effect](const auto& params)
        {
            using T = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<T, types::ReverbParams>)
            {
                if (!params.presetName.empty())
                {
                    auto presetParams = ReverbPresets::fromPreset(params.presetName);
                    applyReverbParams(effect, presetParams);
                }
                else
                {
                    applyReverbParams(effect, params);
                }
            }
            else if constexpr (std::is_same_v<T, types::EQParams>)
            {
                applyEQParams(effect, params);
            }
            else if constexpr (std::is_same_v<T, types::CompressorParams>)
            {
                applyCompressorParams(effect, params);
            }
            else if constexpr (std::is_same_v<T, types::EchoParams>)
            {
                applyEchoParams(effect, params);
            }
            else if constexpr (std::is_same_v<T, types::ChorusParams>)
            {
                applyChorusParams(effect, params);
            }
            else
            {
                static_assert(sizeof(T) == 0, "Unhandled effect parameter type in applyEffectParams");
            }
        }, config.params);
    }

    void AudioEffectManager::applyReverbParams(ALuint effect, const types::ReverbParams& p)
    {
        AudioSystem::alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_DENSITY, p.density);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_DIFFUSION, p.diffusion);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_GAIN, p.gain);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_GAINHF, p.gainHF);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_GAINLF, p.gainLF);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_DECAY_TIME, p.decayTime);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, p.decayHFRatio);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, p.decayLFRatio);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, p.reflectionsGain);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, p.reflectionsDelay);
        AudioSystem::alEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, p.reflectionsPan);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, p.lateReverbGain);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, p.lateReverbDelay);
        AudioSystem::alEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, p.lateReverbPan);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_ECHO_TIME, p.echoTime);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_ECHO_DEPTH, p.echoDepth);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_MODULATION_TIME, p.modulationTime);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_MODULATION_DEPTH, p.modulationDepth);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, p.airAbsorptionGainHF);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_HFREFERENCE, p.hfReference);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_LFREFERENCE, p.lfReference);
        AudioSystem::alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, p.roomRolloffFactor);
        AudioSystem::alEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, p.decayHFLimit);
        AudioSystem::checkError("applyReverbParams");
    }

    void AudioEffectManager::applyEQParams(ALuint effect, const types::EQParams& p)
    {
        AudioSystem::alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EQUALIZER);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_LOW_GAIN, p.lowGain);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_LOW_CUTOFF, p.lowCutoff);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_MID1_GAIN, p.mid1Gain);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_MID1_CENTER, p.mid1Center);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_MID1_WIDTH, p.mid1Width);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_MID2_GAIN, p.mid2Gain);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_MID2_CENTER, p.mid2Center);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_MID2_WIDTH, p.mid2Width);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_HIGH_GAIN, p.highGain);
        AudioSystem::alEffectf(effect, AL_EQUALIZER_HIGH_CUTOFF, p.highCutoff);
        AudioSystem::checkError("applyEQParams");
    }

    void AudioEffectManager::applyCompressorParams(ALuint effect, const types::CompressorParams& p)
    {
        AudioSystem::alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_COMPRESSOR);
        AudioSystem::alEffecti(effect, AL_COMPRESSOR_ONOFF, p.onOff ? AL_TRUE : AL_FALSE);
        AudioSystem::checkError("applyCompressorParams");
    }

    void AudioEffectManager::applyEchoParams(ALuint effect, const types::EchoParams& p)
    {
        AudioSystem::alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_ECHO);
        AudioSystem::alEffectf(effect, AL_ECHO_DELAY, p.delay);
        AudioSystem::alEffectf(effect, AL_ECHO_LRDELAY, p.lrDelay);
        AudioSystem::alEffectf(effect, AL_ECHO_DAMPING, p.damping);
        AudioSystem::alEffectf(effect, AL_ECHO_FEEDBACK, p.feedback);
        AudioSystem::alEffectf(effect, AL_ECHO_SPREAD, p.spread);
        AudioSystem::checkError("applyEchoParams");
    }

    void AudioEffectManager::applyChorusParams(ALuint effect, const types::ChorusParams& p)
    {
        AudioSystem::alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_CHORUS);
        AudioSystem::alEffecti(effect, AL_CHORUS_WAVEFORM, p.waveform);
        AudioSystem::alEffecti(effect, AL_CHORUS_PHASE, p.phase);
        AudioSystem::alEffectf(effect, AL_CHORUS_RATE, p.rate);
        AudioSystem::alEffectf(effect, AL_CHORUS_DEPTH, p.depth);
        AudioSystem::alEffectf(effect, AL_CHORUS_FEEDBACK, p.feedback);
        AudioSystem::alEffectf(effect, AL_CHORUS_DELAY, p.delay);
        AudioSystem::checkError("applyChorusParams");
    }
}
