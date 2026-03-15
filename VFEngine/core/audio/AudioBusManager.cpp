#include "AudioBusManager.hpp"
#include "AudioEffectManager.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace core::audio
{
    void AudioBusManager::init(VolumeApplyCallback callback)
    {
        volumeCallback = std::move(callback);
    }

    void AudioBusManager::setEffectManager(AudioEffectManager* manager)
    {
        effectManager = manager;
    }

    void AudioBusManager::setSourceResolveCallback(SourceResolveCallback callback)
    {
        sourceResolveCallback = std::move(callback);
    }

    void AudioBusManager::cleanUp()
    {
        trackedSources.clear();
        snapshots.clear();
        buses.clear();
        nameToId.clear();
        nextBusId = 0;
    }

    uint32_t AudioBusManager::createBus(const std::string& name, const std::string& parentName)
    {
        if (nameToId.count(name))
        {
            return nameToId[name];
        }

        uint32_t parentId = 0;
        if (!parentName.empty() && parentName != name)
        {
            auto it = nameToId.find(parentName);
            if (it != nameToId.end())
            {
                parentId = it->second;
            }
        }

        AudioBus bus;
        bus.id = nextBusId++;
        bus.name = name;
        bus.parentId = parentId;

        if (bus.id != parentId)
        {
            for (auto& b : buses)
            {
                if (b.id == parentId)
                {
                    b.childIds.push_back(bus.id);
                    break;
                }
            }
        }

        nameToId[name] = bus.id;
        buses.push_back(std::move(bus));

        recalculateEffectiveVolumes();
        return buses.back().id;
    }

    AudioBus* AudioBusManager::getBus(uint32_t busId)
    {
        for (auto& bus : buses)
        {
            if (bus.id == busId) return &bus;
        }
        return nullptr;
    }

    AudioBus* AudioBusManager::getBusByName(const std::string& name)
    {
        auto it = nameToId.find(name);
        if (it == nameToId.end()) return nullptr;
        return getBus(it->second);
    }

    uint32_t AudioBusManager::getBusIdByName(const std::string& name) const
    {
        auto it = nameToId.find(name);
        return it != nameToId.end() ? it->second : 0;
    }

    std::vector<std::string> AudioBusManager::getBusNames() const
    {
        std::vector<std::string> names;
        names.reserve(buses.size());
        for (const auto& bus : buses)
        {
            names.push_back(bus.name);
        }
        return names;
    }

    void AudioBusManager::setBusVolume(const std::string& name, float volume)
    {
        auto* bus = getBusByName(name);
        if (!bus) return;

        bus->volume = std::clamp(volume, 0.0f, 1.0f);
        recalculateEffectiveVolumes();
        applyEffectiveVolumesToSources();
    }

    void AudioBusManager::setBusMuted(const std::string& name, bool muted)
    {
        auto* bus = getBusByName(name);
        if (!bus) return;

        bus->muted = muted;
        recalculateEffectiveVolumes();
        applyEffectiveVolumesToSources();
    }

    void AudioBusManager::setBusSoloed(const std::string& name, bool soloed)
    {
        auto* bus = getBusByName(name);
        if (!bus) return;

        bus->soloed = soloed;
        recalculateEffectiveVolumes();
        applyEffectiveVolumesToSources();
    }

    float AudioBusManager::getBusVolume(const std::string& name) const
    {
        auto it = nameToId.find(name);
        if (it == nameToId.end()) return 1.0f;

        for (const auto& bus : buses)
        {
            if (bus.id == it->second) return bus.volume;
        }
        return 1.0f;
    }

    bool AudioBusManager::isBusMuted(const std::string& name) const
    {
        auto it = nameToId.find(name);
        if (it == nameToId.end()) return false;

        for (const auto& bus : buses)
        {
            if (bus.id == it->second) return bus.muted;
        }
        return false;
    }

    bool AudioBusManager::isBusSoloed(const std::string& name) const
    {
        auto it = nameToId.find(name);
        if (it == nameToId.end()) return false;

        for (const auto& bus : buses)
        {
            if (bus.id == it->second) return bus.soloed;
        }
        return false;
    }

    void AudioBusManager::assignSource(AudioHandle handle, const std::string& busName, float userVolume)
    {
        uint32_t busId = getBusIdByName(busName);

        TrackedSource tracked;
        tracked.handle = handle;
        tracked.busId = busId;
        tracked.userVolume = userVolume;
        trackedSources[handle] = tracked;

        auto* bus = getBus(busId);
        float effective = bus ? bus->effectiveVolume : 1.0f;

        if (volumeCallback)
        {
            volumeCallback(handle, userVolume * effective);
        }

        // Route source to bus effects
        if (effectManager && sourceResolveCallback)
        {
            ALuint sourceId = sourceResolveCallback(handle);
            if (sourceId != 0)
            {
                effectManager->routeSourceToBus(sourceId, busId);
            }
        }
    }

    void AudioBusManager::removeSource(AudioHandle handle)
    {
        auto it = trackedSources.find(handle);
        if (it != trackedSources.end())
        {
            // Unroute source from bus effects
            if (effectManager && sourceResolveCallback)
            {
                ALuint sourceId = sourceResolveCallback(handle);
                if (sourceId != 0)
                {
                    effectManager->unrouteSource(sourceId, it->second.busId);
                }
            }
            trackedSources.erase(it);
        }
    }

    void AudioBusManager::setSourceUserVolume(AudioHandle handle, float volume)
    {
        auto it = trackedSources.find(handle);
        if (it == trackedSources.end()) return;

        it->second.userVolume = volume;

        auto* bus = getBus(it->second.busId);
        float effective = bus ? bus->effectiveVolume : 1.0f;

        if (volumeCallback)
        {
            volumeCallback(handle, volume * effective);
        }
    }

    void AudioBusManager::saveSnapshot(const std::string& name)
    {
        MixSnapshot snapshot;
        snapshot.name = name;
        for (const auto& bus : buses)
        {
            snapshot.busVolumes[bus.name] = bus.volume;
            snapshot.busMutes[bus.name] = bus.muted;

            if (effectManager)
            {
                auto chain = effectManager->getBusEffectChain(bus.id);
                if (!chain.empty())
                {
                    snapshot.busEffects[bus.name] = chain;
                }
            }
        }
        snapshots[name] = std::move(snapshot);
    }

    void AudioBusManager::loadSnapshot(const std::string& name)
    {
        auto it = snapshots.find(name);
        if (it == snapshots.end()) return;

        const auto& snapshot = it->second;
        for (auto& bus : buses)
        {
            auto volIt = snapshot.busVolumes.find(bus.name);
            if (volIt != snapshot.busVolumes.end())
            {
                bus.volume = volIt->second;
            }

            auto muteIt = snapshot.busMutes.find(bus.name);
            if (muteIt != snapshot.busMutes.end())
            {
                bus.muted = muteIt->second;
            }
        }

        recalculateEffectiveVolumes();
        applyEffectiveVolumesToSources();
    }

    void AudioBusManager::deleteSnapshot(const std::string& name)
    {
        snapshots.erase(name);
    }

    std::vector<std::string> AudioBusManager::getSnapshotNames() const
    {
        std::vector<std::string> names;
        for (const auto& [name, _] : snapshots)
        {
            names.push_back(name);
        }
        return names;
    }

    void AudioBusManager::createDefaultBuses()
    {
        createBus("Master", "");
        createBus("Music", "Master");
        createBus("SFX", "Master");
        createBus("Dialogue", "Master");
        createBus("Ambient", "Master");
    }

    void AudioBusManager::loadBusDefinitions(const std::vector<types::AudioBusDefinition>& definitions)
    {
        // Clear existing effects before clearing buses
        if (effectManager)
        {
            for (const auto& bus : buses)
            {
                effectManager->clearBusEffects(bus.id);
            }
        }

        buses.clear();
        nameToId.clear();
        nextBusId = 0;

        if (definitions.empty())
        {
            createDefaultBuses();
            return;
        }

        // First pass: create all buses (Master first)
        bool hasMaster = false;
        for (const auto& def : definitions)
        {
            if (def.name == "Master")
            {
                hasMaster = true;
                break;
            }
        }

        if (!hasMaster)
        {
            createBus("Master", "");
        }

        for (const auto& def : definitions)
        {
            if (def.name == "Master")
            {
                createBus("Master", "");
                auto* bus = getBusByName("Master");
                if (bus) bus->volume = def.defaultVolume;
            }
        }

        for (const auto& def : definitions)
        {
            if (def.name != "Master")
            {
                createBus(def.name, def.parentName);
                auto* bus = getBusByName(def.name);
                if (bus) bus->volume = def.defaultVolume;
            }
        }

        // Load effect chains for each bus
        if (effectManager)
        {
            for (const auto& def : definitions)
            {
                uint32_t busId = getBusIdByName(def.name);
                for (const auto& effectConfig : def.effects)
                {
                    effectManager->addEffect(busId, effectConfig);
                }
            }
        }

        recalculateEffectiveVolumes();
    }

    void AudioBusManager::loadSnapshots(const std::vector<types::AudioMixSnapshotDefinition>& snapshotDefs)
    {
        snapshots.clear();
        for (const auto& def : snapshotDefs)
        {
            MixSnapshot snapshot;
            snapshot.name = def.name;
            snapshot.busVolumes = def.busVolumes;
            snapshot.busMutes = def.busMutes;
            snapshots[def.name] = std::move(snapshot);
        }
    }

    std::vector<types::AudioMixSnapshotDefinition> AudioBusManager::getSnapshotDefinitions() const
    {
        std::vector<types::AudioMixSnapshotDefinition> defs;
        for (const auto& [name, snapshot] : snapshots)
        {
            types::AudioMixSnapshotDefinition def;
            def.name = snapshot.name;
            def.busVolumes = snapshot.busVolumes;
            def.busMutes = snapshot.busMutes;
            defs.push_back(std::move(def));
        }
        return defs;
    }

    bool AudioBusManager::addBusEffect(const std::string& busName, const types::BusEffectConfig& config)
    {
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        if (!getBus(busId)) return false;

        if (!effectManager->addEffect(busId, config)) return false;

        // Route all existing sources on this bus to the updated effect chain
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
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        if (!getBus(busId)) return false;

        // Unroute sources first, then remove, then re-route
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

        // Re-route with remaining effects
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
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        return effectManager->updateEffectParams(busId, effectId, config);
    }

    bool AudioBusManager::setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled)
    {
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        return effectManager->setEffectEnabled(busId, effectId, enabled);
    }

    bool AudioBusManager::setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry)
    {
        if (!effectManager) return false;
        uint32_t busId = getBusIdByName(busName);
        return effectManager->setEffectWetDry(busId, effectId, wetDry);
    }

    std::vector<types::BusEffectConfig> AudioBusManager::getBusEffectChain(const std::string& busName) const
    {
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

        // Find Master bus and start recursion
        for (auto& bus : buses)
        {
            if (bus.name == "Master")
            {
                bool masterMuted = bus.muted;
                float masterVol = (masterMuted && !bus.soloed && anySoloed) ? 0.0f : bus.volume;
                if (bus.muted && !anySoloed) masterVol = 0.0f;
                if (anySoloed && !bus.soloed) masterVol = 0.0f;
                if (anySoloed && bus.soloed) masterVol = bus.volume;
                if (!anySoloed && !bus.muted) masterVol = bus.volume;

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
                    if (parentEffective == 0.0f)
                    {
                        // Parent is not soloed, but child is — use own volume
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
