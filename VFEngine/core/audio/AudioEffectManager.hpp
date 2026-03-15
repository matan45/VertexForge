#pragma once
#include "AudioSystem.hpp"
#include "types/AudioEffectTypes.hpp"
#include <AL/al.h>
#include <AL/efx.h>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace core::audio
{
    struct EffectSlotEntry
    {
        uint32_t configId = 0;
        ALuint effectObject = 0;
        ALuint auxSlot = 0;
        types::AudioEffectType type = types::AudioEffectType::Reverb;
        bool enabled = true;
        float wetDryMix = 1.0f;
    };

    class AudioEffectManager
    {
    public:
        void init(int maxSends);
        void cleanUp();

        bool addEffect(uint32_t busId, const types::BusEffectConfig& config);
        bool removeEffect(uint32_t busId, uint32_t effectId);
        bool updateEffectParams(uint32_t busId, uint32_t effectId, const types::BusEffectConfig& config);
        bool setEffectEnabled(uint32_t busId, uint32_t effectId, bool enabled);
        bool setEffectWetDry(uint32_t busId, uint32_t effectId, float wetDry);
        void clearBusEffects(uint32_t busId);

        void routeSourceToBus(ALuint sourceId, uint32_t busId);
        void unrouteSource(ALuint sourceId, uint32_t busId);

        std::vector<types::BusEffectConfig> getBusEffectChain(uint32_t busId) const;
        int getMaxEffectsPerBus() const { return maxSends; }

    private:
        void applyReverbParams(ALuint effect, const types::ReverbParams& p);
        void applyEQParams(ALuint effect, const types::EQParams& p);
        void applyCompressorParams(ALuint effect, const types::CompressorParams& p);
        void applyEchoParams(ALuint effect, const types::EchoParams& p);
        void applyChorusParams(ALuint effect, const types::ChorusParams& p);
        void applyEffectParams(ALuint effect, const types::BusEffectConfig& config);

        EffectSlotEntry* findEffect(uint32_t busId, uint32_t effectId);
        const EffectSlotEntry* findEffect(uint32_t busId, uint32_t effectId) const;

        std::unordered_map<uint32_t, std::vector<EffectSlotEntry>> busEffects;
        int maxSends = 0;
        uint32_t nextEffectId = 1;
    };
}
