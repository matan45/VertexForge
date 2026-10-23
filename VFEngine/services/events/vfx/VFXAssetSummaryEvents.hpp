#pragma once

#include "../EventTypes.hpp"
#include "../../data/VFXAssetSummary.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace services::events::vfxassets
{
    // VK-1657 — authored summary of each .vfVFX / .vfVFXSequence, read from disk (never a runtime
    // cache). One result per path, in order; a file that cannot be read reports valid = false.
    // Paths are absolute or project-relative. Handled by VFXAssetSummaryService (Editor).
    struct GetVFXAssetSummariesQuery : ::events::IQuery<std::vector<VFXAssetSummary>>
    {
        std::vector<std::string> paths;
        std::string_view getName() const override { return "GetVFXAssetSummaries"; }
    };
}
