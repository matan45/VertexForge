#pragma once

#include <string>
#include <vector>
#include <bitset>
#include <cstdint>

namespace animator
{
    constexpr uint32_t MAX_BONE_MASK_SIZE = 256;
    using BoneMask = std::bitset<MAX_BONE_MASK_SIZE>;

    enum class LayerBlendMode : uint8_t
    {
        Override,
        Additive
    };

    enum class LayerSourceMode : uint8_t
    {
        StateMachine,
        DirectClip
    };

    struct BoneMaskDefinition
    {
        std::string name;
        std::vector<std::string> includedBoneNames;
        BoneMask resolvedMask;
    };

    inline const char* layerBlendModeToString(LayerBlendMode mode)
    {
        switch (mode)
        {
        case LayerBlendMode::Override: return "Override";
        case LayerBlendMode::Additive: return "Additive";
        default: return "Override";
        }
    }

    inline LayerBlendMode stringToLayerBlendMode(const std::string& str)
    {
        if (str == "Additive") return LayerBlendMode::Additive;
        return LayerBlendMode::Override;
    }

    inline const char* layerSourceModeToString(LayerSourceMode mode)
    {
        switch (mode)
        {
        case LayerSourceMode::StateMachine: return "StateMachine";
        case LayerSourceMode::DirectClip: return "DirectClip";
        default: return "StateMachine";
        }
    }

    inline LayerSourceMode stringToLayerSourceMode(const std::string& str)
    {
        if (str == "DirectClip") return LayerSourceMode::DirectClip;
        return LayerSourceMode::StateMachine;
    }
}
