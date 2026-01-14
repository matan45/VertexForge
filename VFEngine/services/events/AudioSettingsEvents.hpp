#pragma once

#include "EventTypes.hpp"
#include "../../utilities/types/AudioTypes.hpp"

namespace events::audio {

    // Command to apply audio settings to OpenAL
    struct ApplyAudioSettingsCommand : ::events::ICommand<bool> {
        types::AudioSettings settings;
        std::string_view getName() const override { return "ApplyAudioSettings"; }
    };

    // Query to get current audio settings from provider
    struct GetAudioSettingsQuery : ::events::IQuery<types::AudioSettings> {
        std::string_view getName() const override { return "GetAudioSettings"; }
    };

}
