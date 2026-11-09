#pragma once

#include "../../data/VFXAssetSummary.hpp"
#include <string>
#include <vector>

namespace services
{
    // VK-1657 — answers services::events::vfxassets::GetVFXAssetSummariesQuery with the AUTHORED
    // values of .vfVFX effects and .vfVFXSequence combos, read from disk on every call (no cache:
    // a file saved a moment ago is reported as saved). The effect fields are read key for key like
    // VFXEmitterConfigLoader::fromVFXData, but only through the graphics-free VFX loaders.
    class VFXAssetSummaryService
    {
    public:
        VFXAssetSummaryService() = default;
        ~VFXAssetSummaryService();

        VFXAssetSummaryService(const VFXAssetSummaryService&) = delete;
        VFXAssetSummaryService& operator=(const VFXAssetSummaryService&) = delete;

        void registerEventHandlers();
        void unregisterEventHandlers();

        // `path` is absolute or project-relative; a .vfVFXSequence (any case) is read as a combo,
        // anything else as an effect. Failures come back with valid = false and `error` set.
        static VFXAssetSummary summarizeAsset(const std::string& path);
        static std::vector<VFXAssetSummary> summarizeAssets(const std::vector<std::string>& paths);

    private:
        bool handlersRegistered = false;
    };
}
