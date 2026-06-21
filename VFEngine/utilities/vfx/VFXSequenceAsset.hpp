#pragma once

#include "VFXSequenceTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <optional>

namespace vfx
{
    inline constexpr const char* VFX_SEQUENCE_FORMAT_VERSION = "1.0";

    class VFXSequenceAsset
    {
    public:
        static std::optional<VFXSequenceData> load(std::string_view path);
        static bool save(const VFXSequenceData& data, std::string_view path);
        static VFXSequenceData createDefault(const std::string& name = "New Sequence");

    private:
        static nlohmann::json serializeStep(const VFXSequenceStep& step);
        static std::optional<VFXSequenceStep> deserializeStep(const nlohmann::json& j);
    };
}
