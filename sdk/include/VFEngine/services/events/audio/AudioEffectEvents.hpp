#pragma once
#include "../EventTypes.hpp"
#include "types/AudioEffectTypes.hpp"
#include <string>
#include <vector>

namespace events::audio
{
    struct AddBusEffectCommand : ::events::ICommand<bool>
    {
        std::string busName;
        types::BusEffectConfig config;
        std::string_view getName() const override { return "AddBusEffect"; }
    };

    struct RemoveBusEffectCommand : ::events::ICommand<bool>
    {
        std::string busName;
        uint32_t effectId = 0;
        std::string_view getName() const override { return "RemoveBusEffect"; }
    };

    struct UpdateBusEffectCommand : ::events::ICommand<bool>
    {
        std::string busName;
        uint32_t effectId = 0;
        types::BusEffectConfig config;
        std::string_view getName() const override { return "UpdateBusEffect"; }
    };

    struct SetBusEffectEnabledCommand : ::events::ICommand<bool>
    {
        std::string busName;
        uint32_t effectId = 0;
        bool enabled = true;
        std::string_view getName() const override { return "SetBusEffectEnabled"; }
    };

    struct SetBusEffectWetDryCommand : ::events::ICommand<bool>
    {
        std::string busName;
        uint32_t effectId = 0;
        float wetDryMix = 1.0f;
        std::string_view getName() const override { return "SetBusEffectWetDry"; }
    };

    struct GetBusEffectChainQuery : ::events::IQuery<std::vector<types::BusEffectConfig>>
    {
        std::string busName;
        std::string_view getName() const override { return "GetBusEffectChain"; }
    };

    struct GetMaxEffectsPerBusQuery : ::events::IQuery<int>
    {
        std::string_view getName() const override { return "GetMaxEffectsPerBus"; }
    };
}
