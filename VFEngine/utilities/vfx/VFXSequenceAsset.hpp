#pragma once

#include "VFXSequenceTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <optional>

namespace vfx
{
    // 1.1 (VK-1451) adds seed/playbackRate/fixedStep/prewarm/eventMarkers.
    // 1.2 (VK-1452) replaces scalar/vector override lists with typed overrides
    // and adds marker cue payloads.
    // The loader stays version-agnostic (tolerant j.value defaults), so 1.0
    // assets load unchanged and 1.1 assets load on older readers minus the new
    // fields.
    inline constexpr const char* VFX_SEQUENCE_FORMAT_VERSION = "1.2";

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
