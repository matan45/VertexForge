#include "AudioBusManager.hpp"
#include "AudioEffectManager.hpp"
#include "ReverbZoneManager.hpp"
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

    void AudioBusManager::setReverbZoneManager(ReverbZoneManager* manager)
    {
        reverbZoneManager = manager;
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
        std::unique_lock lock(busMutex);
        return createBusInternal(name, parentName);
    }

    uint32_t AudioBusManager::createBusInternal(const std::string& name, const std::string& parentName)
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

                uint32_t ancestor = parentId;
                int maxDepth = 32;
                while (maxDepth-- > 0)
                {
                    auto* parentBus = getBus(ancestor);
                    if (!parentBus || parentBus->id == parentBus->parentId) break;
                    ancestor = parentBus->parentId;
                }
                if (maxDepth <= 0)
                {
                    vfLogWarning("AudioBusManager: Circular parent detected for bus '{}', defaulting to Master", name);
                    parentId = 0;
                }
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
        std::shared_lock lock(busMutex);
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
        std::unique_lock lock(busMutex);
        auto* bus = getBusByName(name);
        if (!bus) return;

        bus->volume = std::clamp(volume, 0.0f, 1.0f);
        volumesDirty = true;
    }

    void AudioBusManager::setBusMuted(const std::string& name, bool muted)
    {
        std::unique_lock lock(busMutex);
        auto* bus = getBusByName(name);
        if (!bus) return;

        bus->muted = muted;
        volumesDirty = true;
    }

    void AudioBusManager::setBusSoloed(const std::string& name, bool soloed)
    {
        std::unique_lock lock(busMutex);
        auto* bus = getBusByName(name);
        if (!bus) return;

        bus->soloed = soloed;
        volumesDirty = true;
    }

    void AudioBusManager::flushDirtyVolumes()
    {
        std::unique_lock lock(busMutex);
        if (!volumesDirty) return;
        volumesDirty = false;
        recalculateEffectiveVolumes();
        applyEffectiveVolumesToSources();
    }

    float AudioBusManager::getBusVolume(const std::string& name) const
    {
        std::shared_lock lock(busMutex);
        auto it = nameToId.find(name);
        if (it == nameToId.end()) return 1.0f;

        for (const auto& bus : buses)
        {
            if (bus.id == it->second) return bus.volume;
        }
        return 1.0f;
    }

    float AudioBusManager::getBusEffectiveVolume(const std::string& name) const
    {
        std::shared_lock lock(busMutex);
        auto it = nameToId.find(name);
        if (it == nameToId.end()) return 1.0f;

        for (const auto& bus : buses)
        {
            if (bus.id == it->second) return bus.effectiveVolume;
        }
        return 1.0f;
    }

    bool AudioBusManager::isBusMuted(const std::string& name) const
    {
        std::shared_lock lock(busMutex);
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
        std::shared_lock lock(busMutex);
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
        std::unique_lock lock(busMutex);
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

        if (sourceResolveCallback)
        {
            ALuint sourceId = sourceResolveCallback(handle);
            if (sourceId != 0)
            {
                if (effectManager)
                {
                    effectManager->routeSourceToBus(sourceId, busId);
                }
                if (reverbZoneManager)
                {
                    reverbZoneManager->routeSource(sourceId);
                }
            }
        }
    }

    void AudioBusManager::removeSource(AudioHandle handle)
    {
        std::unique_lock lock(busMutex);
        auto it = trackedSources.find(handle);
        if (it != trackedSources.end())
        {
            if (sourceResolveCallback)
            {
                ALuint sourceId = sourceResolveCallback(handle);
                if (sourceId != 0)
                {
                    if (effectManager)
                    {
                        effectManager->unrouteSource(sourceId, it->second.busId);
                    }
                    if (reverbZoneManager)
                    {
                        reverbZoneManager->unrouteSource(sourceId);
                    }
                }
            }
            trackedSources.erase(it);
        }
    }

    void AudioBusManager::setSourceUserVolume(AudioHandle handle, float volume)
    {
        std::unique_lock lock(busMutex);
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
        std::unique_lock lock(busMutex);
        MixSnapshot snapshot;
        snapshot.name = name;
        for (const auto& bus : buses)
        {
            snapshot.busVolumes[bus.id] = bus.volume;
            snapshot.busMutes[bus.id] = bus.muted;

            if (effectManager)
            {
                auto chain = effectManager->getBusEffectChain(bus.id);
                if (!chain.empty())
                {
                    snapshot.busEffects[bus.id] = chain;
                }
            }
        }
        snapshots[name] = std::move(snapshot);
    }

    void AudioBusManager::loadSnapshot(const std::string& name)
    {
        std::unique_lock lock(busMutex);
        auto it = snapshots.find(name);
        if (it == snapshots.end()) return;

        const auto& snapshot = it->second;
        for (auto& bus : buses)
        {
            auto volIt = snapshot.busVolumes.find(bus.id);
            if (volIt != snapshot.busVolumes.end())
            {
                bus.volume = volIt->second;
            }

            auto muteIt = snapshot.busMutes.find(bus.id);
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
        std::unique_lock lock(busMutex);
        snapshots.erase(name);
    }

    std::vector<std::string> AudioBusManager::getSnapshotNames() const
    {
        std::shared_lock lock(busMutex);
        std::vector<std::string> names;
        for (const auto& [name, _] : snapshots)
        {
            names.push_back(name);
        }
        return names;
    }

    void AudioBusManager::createBusesFromDefinitions(const std::vector<types::AudioBusDefinition>& definitions)
    {
        bool hasMaster = false;
        for (const auto& def : definitions)
        {
            if (def.name == "Master") { hasMaster = true; break; }
        }

        if (!hasMaster)
            createBusInternal("Master", "");

        for (const auto& def : definitions)
        {
            if (def.name == "Master")
            {
                createBusInternal("Master", "");
                auto* bus = getBusByName("Master");
                if (bus) bus->volume = def.defaultVolume;
            }
        }

        for (const auto& def : definitions)
        {
            if (def.name != "Master")
            {
                createBusInternal(def.name, def.parentName);
                auto* bus = getBusByName(def.name);
                if (bus) bus->volume = def.defaultVolume;
            }
        }
    }

    void AudioBusManager::createDefaultBuses()
    {
        createBusInternal(BusNames::Master, "");
        createBusInternal(BusNames::Music, BusNames::Master);
        createBusInternal(BusNames::SFX, BusNames::Master);
        createBusInternal(BusNames::Dialogue, BusNames::Master);
        createBusInternal(BusNames::Ambient, BusNames::Master);
    }

    void AudioBusManager::loadBusDefinitions(const std::vector<types::AudioBusDefinition>& definitions)
    {
        std::unique_lock lock(busMutex);
        if (effectManager)
        {
            for (const auto& bus : buses)
                effectManager->clearBusEffects(bus.id);
        }

        buses.clear();
        nameToId.clear();
        nextBusId = 0;

        if (definitions.empty())
        {
            createDefaultBuses();
            return;
        }

        createBusesFromDefinitions(definitions);

        if (effectManager)
        {
            for (const auto& def : definitions)
            {
                uint32_t busId = getBusIdByName(def.name);
                for (const auto& effectConfig : def.effects)
                    effectManager->addEffect(busId, effectConfig);
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
            for (const auto& [busName, vol] : def.busVolumes)
            {
                uint32_t id = getBusIdByName(busName);
                snapshot.busVolumes[id] = vol;
            }
            for (const auto& [busName, muted] : def.busMutes)
            {
                uint32_t id = getBusIdByName(busName);
                snapshot.busMutes[id] = muted;
            }
            snapshots[def.name] = std::move(snapshot);
        }
    }

    std::vector<types::AudioMixSnapshotDefinition> AudioBusManager::getSnapshotDefinitions() const
    {
        std::shared_lock lock(busMutex);
        std::vector<types::AudioMixSnapshotDefinition> defs;
        for (const auto& [name, snapshot] : snapshots)
        {
            types::AudioMixSnapshotDefinition def;
            def.name = snapshot.name;
            for (const auto& [busId, vol] : snapshot.busVolumes)
            {
                for (const auto& bus : buses)
                {
                    if (bus.id == busId) { def.busVolumes[bus.name] = vol; break; }
                }
            }
            for (const auto& [busId, muted] : snapshot.busMutes)
            {
                for (const auto& bus : buses)
                {
                    if (bus.id == busId) { def.busMutes[bus.name] = muted; break; }
                }
            }
            defs.push_back(std::move(def));
        }
        return defs;
    }

}
