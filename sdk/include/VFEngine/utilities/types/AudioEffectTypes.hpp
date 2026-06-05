#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <variant>

namespace types
{
    enum class AudioEffectType : uint8_t
    {
        Reverb = 0,
        EQ = 1,
        Compressor = 2,
        Echo = 3,
        Chorus = 4
    };

    struct ReverbParams
    {
        float density = 1.0f;
        float diffusion = 1.0f;
        float gain = 0.32f;
        float gainHF = 0.89f;
        float gainLF = 1.0f;
        float decayTime = 1.49f;
        float decayHFRatio = 0.83f;
        float decayLFRatio = 1.0f;
        float reflectionsGain = 0.05f;
        float reflectionsDelay = 0.007f;
        float reflectionsPan[3] = {0.0f, 0.0f, 0.0f};
        float lateReverbGain = 1.26f;
        float lateReverbDelay = 0.011f;
        float lateReverbPan[3] = {0.0f, 0.0f, 0.0f};
        float echoTime = 0.25f;
        float echoDepth = 0.0f;
        float modulationTime = 0.25f;
        float modulationDepth = 0.0f;
        float airAbsorptionGainHF = 0.994f;
        float hfReference = 5000.0f;
        float lfReference = 250.0f;
        float roomRolloffFactor = 0.0f;
        int decayHFLimit = 1;

        std::string presetName;
    };

    struct EQParams
    {
        float lowGain = 1.0f;
        float lowCutoff = 200.0f;
        float mid1Gain = 1.0f;
        float mid1Center = 500.0f;
        float mid1Width = 1.0f;
        float mid2Gain = 1.0f;
        float mid2Center = 3000.0f;
        float mid2Width = 1.0f;
        float highGain = 1.0f;
        float highCutoff = 6000.0f;
    };

    struct CompressorParams
    {
        bool onOff = true;
    };

    struct EchoParams
    {
        float delay = 0.1f;
        float lrDelay = 0.1f;
        float damping = 0.5f;
        float feedback = 0.5f;
        float spread = -1.0f;
    };

    struct ChorusParams
    {
        int waveform = 1; // 0=sinusoid, 1=triangle
        int phase = 90;
        float rate = 1.1f;
        float depth = 0.1f;
        float feedback = 0.25f;
        float delay = 0.016f;
    };

    struct BusEffectConfig
    {
        uint32_t id = 0;
        AudioEffectType type = AudioEffectType::Reverb;
        bool enabled = true;
        float wetDryMix = 1.0f;
        std::variant<ReverbParams, EQParams, CompressorParams, EchoParams, ChorusParams> params;

        static BusEffectConfig createDefault(AudioEffectType effectType)
        {
            BusEffectConfig config;
            config.type = effectType;
            switch (effectType)
            {
            case AudioEffectType::Reverb:
                config.params = ReverbParams{};
                break;
            case AudioEffectType::EQ:
                config.params = EQParams{};
                break;
            case AudioEffectType::Compressor:
                config.params = CompressorParams{};
                break;
            case AudioEffectType::Echo:
                config.params = EchoParams{};
                break;
            case AudioEffectType::Chorus:
                config.params = ChorusParams{};
                break;
            }
            return config;
        }
    };

    inline std::string audioEffectTypeToString(AudioEffectType type)
    {
        switch (type)
        {
        case AudioEffectType::Reverb: return "Reverb";
        case AudioEffectType::EQ: return "EQ";
        case AudioEffectType::Compressor: return "Compressor";
        case AudioEffectType::Echo: return "Echo";
        case AudioEffectType::Chorus: return "Chorus";
        default: return "Unknown";
        }
    }

    inline AudioEffectType stringToAudioEffectType(const std::string& str)
    {
        if (str == "Reverb") return AudioEffectType::Reverb;
        if (str == "EQ") return AudioEffectType::EQ;
        if (str == "Compressor") return AudioEffectType::Compressor;
        if (str == "Echo") return AudioEffectType::Echo;
        if (str == "Chorus") return AudioEffectType::Chorus;
        return AudioEffectType::Reverb;
    }
}
