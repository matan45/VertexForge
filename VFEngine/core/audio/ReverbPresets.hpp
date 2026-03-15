#pragma once
#include "types/AudioEffectTypes.hpp"
#include <string>
#include <vector>

namespace core::audio
{
    class ReverbPresets
    {
    public:
        static types::ReverbParams fromPreset(const std::string& name);
        static std::vector<std::string> getPresetNames();
    };
}
