#include "AudioBusManager.hpp"
#include "AudioEffectManager.hpp"
#include "BusGainPolicy.hpp"
#include "ReverbZoneManager.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace core::audio
{
    void AudioBusManager::replaceBusEffectChainLocked(
        uint32_t busId, const std::vector<types::BusEffectConfig>& chain)
    {
        if (!effectManager)
        {
            return;
        }

        const int maxEffects = effectManager->getMaxEffectsPerBus();
        if (maxEffects < 0 || chain.size() > static_cast<std::size_t>(maxEffects))
        {
            vfLogWarning("AudioBusManager: Snapshot chain for bus {} has {} effects, maximum is {}; keeping current chain",
                         busId, chain.size(), maxEffects);
            return;
        }

        std::vector<ALuint> sourceIds;
        if (sourceResolveCallback)
        {
            sourceIds.reserve(trackedSources.size());
            for (const auto& [handle, tracked] : trackedSources)
            {
                if (tracked.busId != busId)
                {
                    continue;
                }

                const ALuint sourceId = sourceResolveCallback(handle);
                if (sourceId != 0)
                {
                    sourceIds.push_back(sourceId);
                }
            }
        }

        // OpenAL refuses to delete an auxiliary slot while a source send references it.
        for (const ALuint sourceId : sourceIds)
        {
            effectManager->unrouteSource(sourceId, busId);
        }

        effectManager->clearBusEffects(busId);
        for (std::size_t effectIndex = 0; effectIndex < chain.size(); ++effectIndex)
        {
            if (!effectManager->addEffect(busId, chain[effectIndex]))
            {
                vfLogWarning("AudioBusManager: Failed to restore effect {} ({}) on bus {}",
                             effectIndex,
                             types::audioEffectTypeToString(chain[effectIndex].type),
                             busId);
            }
        }

        for (const ALuint sourceId : sourceIds)
        {
            effectManager->routeSourceToBus(sourceId, busId);
        }
    }

    bool AudioBusManager::addBusEffect(const std::string& busName, const types::BusEffectConfig& config)
    {
        std::unique_lock lock(busMutex);
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        if (!getBus(busId)) return false;

        if (!effectManager->addEffect(busId, config)) return false;

        if (sourceResolveCallback)
        {
            for (auto& [handle, tracked] : trackedSources)
            {
                if (tracked.busId == busId)
                {
                    ALuint sourceId = sourceResolveCallback(handle);
                    if (sourceId != 0)
                    {
                        effectManager->routeSourceToBus(sourceId, busId);
                    }
                }
            }
        }
        return true;
    }

    bool AudioBusManager::removeBusEffect(const std::string& busName, uint32_t effectId)
    {
        std::unique_lock lock(busMutex);
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        if (!getBus(busId)) return false;

        if (sourceResolveCallback)
        {
            for (auto& [handle, tracked] : trackedSources)
            {
                if (tracked.busId == busId)
                {
                    ALuint sourceId = sourceResolveCallback(handle);
                    if (sourceId != 0)
                    {
                        effectManager->unrouteSource(sourceId, busId);
                    }
                }
            }
        }

        bool result = effectManager->removeEffect(busId, effectId);

        if (sourceResolveCallback)
        {
            for (auto& [handle, tracked] : trackedSources)
            {
                if (tracked.busId == busId)
                {
                    ALuint sourceId = sourceResolveCallback(handle);
                    if (sourceId != 0)
                    {
                        effectManager->routeSourceToBus(sourceId, busId);
                    }
                }
            }
        }
        return result;
    }

    bool AudioBusManager::updateBusEffect(const std::string& busName, uint32_t effectId,
                                           const types::BusEffectConfig& config)
    {
        std::unique_lock lock(busMutex);
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        return effectManager->updateEffectParams(busId, effectId, config);
    }

    bool AudioBusManager::setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled)
    {
        std::unique_lock lock(busMutex);
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        return effectManager->setEffectEnabled(busId, effectId, enabled);
    }

    bool AudioBusManager::setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry)
    {
        std::unique_lock lock(busMutex);
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        return effectManager->setEffectWetDry(busId, effectId, wetDry);
    }

    std::vector<types::BusEffectConfig> AudioBusManager::getBusEffectChain(const std::string& busName) const
    {
        std::shared_lock lock(busMutex);
        if (!effectManager) return {};
        auto it = nameToId.find(busName);
        if (it == nameToId.end()) return {};
        return effectManager->getBusEffectChain(it->second);
    }

    int AudioBusManager::getMaxEffectsPerBus() const
    {
        if (!effectManager) return 0;
        return effectManager->getMaxEffectsPerBus();
    }

    void AudioBusManager::recalculateEffectiveVolumes()
    {
        bool anySoloed = false;
        for (const auto& bus : buses)
        {
            if (bus.soloed)
            {
                anySoloed = true;
                break;
            }
        }

        for (auto& bus : buses)
        {
            if (bus.name == "Master")
            {
                bus.effectiveVolume = gainpolicy::rootEffectiveVolume(
                    bus.volume, bus.muted, bus.soloed, anySoloed);
                recalculateBusEffective(
                    bus, bus.effectiveVolume, bus.muted, anySoloed);
                break;
            }
        }
    }

    void AudioBusManager::recalculateBusEffective(AudioBus& bus, float parentEffective,
                                                   bool parentMuted, bool anySoloed)
    {
        for (uint32_t childId : bus.childIds)
        {
            auto* child = getBus(childId);
            if (!child) continue;

            bool effectivelyMuted = child->muted || parentMuted;
            child->effectiveVolume = gainpolicy::childEffectiveVolume(
                child->volume, parentEffective, child->muted, parentMuted,
                child->soloed, anySoloed);

            recalculateBusEffective(*child, child->effectiveVolume, effectivelyMuted, anySoloed);
        }
    }

    void AudioBusManager::applyEffectiveVolumesToSources()
    {
        if (!volumeCallback) return;

        for (auto& [handle, tracked] : trackedSources)
        {
            auto* bus = getBus(tracked.busId);
            float effective = bus ? bus->effectiveVolume : 1.0f;
            volumeCallback(handle, tracked.userVolume * effective);
        }
    }
}
