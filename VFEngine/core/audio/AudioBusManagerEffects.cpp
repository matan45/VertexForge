#include "AudioBusManager.hpp"
#include "AudioEffectManager.hpp"
#include "ReverbZoneManager.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace core::audio
{
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
                float masterVol = bus.volume;
                if (anySoloed && !bus.soloed) masterVol = 0.0f;
                if (!anySoloed && bus.muted) masterVol = 0.0f;

                bus.effectiveVolume = masterVol;
                recalculateBusEffective(bus, masterVol, bus.muted, anySoloed);
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

            if (anySoloed)
            {
                if (child->soloed)
                {
                    child->effectiveVolume = child->volume * parentEffective;
                    // Parent is not soloed, but child is -- use own volume
                    if (parentEffective == 0.0f)
                    {
                        child->effectiveVolume = child->volume;
                    }
                }
                else
                {
                    child->effectiveVolume = 0.0f;
                }
            }
            else
            {
                if (effectivelyMuted)
                {
                    child->effectiveVolume = 0.0f;
                }
                else
                {
                    child->effectiveVolume = child->volume * parentEffective;
                }
            }

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
