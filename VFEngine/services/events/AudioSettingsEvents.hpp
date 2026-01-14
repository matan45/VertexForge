#pragma once

#include "EventTypes.hpp"
#include "types/AudioTypes.hpp"

namespace events::audio
{
    struct ApplyAudioSettingsCommand : ::events::ICommand<bool>
    {
        types::AudioSettings settings;
        std::string_view getName() const override { return "ApplyAudioSettings"; }
    };
    
    struct GetAudioSettingsQuery : ::events::IQuery<types::AudioSettings>
    {
        std::string_view getName() const override { return "GetAudioSettings"; }
    };
}
