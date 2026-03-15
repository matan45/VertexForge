#pragma once
#include "AudioSourceManager.hpp"
#include "types/AudioTypes.hpp"
#include "types/AudioEffectTypes.hpp"
#include <AL/al.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <functional>

namespace core::audio
{
    struct AudioBus
    {
        uint32_t id = 0;
        std::string name;
        uint32_t parentId = 0;
        float volume = 1.0f;
        bool muted = false;
        bool soloed = false;
        float effectiveVolume = 1.0f;
        std::vector<uint32_t> childIds;
    };

    class AudioEffectManager;

    struct MixSnapshot
    {
        std::string name;
        std::map<std::string, float> busVolumes;
        std::map<std::string, bool> busMutes;
        std::map<std::string, std::vector<types::BusEffectConfig>> busEffects;
    };

    struct TrackedSource
    {
        AudioHandle handle = 0;
        uint32_t busId = 0;
        float userVolume = 1.0f;
    };

    class AudioBusManager
    {
    public:
        using VolumeApplyCallback = std::function<void(AudioHandle, float)>;
        using SourceResolveCallback = std::function<ALuint(AudioHandle)>;

        void init(VolumeApplyCallback callback);
        void setEffectManager(AudioEffectManager* manager);
        void setSourceResolveCallback(SourceResolveCallback callback);
        void cleanUp();

        // Bus management
        uint32_t createBus(const std::string& name, const std::string& parentName = "Master");
        AudioBus* getBus(uint32_t busId);
        AudioBus* getBusByName(const std::string& name);
        uint32_t getBusIdByName(const std::string& name) const;
        std::vector<std::string> getBusNames() const;

        // Bus controls
        void setBusVolume(const std::string& name, float volume);
        void setBusMuted(const std::string& name, bool muted);
        void setBusSoloed(const std::string& name, bool soloed);
        float getBusVolume(const std::string& name) const;
        bool isBusMuted(const std::string& name) const;
        bool isBusSoloed(const std::string& name) const;

        // Source-to-bus assignment
        void assignSource(AudioHandle handle, const std::string& busName, float userVolume);
        void removeSource(AudioHandle handle);
        void setSourceUserVolume(AudioHandle handle, float volume);

        // Snapshots
        void saveSnapshot(const std::string& name);
        void loadSnapshot(const std::string& name);
        void deleteSnapshot(const std::string& name);
        std::vector<std::string> getSnapshotNames() const;

        // Effect chain (delegates to effectManager)
        bool addBusEffect(const std::string& busName, const types::BusEffectConfig& config);
        bool removeBusEffect(const std::string& busName, uint32_t effectId);
        bool updateBusEffect(const std::string& busName, uint32_t effectId, const types::BusEffectConfig& config);
        bool setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled);
        bool setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry);
        std::vector<types::BusEffectConfig> getBusEffectChain(const std::string& busName) const;
        int getMaxEffectsPerBus() const;

        // Setup
        void createDefaultBuses();
        void loadBusDefinitions(const std::vector<types::AudioBusDefinition>& definitions);
        void loadSnapshots(const std::vector<types::AudioMixSnapshotDefinition>& snapshotDefs);
        std::vector<types::AudioMixSnapshotDefinition> getSnapshotDefinitions() const;

    private:
        void recalculateEffectiveVolumes();
        void recalculateBusEffective(AudioBus& bus, float parentEffective, bool parentMuted, bool anySoloed);
        void applyEffectiveVolumesToSources();

        std::vector<AudioBus> buses;
        std::unordered_map<std::string, uint32_t> nameToId;
        std::unordered_map<AudioHandle, TrackedSource> trackedSources;
        std::map<std::string, MixSnapshot> snapshots;
        uint32_t nextBusId = 0;
        VolumeApplyCallback volumeCallback;
        SourceResolveCallback sourceResolveCallback;
        AudioEffectManager* effectManager = nullptr;
    };
}
